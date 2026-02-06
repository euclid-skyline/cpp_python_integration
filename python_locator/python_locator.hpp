#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <regex>

#include "python_version_policy.hpp"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace pyembed
{
    enum class PythonSource
    {
        SYSTEM = 1,
        BUNDLED = 2,
        ZIP = 3
    };

    struct PythonLocation
    {
        std::filesystem::path home;                 // Python root directory
        std::filesystem::path lib;                  // Lib/ or pythonXY.zip
        PythonSource source = PythonSource::SYSTEM; // SYSTEM, BUNDLED, ZIP
    };

    // ------------------------------------------------------------
    // Type alias for version structure
    // ------------------------------------------------------------
    using Version = PythonVersionPolicy::Version;

    // ------------------------------------------------------------
    // Extract version from a string using flexible patterns
    // ------------------------------------------------------------
    inline Version parse_version_from_string(const std::string &s)
    {
        //  Matches :
        // python311, python3.11, python3_11, python3.11.2,
        // 3.11, 3.11.4, python314 → 3.14.0
        //
        // Enforces:
        // major = 1 digit
        // minor = 1–2 digits
        // patch = 1–2 digits
        std::regex re(R"((?:python)?(\d)(?:[\._]?(\d{1,2}))?(?:[\._]?(\d{1,2}))?)");
        std::smatch m;

        if (!std::regex_search(s, m, re))
            return {};

        Version v;
        v.major = std::stoi(m[1]);
        v.minor = m[2].matched ? std::stoi(m[2]) : 0;
        v.patch = m[3].matched ? std::stoi(m[3]) : 0;
        v.valid = true;

        std::cout << "Parsed version from string '" << s << "': " << v.to_string() << std::endl;

        return v;
    }

    // ------------------------------------------------------------
    // Try VERSION file (Linux)
    // ------------------------------------------------------------
    inline Version detect_from_VERSION_file(const std::filesystem::path &lib)
    {
        auto versionFile = lib / "VERSION";
        if (!std::filesystem::exists(versionFile))
            return {};

        std::ifstream f(versionFile);
        if (!f)
            return {};

        std::string content;
        std::getline(f, content);

        return parse_version_from_string(content);
    }

    // ------------------------------------------------------------
    // Try sysconfigdata (bundled Python)
    // ------------------------------------------------------------
    inline Version detect_from_sysconfigdata(const std::filesystem::path &lib)
    {
        for (auto &entry : std::filesystem::directory_iterator(lib))
        {
            auto name = entry.path().filename().string();
            if (name.find("sysconfigdata") == std::string::npos)
                continue;

            std::ifstream f(entry.path());
            if (!f)
                continue;

            std::string content((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());

            // Look for cpython-311, cpython-312, etc.
            std::regex re(R"(cpython-(\d)(\d+))");
            std::smatch m;

            if (std::regex_search(content, m, re))
            {
                Version v;
                v.major = 3;
                v.minor = std::stoi(m[2]);
                v.patch = 0;
                v.valid = true;
                return v;
            }
        }

        return {};
    }

    // ------------------------------------------------------------
    // Try directory name (system + bundled)
    // ------------------------------------------------------------
    inline Version detect_from_directory_name(const std::filesystem::path &lib)
    {
        // Examples:
        // python3.11
        // python311
        // Python311
        // /usr/lib/python3.12
        auto name = lib.filename().string();
        return parse_version_from_string(name);
    }

    // ------------------------------------------------------------
    // Try parent directory name (Windows system Python)
    // ------------------------------------------------------------
    inline Version detect_from_parent_directory(const std::filesystem::path &lib)
    {
        auto parent = lib.parent_path().filename().string();
        std::cout << "Detecting from parent directory: " << parent << "\n";
        return parse_version_from_string(parent);
    }

    // ------------------------------------------------------------
    // Try executable name (Linux/macOS)
    // ------------------------------------------------------------
    inline Version detect_from_executable_name(const PythonLocation &loc)
    {
        auto exeName = loc.home.filename().string();
        std::cout << "Detecting from executable name: " << exeName << "\n";
        return parse_version_from_string(exeName);
    }

    // ------------------------------------------------------------
    // Unified function for two function above detect_from_parent_directory()
    // and detect_from_executable_name().
    // It is kept for possible future use and replace them both.
    // ------------------------------------------------------------
    // inline Version detect_from_names(const PythonLocation &loc)
    // {
    //     // 1. Executable name (Linux/macOS)
    //     auto exeName = loc.home.filename().string();
    //     auto v = parse_version_from_string(exeName);
    //     if (v.valid)
    //         return v;

    //     // 2. Directory name (Linux system Python)
    //     auto dirName = loc.lib.filename().string();
    //     v = parse_version_from_string(dirName);
    //     if (v.valid)
    //         return v;

    //     // 3. Parent directory name (Windows system Python)
    //     auto parent = loc.lib.parent_path().filename().string();
    //     v = parse_version_from_string(parent);
    //     if (v.valid)
    //         return v;

    //     return {};
    // }

    // ------------------------------------------------------------
    // ZIP Python version detection (filename only)
    // ------------------------------------------------------------
    inline Version detect_from_zip_filename(const std::filesystem::path &zip)
    {
        return parse_version_from_string(zip.filename().string());
    }

    // ------------------------------------------------------------
    // Execute Python to get exact version (SYSTEM Python only)
    // ------------------------------------------------------------
    inline Version detect_from_executing_python(const std::filesystem::path &exe)
    {

        if (!std::filesystem::exists(exe))
            return {};

        // Build safe command
#if defined(_WIN32)
        // Correct Windows command should be enclosed with double quoting.
        // Since the command begins with a quoted path containing spaces cmd.exe splits it incorrectly.
        // So we add an extra pair of quotes at the beginning and the end.
        // Outer quotes wrap the entire command for cmd.exe.
        // Inner quotes wrap the python executable path.
        std::string cmd = "cmd /c \"\"";
        cmd += exe.string();
        cmd += "\" -c \"import sys; print(sys.version_info[0], sys.version_info[1], sys.version_info[2])\"\"";
        // std::cout << "Executing command: " << cmd << std::endl;
        FILE *pipe = _popen(cmd.c_str(), "r");
#else
        std::string cmd = exe.string() +
                          " -c \"import sys; print(sys.version_info[0], sys.version_info[1], sys.version_info[2])\"";
        FILE *pipe = popen(cmd.c_str(), "r");
#endif

        if (!pipe)
            return {};

        char buffer[128] = {};
        std::string output;

        if (fgets(buffer, sizeof(buffer), pipe))
            output = buffer;

#if defined(_WIN32)
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        // Trim whitespace
        output.erase(std::remove_if(output.begin(), output.end(),
                                    [](unsigned char c)
                                    { return std::isspace(c); }),
                     output.end());

        // Expected: "3 14 2" → convert to "3.14.2"
        // for (char &c : output)
        //     if (c == ' ')
        //         c = '.';

        // std::cout << "Executed Python version output: " << output << std::endl;

        return parse_version_from_string(output);
    }

    // ------------------------------------------------------------
    // Main version detector (robust, cross-platform)
    // ------------------------------------------------------------
    inline Version detect_python_version(const PythonLocation &loc)
    {
        switch (loc.source)
        {
        case PythonSource::ZIP:
            return detect_from_zip_filename(loc.lib);

        case PythonSource::BUNDLED:
        case PythonSource::SYSTEM:
        {
            // 1. Try executing python (most accurate)
            auto exe = loc.home / "python.exe";    // Windows
            auto exe3 = loc.home / "python3";      // Linux/macOS
            auto exeGeneric = loc.home / "python"; // Linux/macOS fallback
            Version v;

            std::cout << "\nDetecting Python version for location:\n"
                      << " Loc.Home: " << loc.home << "\n"
                      << " Loc.Lib:  " << loc.lib << "\n"
                      << " Loc.exe:  " << exe << "\n"
                      << " Loc.exe3: " << exe3 << "\n"
                      << " Loc.exeG: " << exeGeneric << "\n";

            if (std::filesystem::exists(exe))
            {
                v = detect_from_executing_python(exe);
                v.patch_strict = true; // exact match required, Don't ignore patch version
                std::cout << "Detected version from executing python.exe: " << v.to_string() << "\n";
                if (v.valid)
                    return v;
            }
            if (std::filesystem::exists(exe3))
            {
                v = detect_from_executing_python(exe3);
                v.patch_strict = true; // exact match required, Don't ignore patch version
                std::cout << "Detected version from executing python.exe: " << v.to_string() << "\n";
                if (v.valid)
                    return v;
            }
            if (std::filesystem::exists(exeGeneric))
            {
                v = detect_from_executing_python(exeGeneric);
                v.patch_strict = true; // exact match required, Don't ignore patch version
                std::cout << "Detected version from executing python.exe: " << v.to_string() << "\n";
                if (v.valid)
                    return v;
            }
            std::cout << "1. Executing python detection failed.\n";
            // All other methods below are heuristics and may be inaccurate. Use with caution.
            // They are tried only if executing python failed. 
            // Also they do not enforce patch_strict.
            // 2. VERSION file (Linux)
            v = detect_from_VERSION_file(loc.lib);
            if (v.valid)
                return v;
            std::cout << "2. VERSION file detection failed.\n";
            // 3. sysconfigdata (bundled)
            v = detect_from_sysconfigdata(loc.lib);
            if (v.valid)
                return v;
            std::cout << "3. sysconfigdata detection failed.\n";
            // 4. directory name
            v = detect_from_directory_name(loc.lib);
            if (v.valid)
                return v;
            std::cout << "4. Directory name detection failed.\n";
            // 5. parent directory name (Windows)
            v = detect_from_parent_directory(loc.lib);
            if (v.valid)
                return v;
            std::cout << "5. Parent directory name detection failed.\n";
            // 6. executable name (Linux/macOS)
            v = detect_from_executable_name(loc);
            if (v.valid)
                return v;
            std::cout << "6. Executable name detection failed.\n";
            return {};
        }

        default:
            return {};
        }
    }

    // ------------------------------------------------------------
    // Version acceptance logic (selection)
    // ------------------------------------------------------------
    inline bool python_version_is_acceptable(
        const PythonLocation &loc,
        const PythonVersionPolicy &policy)
    {
        auto v = detect_python_version(loc);
        return policy.in_range(v);
    }

    // -----------------------------------------------------------------------------
    // Lightweight ZIP stdlib validator
    // Scans the ZIP file for essential stdlib components without full ZIP parsing.
    // -----------------------------------------------------------------------------
    inline bool zip_contains_minimal_stdlib(const std::filesystem::path &zipPath)
    {
        std::ifstream f(zipPath, std::ios::binary);
        if (!f)
            return false;

        // Read entire ZIP into memory (fast enough for stdlib ZIP sizes)
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());

        // Check for essential stdlib components
        return content.find("encodings/") != std::string::npos &&
               content.find("os.py") != std::string::npos &&
               content.find("importlib/") != std::string::npos;
    }

    // -----------------------------------------------------------------------------
    // Python location validator
    // Strict for ZIP + bundled, minimal for system Python.
    // -----------------------------------------------------------------------------
    inline bool validate_python_location(const PythonLocation &loc)
    {
        switch (loc.source)
        {
        case PythonSource::ZIP:
        {
            // Check ZIP file exist (loc.lib is the .zip file here. It is not a directory)
            if (!std::filesystem::exists(loc.lib))
                return false;

            // Lightweight ZIP signature check
            std::ifstream f(loc.lib, std::ios::binary);
            if (!f)
                return false;

            unsigned char sig[4] = {0};
            f.read(reinterpret_cast<char *>(sig), 4);

            // Valid ZIP signatures:
            // PK\x03\x04 → local file header
            // PK\x05\x06 → end of central directory
            // PK\x07\x08 → spanning signature (rare)
            bool sig_ok =
                sig[0] == 'P' && sig[1] == 'K' &&
                (sig[2] == 3 || sig[2] == 5 || sig[2] == 7);

            if (!sig_ok)
                return false;

            // Minimal stdlib presence check
            return zip_contains_minimal_stdlib(loc.lib);
        }

        case PythonSource::BUNDLED:
        {
            auto enc = loc.lib / "encodings";
            auto os_py = loc.lib / "os.py";
            auto importlib_dir = loc.lib / "importlib";

            return std::filesystem::exists(enc) &&
                   !std::filesystem::is_empty(enc) &&
                   std::filesystem::exists(os_py) &&
                   std::filesystem::exists(importlib_dir);
        }

        case PythonSource::SYSTEM:
        {
            // Minimal check only — system Python is assumed valid
            return std::filesystem::exists(loc.lib / "encodings") &&
                   !std::filesystem::is_empty(loc.lib / "encodings");
        }

        default:
        {
            return false;
        }
        }
    }
    // ---------------------------------------------------------
    // Get executable directory (cross-platform)
    // ---------------------------------------------------------
    inline std::filesystem::path get_executable_dir()
    {
#if defined(_WIN32)
        wchar_t buffer[MAX_PATH];
        GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        return std::filesystem::path(buffer).parent_path();

#elif defined(__APPLE__)
        char buffer[1024];
        uint32_t size = sizeof(buffer);
        _NSGetExecutablePath(buffer, &size);
        return std::filesystem::path(buffer).parent_path();

#else
        char buffer[1024];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        buffer[len] = '\0';
        return std::filesystem::path(buffer).parent_path();
#endif
    }

    // ---------------------------------------------------------
    // Try to locate system Python (very lightweight detection)
    // ---------------------------------------------------------
    inline std::optional<PythonLocation> find_system_python(const PythonVersionPolicy &policy)
    {
        std::vector<std::filesystem::path> candidates;

        // ---------------------------------------------------------
        // 1. Collect python executable paths
        // ---------------------------------------------------------
#if defined(_WIN32)
        FILE *pipe = _popen("where python", "r");
#else
        FILE *pipe = popen("which -a python3 python", "r");
#endif

        if (!pipe)
            return std::nullopt;

        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe))
        {
            std::string line(buffer);
            line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());
            candidates.emplace_back(line);
        }

