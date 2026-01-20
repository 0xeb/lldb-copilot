# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Linux
```bash
# Install LLDB development files (Ubuntu/Debian)
sudo apt install liblldb-dev lldb

# Build
cmake -B build
cmake --build build

# Output: build/lldb_copilot.so
```

### macOS
```bash
# Xcode includes LLDB
xcode-select --install

# Build
cmake -B build
cmake --build build

# Output: build/lldb_copilot.dylib
```

### Windows
```powershell
# Install LLVM (headers already in external/lldb-headers/)
winget install LLVM.LLVM

# Build using preset
cmake --preset windows
cmake --build --preset windows

# Output: build-windows/Release/lldb_copilot.dll
```

**Note**: The LLVM winget package does not include LLDB development headers.
The required headers are bundled in `external/lldb-headers/` for convenience.
If updating LLVM versions, regenerate `SBLanguages.h` from LLVM's Dwarf.def.

**Requirements**: CMake 3.20+, C++20 compiler, LLDB development headers

## Usage

```bash
# Start LLDB with a target
lldb ./myprogram
# Or with a core dump
lldb --core ./crash.core ./myprogram

# Load the plugin
(lldb) plugin load /path/to/lldb_copilot.so    # Linux
(lldb) plugin load /path/to/lldb_copilot.dylib # macOS
(lldb) plugin load C:\path\to\lldb_copilot.dll # Windows

# Ask Copilot questions
(lldb) copilot what is the call stack?
(lldb) copilot explain this crash
(lldb) copilot help me understand this

# Copilot settings via agent command
(lldb) agent help
(lldb) agent provider claude
(lldb) agent clear
```

## Architecture

This project uses `libagents` for AI provider logic. It is self-contained and does not depend on WinDbg-specific code.

### Dependency Graph

```
lldb_copilot.(so|dylib|dll)
    │
    ├── LldbClient (debugger adapter)
    │       │
    │       └── LLDB SB API (liblldb)
    │             ├── SBDebugger
    │             ├── SBCommandInterpreter
    │             └── SBCommandReturnObject
    │
    ├── lldb_commands (agent/ai commands)
    │       └── libagents::IAgent (hosted queries + tool dispatch)
    │
    └── SessionStore + Settings (local to lldb_copilot)
```

### Files

| File | Purpose |
|------|---------|
| `src/plugin.cpp` | LLDB plugin entry point (`lldb::PluginInitialize`) |
| `src/lldb_commands.cpp` | `copilot` and `agent` command implementations |
| `src/lldb_client.cpp` | LldbClient - debugger adapter using SB API |
| `src/settings.cpp` | Cross-platform settings (`~/.lldb_copilot/`) |
| `src/session_store.cpp` | Session persistence per target |
| `include/lldb_copilot/lldb_client.hpp` | LldbClient class definition |
| `include/lldb_copilot/system_prompt.hpp` | LLDB-specific system prompt |

## LLDB SB API Reference

### Command Execution

```cpp
lldb::SBCommandInterpreter interp = debugger.GetCommandInterpreter();
lldb::SBCommandReturnObject result;

interp.HandleCommand("bt", result);

// Get output
std::string output = result.GetOutput();   // stdout
std::string errors = result.GetError();    // stderr
bool ok = result.Succeeded();
```

### Custom Command Registration

```cpp
class MyCommand : public lldb::SBCommandPluginInterface {
    bool DoExecute(lldb::SBDebugger debugger,
                   char** command,           // null-terminated argv
                   lldb::SBCommandReturnObject& result) override
    {
        result.Printf("Hello from command\n");
        result.SetStatus(lldb::eReturnStatusSuccessFinishResult);
        return true;
    }
};

// Register
interp.AddCommand("mycommand", new MyCommand(), "Help text");
```

### Plugin Entry Point

```cpp
// Must be in the lldb namespace with exact signature
namespace lldb {
bool PluginInitialize(SBDebugger debugger);
}
```

### Useful SB Classes

| Class | Purpose |
|-------|---------|
| `SBDebugger` | Top-level debugger, manages targets |
| `SBTarget` | Debug target (executable + loaded modules) |
| `SBProcess` | Running/stopped process |
| `SBThread` | Thread within process |
| `SBFrame` | Stack frame |
| `SBCommandInterpreter` | Execute commands |
| `SBCommandReturnObject` | Command output/error/status |
| `SBFileSpec` | File path representation |

### Getting Target Info

```cpp
lldb::SBTarget target = debugger.GetSelectedTarget();
lldb::SBFileSpec exe = target.GetExecutable();
const char* name = exe.GetFilename();       // "myprogram"
const char* path = exe.GetDirectory();      // "/path/to"
```

## System Prompt (LLDB-specific)

The system prompt in `windbg_copilot/include/windbg_copilot/system_prompt.hpp` references WinDbg commands. For LLDB, create or override with:

