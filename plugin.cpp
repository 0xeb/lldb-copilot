#include <lldb/API/LLDB.h>
#include <lldb/API/SBDebugger.h>

namespace lldb_copilot
{
void RegisterCommands(lldb::SBDebugger& debugger);
}

// LLDB plugin entry point
// Called when plugin is loaded via: plugin load /path/to/lldb_copilot.so
//
// LLDB looks for the symbol with Itanium C++ mangling:
//   _ZN4lldb16PluginInitializeENS_10SBDebuggerE
//
// On Linux/macOS with GCC/Clang, this happens naturally.
// On Windows with MSVC, we use lldb_copilot.def to export with this name.

namespace lldb
{

#ifdef _WIN32
// On Windows, export with C linkage so .def file can alias it
// The .def file maps: _ZN4lldb16PluginInitializeENS_10SBDebuggerE -> lldb_PluginInitialize
extern "C" __declspec(dllexport) bool lldb_PluginInitialize(lldb::SBDebugger debugger)
{
    lldb_copilot::RegisterCommands(debugger);
    return true;
}
#else
// On Unix, standard C++ mangling produces the expected symbol
bool PluginInitialize(SBDebugger debugger)
{
    lldb_copilot::RegisterCommands(debugger);
    return true;
}
#endif

} // namespace lldb
