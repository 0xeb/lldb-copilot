# LLDB Copilot

Copilot-powered debugging assistant for LLDB. Ask questions about your debug session and get intelligent analysis.

## Requirements

- LLDB 16+ with development headers
- CMake 3.20+
- C++20 compiler
- Claude Code or GitHub Copilot configured

### Platform-specific requirements

**Linux:**
```bash
sudo apt install liblldb-dev lldb
```

**macOS:**
```bash
xcode-select --install
```

**Windows:**
```powershell
winget install LLVM.LLVM
```
Note: The winget package requires Python 3.10 at runtime (see [Windows Setup](#windows-setup) below).

## Build

**Linux/macOS:**
```bash
cmake --preset default
cmake --build --preset default
```

**Windows:**
```cmd
cmake --preset windows
cmake --build --preset windows
```

Output:
- Linux: `build/lldb_copilot.so`
- macOS: `build/lldb_copilot.dylib`
- Windows: `build-windows/Release/lldb_copilot.dll`

## Usage

```bash
# Start LLDB with your program
lldb ./myprogram

# Load the plugin
(lldb) plugin load /path/to/lldb_copilot.so

# Ask questions
(lldb) copilot what is the call stack?
(lldb) copilot explain this crash

# Run commands directly - AI executes and explains
(lldb) copilot bt all
(lldb) copilot register read

# Decompilation
(lldb) copilot decompile main

# Provider and settings
(lldb) agent help
(lldb) agent provider claude
(lldb) agent clear
```

## Features

- **Direct command execution**: Pass commands directly (`copilot bt`) - AI runs and explains
- **Expression evaluation**: Uses `p`, `expression` for calculations instead of guessing
- **Decompilation**: Ask to decompile functions - AI uses disassemble, frame variable, type info
- **Automatic tool execution**: AI runs debugger commands to gather information
- **Conversation continuity**: Follow-up questions remember context
- **Multiple providers**: Switch between Claude and Copilot

## Testing

### Run the test suite

```bash
cd lldb_copilot
pytest tests/ -v
```

**Windows:**
```cmd
cd lldb_copilot
pytest tests/ -v
```

### Manual testing

**Linux/macOS:**
```bash
lldb ./build/crash_test
(lldb) plugin load ./build/lldb_copilot.so
```

**Windows:** (use `test_lldb.cmd` which sets up Python and loads the plugin)
```cmd
test_lldb.cmd
(lldb) target create build-windows\Release\crash_test.exe
```

**Common commands:**
```
(lldb) agent help
(lldb) agent version
(lldb) run
(lldb) copilot what function am I in?
(lldb) copilot show me the local variables

# Test with a crash
(lldb) run crash
(lldb) copilot explain this crash
```

## Commands

| Command | Description |
|---------|-------------|
| `copilot <question>` | Ask Copilot a question |
| `agent help` | Show help |
| `agent version` | Show version and current provider |
| `agent provider` | Show current provider |
| `agent provider <name>` | Switch provider (claude, copilot) |
| `agent clear` | Clear conversation history |
| `agent prompt` | Show custom prompt |
| `agent prompt <text>` | Set custom prompt |
| `agent prompt clear` | Clear custom prompt |

## Providers

- **Claude** - Uses Claude Code CLI
- **Copilot** - Uses GitHub Copilot

Switch providers with:
```
(lldb) agent provider claude
(lldb) agent provider copilot
```

## Configuration

Settings are stored in `~/.lldb_copilot/settings.json`:

```json
{
  "default_provider": "copilot",
  "custom_prompt": ""
}
```

## Windows Setup

The LLVM package from winget requires Python 3.10 to run LLDB. Download and extract to the project `bin` folder:

```powershell
# Download and extract embedded Python 3.10 to project bin folder
Invoke-WebRequest -Uri "https://www.python.org/ftp/python/3.10.11/python-3.10.11-embed-amd64.zip" -OutFile "$env:TEMP\python310.zip"
Expand-Archive -Path "$env:TEMP\python310.zip" -DestinationPath "..\bin\python310" -Force
```

Then use the `test_lldb.cmd` wrapper script which sets up the environment:

```cmd
cd lldb_copilot
test_lldb.cmd
```

### Quick Test

```cmd
test_lldb.cmd
(lldb) target create "C:\Windows\System32\notepad.exe"
(lldb) run
```

Press Ctrl+C to break, then:
```
(lldb) bt
(lldb) copilot what is the call stack?
```
