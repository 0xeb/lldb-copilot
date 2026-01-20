#pragma once

#include <lldb/API/LLDB.h>
#include <string>

namespace lldb_copilot
{

// LLDB debugger client using the SB API
class LldbClient
{
  public:
    explicit LldbClient(lldb::SBDebugger& debugger);
    ~LldbClient() = default;

    // Execute LLDB command and return output
    std::string ExecuteCommand(const std::string& command);

    // Output methods for displaying messages to the user
    void Output(const std::string& message);
    void OutputError(const std::string& message);
    void OutputWarning(const std::string& message);

    // Styled output for agent interactions
    void OutputCommand(const std::string& command);
    void OutputCommandResult(const std::string& result);
    void OutputThinking(const std::string& message);
    void OutputResponse(const std::string& response);

    // Query capabilities
    bool SupportsColor() const;

    // Get target info (executable path)
    std::string GetTargetName() const;

    // Check if user requested interrupt
    bool IsInterrupted() const;

  private:
    lldb::SBDebugger& debugger_;
    lldb::SBCommandInterpreter interp_;
};

} // namespace lldb_copilot
