cmake_minimum_required(VERSION 3.20)
project(lldb_copilot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add libagents if building standalone (not part of monorepo)
if(NOT TARGET libagents)
    add_subdirectory(external/libagents)
endif()

# Find LLDB
# On macOS: /Applications/Xcode.app/Contents/SharedFrameworks/LLDB.framework
# On Linux: apt install liblldb-dev
find_package(LLDB QUIET)

if(NOT LLDB_FOUND)
    # Try to find LLDB manually based on platform
    if(WIN32)
        # Windows: LLVM typically installed via installer or scoop/choco
        # Check local headers first (winget installs don't include headers)
        # Then check LLVM_DIR environment variable, then standard locations
        find_path(LLDB_INCLUDE_DIRS lldb/API/LLDB.h
            PATHS
                "${CMAKE_CURRENT_SOURCE_DIR}/external/lldb-headers/include"
                "$ENV{LLVM_DIR}/include"
                "$ENV{ProgramFiles}/LLVM/include"
                "$ENV{ProgramW6432}/LLVM/include"
                "C:/Program Files/LLVM/include"
            NO_DEFAULT_PATH
        )
        find_library(LLDB_LIBRARIES NAMES liblldb lldb
            PATHS
                "$ENV{LLVM_DIR}/lib"
                "$ENV{ProgramFiles}/LLVM/lib"
                "$ENV{ProgramW6432}/LLVM/lib"
                "C:/Program Files/LLVM/lib"
            NO_DEFAULT_PATH
        )
        if(LLDB_INCLUDE_DIRS AND LLDB_LIBRARIES)
            set(LLDB_FOUND TRUE)
        endif()
    elseif(APPLE)
        set(LLDB_FRAMEWORK "/Applications/Xcode.app/Contents/SharedFrameworks/LLDB.framework")
        if(EXISTS ${LLDB_FRAMEWORK})
            set(LLDB_INCLUDE_DIRS "${LLDB_FRAMEWORK}/Headers")
            set(LLDB_LIBRARIES "${LLDB_FRAMEWORK}/LLDB")
            set(LLDB_FOUND TRUE)
        endif()
    else()
        # Linux: Search in versioned LLVM directories
        find_path(LLDB_INCLUDE_DIRS lldb/API/LLDB.h
            PATHS
                "$ENV{LLVM_DIR}/include"
                /usr/lib/llvm-18/include
                /usr/lib/llvm-17/include
                /usr/lib/llvm-16/include
                /usr/include
                /usr/local/include
        )
        find_library(LLDB_LIBRARIES NAMES lldb lldb-18 lldb-17 lldb-16
            PATHS
                "$ENV{LLVM_DIR}/lib"
                /usr/lib/x86_64-linux-gnu
                /usr/lib
                /usr/local/lib
        )
        if(LLDB_INCLUDE_DIRS AND LLDB_LIBRARIES)
            set(LLDB_FOUND TRUE)
        endif()
    endif()
endif()

if(NOT LLDB_FOUND)
    if(WIN32)
        message(FATAL_ERROR "LLDB not found. Install LLVM from https://releases.llvm.org/ or set LLVM_DIR environment variable")
    elseif(APPLE)
        message(FATAL_ERROR "LLDB not found. Install Xcode or Xcode Command Line Tools")
    else()
        message(FATAL_ERROR "LLDB not found. Install liblldb-dev (apt) or set LLVM_DIR environment variable")
    endif()
endif()

message(STATUS "LLDB include: ${LLDB_INCLUDE_DIRS}")
message(STATUS "LLDB library: ${LLDB_LIBRARIES}")

# Enable position-independent code for all targets (required for shared library)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# LLDB Copilot shared library (plugin)
add_library(lldb_copilot SHARED
    lldb_client.cpp
    lldb_commands.cpp
    plugin.cpp
    settings.cpp
    session_store.cpp
)

target_include_directories(lldb_copilot
    PRIVATE
        ${LLDB_INCLUDE_DIRS}
)

target_link_libraries(lldb_copilot
    PRIVATE
        ${LLDB_LIBRARIES}
        libagents         # Unified provider library
)

# On macOS, need to handle framework properly
if(APPLE)
    target_link_options(lldb_copilot PRIVATE -undefined dynamic_lookup)
endif()

# On Windows, export symbol with Itanium-mangled name that LLDB expects
if(WIN32)
    target_sources(lldb_copilot PRIVATE lldb_copilot.def)
endif()

set_target_properties(lldb_copilot PROPERTIES
    PREFIX ""
    OUTPUT_NAME "lldb_copilot"
)
