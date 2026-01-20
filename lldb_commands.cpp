#include "lldb_client.hpp"
#include "session_store.hpp"
#include "settings.hpp"
#include "system_prompt.hpp"

#include <atomic>
#include <chrono>
#include <libagents/agent.hpp>
#include <libagents/provider.hpp>
#include <libagents/tool_builder.hpp>
#include <lldb/API/SBCommandInterpreter.h>
#include <lldb/API/SBCommandReturnObject.h>
#include <sstream>
#include <string>

namespace lldb_copilot
{

namespace
{
struct AgentSession
{
    std::unique_ptr<libagents::IAgent> agent;
    libagents::ProviderType provider = libagents::ProviderType::Copilot;
    std::string provider_name;
    std::string target;
    std::string session_id;
    std::string system_prompt;
    bool primed = false;
    bool initialized = false;
    bool host_ready = false;
    std::atomic<bool> aborted{false};
    LldbClient* dbg = nullptr;
    libagents::HostContext host;
};

AgentSession& GetAgentSession()
{
    static AgentSession session;
    return session;
}

void ResetAgentSession(AgentSession& session)
{
    if (session.agent)
    {
        session.agent->shutdown();
        session.agent.reset();
    }
    session.initialized = false;
    session.host_ready = false;
    session.provider_name.clear();
    session.session_id.clear();
    session.system_prompt.clear();
    session.primed = false;
    session.target.clear();
}

libagents::Tool BuildDebuggerTool(AgentSession& session)
{
    return libagents::make_tool(
        "dbg_exec",
        "Execute an LLDB debugger command and return its output. "
        "Use this to inspect the target process, memory, threads, stack, registers, etc.",
        [&session](std::string command) -> std::string
        {
            if (session.aborted.load())
                return "(Aborted)";

            if (!session.dbg)
                return "Error: No debugger client available";

            return session.dbg->ExecuteCommand(command);
        },
        {"command"});
}

void ConfigureHost(AgentSession& session)
{
    if (session.host_ready)
        return;

    session.host.should_abort = [&session]()
    {
        if (session.dbg && session.dbg->IsInterrupted())
            session.aborted = true;
        return session.aborted.load();
    };

    session.host.on_event = [&session](const libagents::Event& event)
    {
        if (!session.dbg)
            return;

        switch (event.type)
        {
        case libagents::EventType::ContentDelta:
            session.dbg->OutputThinking(event.content);
            break;
        case libagents::EventType::ContentComplete:
            session.dbg->Output("\n");
            session.dbg->OutputResponse(event.content.empty() ? "(No output)" : event.content);
            break;
        case libagents::EventType::Error:
            if (!event.error_message.empty())
                session.dbg->OutputError(event.error_message);
            else if (!event.content.empty())
                session.dbg->OutputError(event.content);
            else
                session.dbg->OutputError("Error");
            break;
        default:
            break;
        }
    };

    session.host_ready = true;
}

bool EnsureAgent(AgentSession& session, LldbClient& dbg_client,
                 const lldb_copilot::Settings& settings, const std::string& target,
                 std::string* error, bool* created)
{
    if (created)
        *created = false;

    session.dbg = &dbg_client;

    if (session.agent && session.provider != settings.default_provider)
        ResetAgentSession(session);

    if (!session.agent)
    {
        session.provider = settings.default_provider;
        session.provider_name = libagents::provider_type_name(session.provider);
        session.agent = libagents::create_agent(session.provider);
        if (!session.agent)
        {
            if (error)
                *error = "Failed to create agent";
            return false;
        }

        session.agent->register_tool(BuildDebuggerTool(session));

        session.system_prompt = lldb_copilot::GetFullSystemPrompt(settings.custom_prompt);
        session.primed = false; // will prepend on first user query instead of system_prompt

        // Apply BYOK settings if enabled
        const auto* byok = settings.get_byok();
        if (byok && byok->is_usable())
            session.agent->set_byok(byok->to_config());

        // Apply response timeout setting
        if (settings.response_timeout_ms > 0)
            session.agent->set_response_timeout(
                std::chrono::milliseconds(settings.response_timeout_ms));

        // Skip session resume when BYOK is enabled (not supported by BYOK providers)
        if (!(byok && byok->is_usable()))
        {
            session.session_id =
                lldb_copilot::GetSessionStore().GetSessionId(target, session.provider_name);
            if (!session.session_id.empty())
                session.agent->set_session_id(session.session_id);
        }

        if (!session.agent->initialize())
        {
            if (error)
                *error = "Failed to initialize: " + session.agent->provider_name();
            ResetAgentSession(session);
            return false;
        }

        ConfigureHost(session);
        session.initialized = true;

        if (created)
            *created = true;
    }

    std::string updated_prompt = lldb_copilot::GetFullSystemPrompt(settings.custom_prompt);
    if (updated_prompt != session.system_prompt)
    {
        session.system_prompt = updated_prompt;
        session.primed = false; // re-prime on next query
    }

    if (session.target != target)
    {
        session.target = target;
        // Skip session resume when BYOK is enabled (not supported by BYOK providers)
        const auto* byok_check = settings.get_byok();
        if (!(byok_check && byok_check->is_usable()))
        {
            std::string new_session_id =
                lldb_copilot::GetSessionStore().GetSessionId(target, session.provider_name);
            if (new_session_id != session.session_id)
            {
                if (session.agent)
                {
                    session.agent->clear_session();
                    session.session_id = new_session_id;
                    if (!session.session_id.empty())
                        session.agent->set_session_id(session.session_id);
                }
            }
        }
        session.primed = false; // new target -> re-prime next query
    }

    session.aborted = false;
    return true;
}
} // namespace

// Helper to join command args into a string
static std::string JoinArgs(char** command)
{
    std::string result;
    for (int i = 0; command && command[i]; i++)
    {
        if (i > 0)
            result += " ";
        result += command[i];
    }
    return result;
}

// "copilot" command - purely for asking questions, no subcommands
class CopilotCommand : public lldb::SBCommandPluginInterface
{
  public:
    bool DoExecute(lldb::SBDebugger debugger, char** command,
                   lldb::SBCommandReturnObject& result) override
    {
        std::string question = JoinArgs(command);
        if (question.empty())
        {
            result.SetError(
                "Usage: copilot <question>\n\nExamples:\n  copilot what is the call stack?\n  "
                "copilot explain this crash\n\nFor Copilot settings, use: agent help");
            return false;
        }

        LldbClient client(debugger);
        auto settings = lldb_copilot::LoadSettings();
        auto& session = GetAgentSession();
        std::string target = client.GetTargetName();

        std::string error;
        bool created = false;
        if (!EnsureAgent(session, client, settings, target, &error, &created))
        {
            result.SetError((error.empty() ? "Failed to initialize" : error).c_str());
            return false;
        }

        std::string provider_name = libagents::provider_type_name(settings.default_provider);
        client.OutputThinking("[" + provider_name + "] Asking: " + question);
        if (created)
            client.OutputThinking("Initializing " + provider_name + " provider...");

        try
        {
            std::string full_prompt =
                (session.primed || session.system_prompt.empty())
                    ? question
                    : (session.system_prompt + "\n\n---\n\n" + question);

            std::string response = session.agent->query_hosted(full_prompt, session.host);
            session.primed = true;
            if (response == "(Aborted)")
                client.OutputWarning("Aborted.");

            // Skip session persistence when BYOK is enabled (not supported by BYOK providers)
            const auto* byok_save = settings.get_byok();
            if (!(byok_save && byok_save->is_usable()))
            {
                std::string new_session_id = session.agent->get_session_id();
                if (!new_session_id.empty() && new_session_id != session.session_id)
                {
                    lldb_copilot::GetSessionStore().SetSessionId(target, provider_name,
                                                                 new_session_id);
                    session.session_id = new_session_id;
                }
            }

            result.SetStatus(lldb::eReturnStatusSuccessFinishResult);
        }
        catch (const std::exception& e)
        {
            result.SetError(e.what());
            return false;
        }

        return true;
    }
};

// "agent" command - for control/settings only
class AgentCommand : public lldb::SBCommandPluginInterface
{
  public:
    bool DoExecute(lldb::SBDebugger debugger, char** command,
                   lldb::SBCommandReturnObject& result) override
    {
        std::string args = JoinArgs(command);
        auto settings = lldb_copilot::LoadSettings();
        auto& session = GetAgentSession();
        LldbClient client(debugger);

        // Parse subcommand
        std::string subcmd, rest;
        size_t space = args.find(' ');
        if (space != std::string::npos)
        {
            subcmd = args.substr(0, space);
            rest = args.substr(space + 1);
        }
        else
        {
            subcmd = args;
        }

        if (subcmd.empty() || subcmd == "help")
        {
            const auto* byok = settings.get_byok();
            result.Printf(
                "LLDB Copilot - AI-powered debugger assistant\n\n"
                "Commands:\n"
                "  copilot <question>     Ask the AI a question\n"
                "  agent help             Show this help\n"
                "  agent version          Show version information\n"
                "  agent provider         Show current provider\n"
                "  agent provider <name>  Switch provider (claude, copilot)\n"
                "  agent clear            Clear conversation history\n"
                "  agent prompt           Show custom prompt\n"
                "  agent prompt <text>    Set custom prompt\n"
                "  agent prompt clear     Clear custom prompt\n"
                "  agent timeout          Show response timeout\n"
                "  agent timeout <ms>     Set response timeout in milliseconds\n"
                "  agent byok             Show BYOK status\n"
                "  agent byok enable      Enable BYOK for current provider\n"
                "  agent byok disable     Disable BYOK\n"
                "  agent byok key <val>   Set BYOK API key\n"
                "  agent byok endpoint <url>  Set BYOK endpoint\n"
                "  agent byok model <name>    Set BYOK model\n"
                "  agent byok type <type>     Set BYOK type (openai, anthropic, azure)\n\n"
                "Examples:\n"
                "  copilot what is the call stack?\n"
                "  copilot help me understand this crash\n"
                "  agent provider claude\n"
                "  agent byok key sk-xxx\n"
                "  agent byok enable\n\n"
                "Current provider: %s%s\n",
                libagents::provider_type_name(settings.default_provider),
                (byok && byok->is_usable()) ? " (BYOK enabled)" : "");
        }
        else if (subcmd == "version")
        {
            result.Printf("LLDB Copilot v0.1.0\nCurrent provider: %s\n",
                          libagents::provider_type_name(settings.default_provider));
        }
        else if (subcmd == "provider")
        {
            if (rest.empty())
            {
                result.Printf("Current provider: %s\n\nAvailable: claude, copilot\n",
                              libagents::provider_type_name(settings.default_provider));
            }
            else
            {
                try
                {
                    auto type = lldb_copilot::ParseProviderType(rest);
                    if (type != settings.default_provider)
                    {
                        settings.default_provider = type;
                        lldb_copilot::SaveSettings(settings);
                        ResetAgentSession(session);
                    }
                    result.Printf("Provider set to: %s\n", libagents::provider_type_name(type));
                }
                catch (const std::exception& e)
                {
                    result.SetError(e.what());
                    return false;
                }
            }
        }
        else if (subcmd == "clear")
        {
            std::string target = client.GetTargetName();
            std::string provider_name = libagents::provider_type_name(settings.default_provider);
            if (session.agent)
            {
                session.agent->clear_session();
                session.session_id.clear();
            }
            lldb_copilot::GetSessionStore().ClearSession(target, provider_name);
            result.Printf("Conversation history cleared.\n");
        }
        else if (subcmd == "prompt")
        {
            if (rest.empty())
            {
                if (settings.custom_prompt.empty())
                    result.Printf("No custom prompt set.\n");
                else
                    result.Printf("Custom prompt:\n%s\n", settings.custom_prompt.c_str());
            }
            else if (rest == "clear")
            {
                settings.custom_prompt.clear();
                lldb_copilot::SaveSettings(settings);
                if (session.agent)
                {
                    session.system_prompt =
                        lldb_copilot::GetFullSystemPrompt(settings.custom_prompt);
                    session.primed = false; // re-prime next turn
                }
                result.Printf("Custom prompt cleared.\n");
            }
            else
            {
                settings.custom_prompt = rest;
                lldb_copilot::SaveSettings(settings);
                if (session.agent)
                {
                    session.system_prompt =
                        lldb_copilot::GetFullSystemPrompt(settings.custom_prompt);
                    session.primed = false; // re-prime next turn
                }
                result.Printf("Custom prompt set.\n");
            }
        }
        else if (subcmd == "timeout")
        {
            if (rest.empty())
            {
                result.Printf("Response timeout: %d ms (%d seconds)\n",
                              settings.response_timeout_ms, settings.response_timeout_ms / 1000);
            }
            else
            {
                try
                {
                    int ms = std::stoi(rest);
                    if (ms < 1000)
                    {
                        result.SetError("Timeout must be at least 1000 ms (1 second).");
                        return false;
                    }
                    settings.response_timeout_ms = ms;
                    lldb_copilot::SaveSettings(settings);
                    if (session.agent)
                        session.agent->set_response_timeout(std::chrono::milliseconds(ms));
                    result.Printf("Timeout set to %d ms (%d seconds).\n", ms, ms / 1000);
                }
                catch (...)
                {
                    result.SetError("Invalid timeout value. Use milliseconds.");
                    return false;
                }
            }
        }
        else if (subcmd == "byok")
        {
            std::string provider_name = libagents::provider_type_name(settings.default_provider);

            // Parse BYOK subcommand
            std::string byok_subcmd, byok_value;
            size_t byok_space = rest.find(' ');
            if (byok_space != std::string::npos)
            {
                byok_subcmd = rest.substr(0, byok_space);
                byok_value = rest.substr(byok_space + 1);
                // Trim leading whitespace
                size_t val_start = byok_value.find_first_not_of(" \t");
                if (val_start != std::string::npos)
                    byok_value = byok_value.substr(val_start);
            }
            else
            {
                byok_subcmd = rest;
            }

            if (byok_subcmd.empty())
            {
                // Show BYOK status
                const auto* byok = settings.get_byok();
                result.Printf("BYOK status for provider '%s':\n", provider_name.c_str());
                if (byok)
                {
                    result.Printf("  Enabled:  %s\n", byok->enabled ? "yes" : "no");
                    result.Printf("  API Key:  %s\n",
                                  byok->api_key.empty() ? "(not set)" : "********");
                    result.Printf("  Endpoint: %s\n",
                                  byok->base_url.empty() ? "(default)" : byok->base_url.c_str());
                    result.Printf("  Model:    %s\n",
                                  byok->model.empty() ? "(default)" : byok->model.c_str());
                    result.Printf("  Type:     %s\n", byok->provider_type.empty()
                                                          ? "(default)"
                                                          : byok->provider_type.c_str());
                    result.Printf("  Usable:   %s\n", byok->is_usable() ? "yes" : "no");
                }
                else
                {
                    result.Printf("  (not configured)\n");
                }
            }
            else if (byok_subcmd == "enable")
            {
                auto& byok = settings.get_or_create_byok();
                byok.enabled = true;
                lldb_copilot::SaveSettings(settings);
                ResetAgentSession(session);
                result.Printf("BYOK enabled for provider '%s'.\n", provider_name.c_str());
                if (byok.api_key.empty())
                {
                    result.Printf(
                        "Warning: API key not set. Use 'agent byok key <value>' to set it.\n");
                }
            }
            else if (byok_subcmd == "disable")
            {
                auto& byok = settings.get_or_create_byok();
                byok.enabled = false;
                lldb_copilot::SaveSettings(settings);
                ResetAgentSession(session);
                result.Printf("BYOK disabled for provider '%s'.\n", provider_name.c_str());
            }
            else if (byok_subcmd == "key")
            {
                if (byok_value.empty())
                {
                    result.SetError(
                        "Error: API key value required.\nUsage: agent byok key <value>");
                    return false;
                }
                auto& byok = settings.get_or_create_byok();
                byok.api_key = byok_value;
                lldb_copilot::SaveSettings(settings);
                ResetAgentSession(session);
                result.Printf("BYOK API key set for provider '%s'.\n", provider_name.c_str());
            }
            else if (byok_subcmd == "endpoint")
            {
                auto& byok = settings.get_or_create_byok();
                byok.base_url = byok_value;
                lldb_copilot::SaveSettings(settings);
                ResetAgentSession(session);
                if (byok_value.empty())
                    result.Printf("BYOK endpoint cleared (using default).\n");
                else
                    result.Printf("BYOK endpoint set to: %s\n", byok_value.c_str());
            }
            else if (byok_subcmd == "model")
            {
                auto& byok = settings.get_or_create_byok();
                byok.model = byok_value;
                lldb_copilot::SaveSettings(settings);
                ResetAgentSession(session);
                if (byok_value.empty())
                    result.Printf("BYOK model cleared (using default).\n");
                else
                    result.Printf("BYOK model set to: %s\n", byok_value.c_str());
            }
            else if (byok_subcmd == "type")
            {
                auto& byok = settings.get_or_create_byok();
                byok.provider_type = byok_value;
                lldb_copilot::SaveSettings(settings);
                ResetAgentSession(session);
                if (byok_value.empty())
                    result.Printf("BYOK type cleared (using default).\n");
                else
                    result.Printf("BYOK type set to: %s\n", byok_value.c_str());
            }
            else
            {
                result.SetError(("Unknown byok subcommand: " + byok_subcmd +
                                 "\nUse 'agent byok' to see available commands.")
                                    .c_str());
                return false;
            }
        }
        else
        {
            result.SetError(
                ("Unknown subcommand: " + subcmd + "\nUse 'agent help' for usage.").c_str());
            return false;
        }

        result.SetStatus(lldb::eReturnStatusSuccessFinishResult);
        return true;
    }
};

// Register commands with LLDB
void RegisterCommands(lldb::SBDebugger& debugger)
{
    lldb::SBCommandInterpreter interp = debugger.GetCommandInterpreter();

    interp.AddCommand("copilot", new CopilotCommand(),
                      "Ask Copilot a question. Usage: copilot <question>");

    interp.AddCommand("agent", new AgentCommand(), "Copilot settings. Usage: agent help");
}

} // namespace lldb_copilot