```cpp
constexpr const char* kLldbSystemPrompt = R"(You are an expert debugging assistant integrated into LLDB.

You have access to the dbg_exec tool to execute any debugger command.

Common commands:
- bt, bt all              - Backtrace (current thread, all threads)
- frame select <n>        - Select stack frame
- frame variable          - Show local variables
- thread list             - List all threads
- thread select <n>       - Switch to thread
- register read           - Show registers
- register read <reg>     - Show specific register
- memory read <addr>      - Read memory (alias: x)
- memory read -fx <addr>  - Read as hex
- expression <expr>       - Evaluate expression (alias: p, expr)
- image list              - Loaded modules/libraries
- image lookup -a <addr>  - Symbol lookup by address
- image lookup -n <name>  - Symbol lookup by name
- process status          - Process state
- target variable         - Global variables
- disassemble -pc         - Disassemble at PC
- breakpoint list         - List breakpoints
- watchpoint list         - List watchpoints

For crash analysis:
- bt                      - Get the crash stack
- frame variable          - Check locals at crash site
- register read           - Check register state
- memory read $pc         - Examine code at crash
- image lookup -a $pc     - Get symbol info

Be concise. Show your reasoning. When you find the issue, explain it clearly.)";
```

## Testing

### Run the test suite

```bash
cd lldb_copilot
pytest tests/ -v
```

### Manual testing

```bash
# Start LLDB with the test program
lldb ./build/crash_test

# Load the plugin
(lldb) plugin load ./build/lldb_copilot.so

# Verify commands work
(lldb) agent help
(lldb) agent version

# Run the program
(lldb) run

# Ask Copilot questions
(lldb) copilot what function am I in?
(lldb) copilot show me the local variables

# Test with a crash
(lldb) run crash
(lldb) copilot explain this crash
```

## Remaining TODO Items

1. **Interrupt handling** - Implement `IsInterrupted()` using signal handler or async check
2. **Color detection** - Check `isatty(stdout)` before using ANSI codes
3. **Error handling** - Handle cases where LLDB APIs return invalid objects

## Common Issues

### LLDB Headers Not Found
```bash
# Ubuntu/Debian
sudo apt install liblldb-dev

# Check installed version
lldb --version
# Find headers
find /usr -name "LLDB.h" 2>/dev/null
```

### Undefined Symbols on macOS
The `-undefined dynamic_lookup` flag in CMakeLists.txt defers symbol resolution to load time. This is correct for LLDB plugins.

### Plugin Not Loading
```
(lldb) plugin load ./lldb_copilot.so
error: unable to load plugin
```
Check:
- File exists and is readable
- Built for correct architecture
- All dependencies available (`ldd ./lldb_copilot.so`)

### Windows: python310.dll Not Found
```
The code execution cannot proceed because python310.dll was not found.
```
LLDB on Windows requires Python 3.10. The winget LLVM package doesn't bundle it.

**Fix:** Download embedded Python 3.10 to the project `bin` folder:
```powershell
# Download and extract embedded Python 3.10 to project bin folder
Invoke-WebRequest -Uri "https://www.python.org/ftp/python/3.10.11/python-3.10.11-embed-amd64.zip" -OutFile "$env:TEMP\python310.zip"
Expand-Archive -Path "$env:TEMP\python310.zip" -DestinationPath "..\bin\python310" -Force
```

Then use the `test_lldb.cmd` wrapper script which auto-configures the environment.

### Windows: Module 'uuid' Not Found
```
ModuleNotFoundError: No module named 'uuid'
```
PYTHONHOME and PYTHONPATH must point to the embedded Python directory.
The `test_lldb.cmd` script handles this automatically.

### Windows: LLDB Headers Not Found
The winget LLVM package doesn't include LLDB development headers.
Headers are bundled in `external/lldb-headers/` for this project.

If updating LLVM versions, download headers from LLVM source:
```powershell
# Download LLVM source for your version
# Extract lldb/include/lldb/* to external/lldb-headers/include/lldb/
# Generate SBLanguages.h from llvm/include/llvm/BinaryFormat/Dwarf.def
```

## Development Workflow

```bash
# Edit code
vim src/lldb_client.cpp

# Rebuild
cd build && cmake --build .

# Test in LLDB
lldb ./test_program
(lldb) plugin load ./lldb_copilot.so
(lldb) ai what is happening?

# Debug the plugin itself
LLDB_DEBUGGER_LOG_MASK=all lldb ...
```

## File Tree

```
lldb_copilot/
├── CLAUDE.md              # This file
├── README.md              # User documentation
├── CMakeLists.txt         # Build config, links liblldb + libagents
├── CMakePresets.json      # Windows preset
├── include/
│   └── lldb_copilot/
│       ├── lldb_client.hpp
│       └── system_prompt.hpp  # LLDB-specific system prompt
├── src/
│   ├── lldb_client.cpp    # IDebuggerClient using SB API
│   ├── lldb_commands.cpp  # "ai" and "agent" command handlers
│   ├── plugin.cpp         # lldb::PluginInitialize entry point
│   ├── settings.cpp       # Cross-platform settings (~/.lldb_copilot/)
│   ├── session_store.cpp  # Session persistence per target
│   └── lldb_copilot.def   # Windows export definition
└── external/
    └── lldb-headers/      # Bundled LLDB headers for Windows
```

## External Dependencies

Located in `../windbg_copilot/external/`:
- **claude-agent-sdk-cpp** - C++ SDK for Claude CLI
- **copilot-sdk-cpp** - C++ SDK for GitHub Copilot
- **fastmcpp** - MCP server library (used by claude SDK)
