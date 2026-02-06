cmake_minimum_required(VERSION 3.15)
project(EmbeddedPythonLoop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force /MD (MultiThreadedDLL) for all configs: Debug and Release
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# ---------------------------------------------------------
# Python discovery (always use release libs)
# ---------------------------------------------------------
set(Python3_USE_DEBUG_LIBS FALSE)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

add_executable(EmbeddedPythonLoop main.cpp)

# ---------------------------------------------------------
# Platform + compiler specific debug macro handling
# ---------------------------------------------------------
if (WIN32)
    # Case 1: Windows + MSVC OR GNU GCC (MinGW/MSYS2/UCRT64) → NEVER use _DEBUG
    if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC|GNU|MSYS|MINGW")
        target_compile_definitions(EmbeddedPythonLoop PRIVATE
            $<$<CONFIG:Debug>:APP_DEBUG>
        )
    # Case 2: Windows + clang-cl → MSVC-compatible Clang → NEVER use _DEBUG
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"
    )
        target_compile_definitions(EmbeddedPythonLoop PRIVATE
            $<$<CONFIG:Debug>:APP_DEBUG>
        )
        target_compile_options(EmbeddedPythonLoop PRIVATE
            $<$<CONFIG:Debug>:-U_DEBUG>
        )
        target_link_options(EmbeddedPythonLoop PRIVATE
            $<$<CONFIG:Debug>:-U_DEBUG>
        )
    endif()
# Case 4: Linux + macOS → _DEBUG is SAFE
else()
    target_compile_definitions(EmbeddedPythonLoop PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Debug>:APP_DEBUG=_DEBUG>
    )
endif()

# ---------------------------------------------------------
# Include Python headers and project root
# ---------------------------------------------------------
target_include_directories(EmbeddedPythonLoop PRIVATE
    ${Python3_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../python_locator   # so python_locator.hpp is found
)

# ---------------------------------------------------------
# Link Python library for ALL compilers and ALL configs
# MSVC: prevents pythonXY_d.lib
# Clang/GCC: treated as a normal link
# ---------------------------------------------------------
target_link_libraries(EmbeddedPythonLoop PRIVATE
    optimized "${Python3_LIBRARIES}"
    debug     "${Python3_LIBRARIES}"
)

# ---------------------------------------------------------
# MSVC: absolutely forbid linking pythonXY_d.lib
# ---------------------------------------------------------
if (MSVC)
    target_link_options(EmbeddedPythonLoop PRIVATE
        $<$<CONFIG:Debug>:/NODEFAULTLIB:python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}_d.lib>
    )
endif()


# Silence Clang warning about __int64 in Python headers
if (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(EmbeddedPythonLoop PRIVATE
        -Wno-language-extension-token
    )
endif()


# ---------------------------------------------------------
# Warnings
# ---------------------------------------------------------
if (MSVC)
    target_compile_options(EmbeddedPythonLoop PRIVATE /W4)
else()
    target_compile_options(EmbeddedPythonLoop PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------
# Copy python DLL next to the executable (Windows only)
# ---------------------------------------------------------
if (WIN32)
    add_custom_command(TARGET EmbeddedPythonLoop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${Python3_RUNTIME_LIBRARY_DIRS}/python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}.dll"
        $<TARGET_FILE_DIR:EmbeddedPythonLoop>
    )
endif()

# ---------------------------------------------------------
# Copy scripts folder next to the executable (Windows only)
# ---------------------------------------------------------
if (WIN32)
    add_custom_command(TARGET EmbeddedPythonLoop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
            "$<TARGET_FILE_DIR:EmbeddedPythonLoop>/scripts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/python_required_version.txt"
            "$<TARGET_FILE_DIR:EmbeddedPythonLoop>/python_required_version.txt"
    )
endif()