#if defined(_WIN32)
        _pclose(pipe);
#else
        pclose(pipe);
#endif

        // ---------------------------------------------------------
        // 2. Validate each candidate
        // ---------------------------------------------------------
        for (auto &exePath : candidates)
        {
            std::string p = exePath.string();

            // ------------------------------
            // WINDOWS FILTERS
            // ------------------------------
#if defined(_WIN32)
            // Skip Microsoft Store shim
            if (p.find("WindowsApps") != std::string::npos)
                continue;

            // Skip MSYS2 Python
            if (p.find("msys64") != std::string::npos)
                continue;
#endif

            // ------------------------------
            // Compute Python home directory
            // ------------------------------
            auto home = exePath.parent_path();
            auto lib = home / "Lib";

            // ------------------------------
            // macOS framework Python
            // ------------------------------
#if defined(__APPLE__)
            // Example:
            // /Library/Frameworks/Python.framework/Versions/3.11/bin/python3
            if (home.filename() == "bin")
            {
                auto versions = home.parent_path(); // Versions/3.11
                auto libCandidate = versions / "Lib";
                PythonLocation loc{versions, libCandidate, PythonSource::SYSTEM};
                if (validate_python_location(loc))
                {
                    if (!python_version_is_acceptable(loc, policy))
                        continue;
                    return loc;
                }
            }
#endif

            // ------------------------------
            // Linux standard library locations
            // ------------------------------
#if defined(__linux__)
            if (!std::filesystem::exists(lib))
            {
                // Try /usr/lib/python3.x
                for (auto &entry : std::filesystem::directory_iterator("/usr/lib"))
                {
                    if (entry.path().filename().string().find("python3") == 0)
                    {
                        auto libCandidate = entry.path();
                        PythonLocation loc{home, libCandidate, PythonSource::SYSTEM};
                        if (validate_python_location(loc))
                        {
                            if (!python_version_is_acceptable(loc, policy))
                                continue;
                            return loc;
                        }
                    }

                    // Try /usr/local/lib/python3.x
                    for (auto &entry : std::filesystem::directory_iterator("/usr/local/lib"))
                    {
                        if (entry.path().filename().string().find("python3") == 0)
                        {
                            auto libCandidate = entry.path();
                            PythonLocation loc{home, libCandidate, PythonSource::SYSTEM};
                            if (validate_python_location(loc))
                            {
                                if (!python_version_is_acceptable(loc, policy))
                                    continue;
                                return loc;
                            }
                        }
                    }
                }
            }
#endif
            // ------------------------------
            // Validate standard library
            // ------------------------------
            {
                PythonLocation loc{home, lib, PythonSource::SYSTEM};
                if (validate_python_location(loc))
                {
                    // std::cout << "Python Policy Version Acceptance check: "
                    //           << python_version_is_acceptable(loc, policy) << std::endl;
                    if (!python_version_is_acceptable(loc, policy))
                        continue;
                    return loc;
                }
            }
        }

        return std::nullopt;
    }

    // ---------------------------------------------------------
    // Try to locate bundled Python next to the executable
    // ---------------------------------------------------------
    inline std::optional<PythonLocation> find_bundled_python(const PythonVersionPolicy &policy)
    {
        auto exeDir = get_executable_dir();
        auto pyDir = exeDir / "python";
        // Check if 'python' directory exists first
        if (std::filesystem::exists(pyDir))
        {
            auto lib = pyDir / "Lib";

            // Scoped block for keep variables loc local and it can be reused in the ZIP check
            {
                PythonLocation loc{pyDir, lib, PythonSource::BUNDLED};
                if (validate_python_location(loc))
                {
                    // std::cout << "Python Policy Version Acceptance check: "
                    //           << python_version_is_acceptable(loc, policy) << std::endl;
                    if (!python_version_is_acceptable(loc, policy))
                        return std::nullopt;
                    return loc;
                }
            }
            // Check the ZIP-based Python inside the python folder (only if it contains encodings)
            for (auto &entry : std::filesystem::directory_iterator(pyDir))
            {
                if (entry.path().extension() == ".zip" &&
                    entry.path().filename().string().find("python") != std::string::npos)
                {
                    PythonLocation loc{pyDir, entry.path(), PythonSource::ZIP};
                    if (validate_python_location(loc))
                    {
                        // std::cout << "Python Policy Version Acceptance check: "
                        //           << python_version_is_acceptable(loc, policy) << std::endl;
                        if (!python_version_is_acceptable(loc, policy))
                            continue;
                        return loc;
                    }
                }
            }
        }

        return std::nullopt;
    }

    // ---------------------------------------------------------
    // Try to locate ZIP-based Python next to executable
    // ---------------------------------------------------------
    inline std::optional<PythonLocation> find_zip_python(const PythonVersionPolicy &policy)
    {
        auto exeDir = get_executable_dir();

        for (auto &entry : std::filesystem::directory_iterator(exeDir))
        {
            if (entry.path().extension() == ".zip" &&
                entry.path().filename().string().find("python") != std::string::npos)
            {
                PythonLocation loc{exeDir, entry.path(), PythonSource::ZIP};
                // If Python location is valid then check version acceptance
                // Don't check version if location is invalid
                if (validate_python_location(loc))
                {
                    if (!python_version_is_acceptable(loc, policy))
                        continue;
                    return loc;
                }
            }
        }

        return std::nullopt;
    }

    inline void print_python_status(const PythonLocation &py)
    {
        std::cout << "\n=== Python Status Report ===\n";

        if (py.home.empty())
        {
            std::cout << "Status: INVALID (no Python detected)\n";
            std::cout << "=============================\n\n";
            return;
        }

        std::cout << "Home directory: " << py.home << "\n";
        std::cout << "Lib path:       " << py.lib << "\n";
        std::cout << "ZIP-based:      " << (py.source == PythonSource::ZIP ? "Yes" : "No") << "\n";

        // Check Lib/
        if (py.source != PythonSource::ZIP)
        {
            auto enc = py.lib / "encodings";
            std::cout << "Has encodings:  "
                      << (std::filesystem::exists(enc) ? "Yes" : "NO (INVALID)") << "\n";
        }

        std::cout << "=============================\n\n";
    }

    // ---------------------------------------------------------
    // Unified Python locator ( Bundled → ZIP → System )
    // ---------------------------------------------------------
    inline PythonLocation locate_python()
    {
        PythonVersionPolicy versionPolicy;
        versionPolicy.load(VERSION_POLICY_FILENAME);

        // 1. Try system Python first
        if (auto bundled = find_bundled_python(versionPolicy))
            return *bundled;

        // 2. Then ZIP-based Python
        if (auto zip = find_zip_python(versionPolicy))
            return *zip;

        // 3. Try system Python as last resort
        if (auto system = find_system_python(versionPolicy))
            return *system;

        // 4. Nothing found → return empty
        return PythonLocation{};
    }
} // namespace pyembed