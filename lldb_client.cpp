#include "lldb_client.hpp"

#include <cstdio>

namespace lldb_copilot
{

// ANSI color codes for terminal output
namespace colors
{
constexpr const char* RESET = "\033[0m";
constexpr const char* RED = "\033[31m";
constexpr const char* GREEN = "\033[32m";
constexpr const char* YELLOW = "\033[33m";
constexpr const char* BLUE = "\033[34m";
constexpr const char* CYAN = "\033[36m";
constexpr const char* DIM = "\033[2m";
} // namespace colors

LldbClient::LldbClient(lldb::SBDebugger& debugger)
    : debugger_(debugger), interp_(debugger.GetCommandInterpreter())
{
}

std::string LldbClient::ExecuteCommand(const std::string& command)
{
    OutputCommand(command);

    lldb::SBCommandReturnObject result;
    interp_.HandleCommand(command.c_str(), result);

    std::string output;
    if (result.GetOutputSize() > 0)
        output = result.GetOutput();
    if (result.GetErrorSize() > 0)
    {
        if (!output.empty())
            output += "\n";
        output += result.GetError();
    }

    if (output.empty())
        output = "(No output)";
    else
        OutputCommandResult(output);

    return output;
}

void LldbClient::Output(const std::string& message)
{
    printf("%s", message.c_str());
    fflush(stdout);
}

void LldbClient::OutputError(const std::string& message)
{
    printf("%s[ERROR] %s%s\n", colors::RED, message.c_str(), colors::RESET);
    fflush(stdout);
}

void LldbClient::OutputWarning(const std::string& message)
{
    printf("%s[WARN] %s%s\n", colors::YELLOW, message.c_str(), colors::RESET);
    fflush(stdout);
}

void LldbClient::OutputCommand(const std::string& command)
{
    printf("%s$ %s%s\n", colors::CYAN, command.c_str(), colors::RESET);
    fflush(stdout);
}

void LldbClient::OutputCommandResult(const std::string& result)
{
    printf("%s%s%s\n", colors::DIM, result.c_str(), colors::RESET);
    fflush(stdout);
}

void LldbClient::OutputThinking(const std::string& message)
{
    printf("%s%s%s\n", colors::BLUE, message.c_str(), colors::RESET);
    fflush(stdout);
}

void LldbClient::OutputResponse(const std::string& response)
{
    printf("%s%s%s\n", colors::GREEN, response.c_str(), colors::RESET);
    fflush(stdout);
}

bool LldbClient::SupportsColor() const
{
    // Assume terminal supports color (could check isatty)
    return true;
}

std::string LldbClient::GetTargetName() const
{
    lldb::SBTarget target = debugger_.GetSelectedTarget();
    if (target.IsValid())
    {
        lldb::SBFileSpec exe = target.GetExecutable();
        if (exe.IsValid())
        {
            const char* filename = exe.GetFilename();
            if (filename)
                return filename;
        }
    }
    return "";
}

bool LldbClient::IsInterrupted() const
{
    // TODO: Check for Ctrl+C via signal handler or async interrupt
    return false;
}

} // namespace lldb_copilot
