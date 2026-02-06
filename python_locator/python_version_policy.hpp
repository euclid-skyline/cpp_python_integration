#pragma once
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <functional>
#include <vector>
#include <unordered_map>

namespace pyembed
{

    const const std::string VERSION_POLICY_FILENAME = "python_required_version.txt";
    const const std::string VERSION_DEFAULT_MIN_VALUE = "3.11.0";

    class PythonVersionPolicy
    {
    public:
        // ------------------------------------------------------------
        // Version structure with comparison operators
        // ------------------------------------------------------------
        struct Version
        {
            int major = 0;
            int minor = 0;
            int patch = 0;
            bool valid = false;
            bool patch_strict = false; // for Patch version matching (False = ignore patch)
            // Comparison operators overloads
            bool operator<(const Version &o) const
            {
                if (major != o.major)
                    return major < o.major;
                if (minor != o.minor)
                    return minor < o.minor;
                return patch < o.patch;
            }
            bool operator>(const Version &o) const { return o < *this; }
            bool operator<=(const Version &o) const { return !(o < *this); }
            bool operator>=(const Version &o) const { return !(*this < o); }
            bool operator==(const Version &o) const
            {
                return patch_strict || o.patch_strict
                           ? (major == o.major && minor == o.minor && patch == o.patch)
                           : (major == o.major && minor == o.minor);
            }
            bool operator!=(const Version &o) const
            {
                return !(*this == o);
            }

            std::string to_string() const
            {
                std::ostringstream oss;
                oss << major << "." << minor << "." << patch;
                return oss.str();
            }
        };

    private:
        Version minVer;
        Version maxVer;
        Version preferredVer;
        Version defaultMinVer; // May come from TXT file
        // ------------------------------------------------------------
        // KeyAction registry for version-policy keys
        // ------------------------------------------------------------
        struct KeyAction
        {
            bool requires_value;
            std::function<void(PythonVersionPolicy &, const Version &)> handler;
        };
        struct Rule
        {
            std::function<void(PythonVersionPolicy &)> apply;
        };

        // Key registry: key → action mapping using Command design pattern
        static const std::unordered_map<std::string, KeyAction> KEY_REGISTRY;
        // Set of rules to apply after loading using Chain of Responsibility design pattern
        static const std::vector<Rule> RULES;

    public:
        PythonVersionPolicy()
        {
            // Built‑in default minimum version
            defaultMinVer = parse_version(VERSION_DEFAULT_MIN_VALUE);

            // Initial values
            minVer = defaultMinVer;
            maxVer = Version{0, 0, 0, false};
            preferredVer = Version{0, 0, 0, false};
        }

        // ------------------------------------------------------------
        // Parse "3.14.2" or "3.14" or "3"
        // ------------------------------------------------------------
        static Version parse_version(const std::string &s)
        {
            std::regex re(R"((\d+)(?:\.(\d+))?(?:\.(\d+))?)");
            std::smatch m;

            if (!std::regex_match(s, m, re))
                return {};

            Version v;
            v.major = std::stoi(m[1]);
            v.minor = m[2].matched ? std::stoi(m[2]) : 0;
            v.patch = m[3].matched ? std::stoi(m[3]) : 0;
            v.valid = true;
            return v;
        }

        // ------------------------------------------------------------
        // Load version rules from python_required_version.txt
        // ------------------------------------------------------------
        void load(const std::filesystem::path &txtPath)
        {
            std::cout << "Loading Python version policy from: " << txtPath << std::endl;
            if (!std::filesystem::exists(txtPath))
                return;

            std::ifstream f(txtPath);
            if (!f)
                return;
            std::cout << "Parsing version policy..." << std::endl;
            std::string line;
            int lineNumber = 0;

            while (std::getline(f, line))
            {
                lineNumber++;

                if (line.empty() || line[0] == '#')
                    continue;

                auto pos = line.find('=');
                if (pos == std::string::npos)
                {
                    std::cout << "[VersionPolicy] Error (line " << lineNumber
                              << "): Missing '=' in line: " << line << std::endl;
                    return;
                }

                std::string key = line.substr(0, pos);
                std::string val = line.substr(pos + 1);

                if (val.empty())
                {
                    std::cout << "[VersionPolicy] Error (line " << lineNumber
                              << "): Key '" << key << "' has no value." << std::endl;
                    return;
                }

                // --------------------------------------------------------
                // Use key registry to handle keys actions
                // --------------------------------------------------------
                auto it = KEY_REGISTRY.find(key);
                if (it == KEY_REGISTRY.end())
                {
                    std::cout << "[VersionPolicy] Error (line " << lineNumber
                              << "): Unknown key '" << key << "' ignored." << std::endl;
                    return;
                }

                const auto &entry = it->second;
                // Parse version value
                Version v = parse_version(val);
                if (entry.requires_value && !v.valid)
                {
                    std::cout << "[VersionPolicy] Error (line " << lineNumber
                              << "): Invalid version '" << val
                              << "' for key '" << key << "'." << std::endl;
                    return;
                }

                entry.handler(*this, v);
            }

            // ------------------------------------------------------------
            // APPLY RULES AFTER LOADING
            // ------------------------------------------------------------
            for (const auto &rule : RULES)
            {
                rule.apply(*this);
            }

            // std::cout << "Default Min Ver: " << defaultMinVer.to_string() << std::endl;
            // std::cout << "Min Ver: " << minVer.to_string() << std::endl;
            // std::cout << "Max Ver: " << maxVer.to_string() << std::endl;
            // std::cout << "Preferred Ver: " << preferredVer.to_string() << std::endl;
        }

        // ------------------------------------------------------------
        // Check if version is acceptable
        // ------------------------------------------------------------
        bool in_range(const Version &v) const
        {
            if (!v.valid)
                return false;

            if (preferredVer.valid)
                return v == preferredVer;

            if (v < minVer)
                return false;
            if (v > maxVer)
                return false;

            return true;
        }

        // Accessors
        const Version &min() const { return minVer; }
        const Version &max() const { return maxVer; }
        const Version &preferred() const { return preferredVer; }
        const Version &default_min() const { return defaultMinVer; }
    };

    const std::unordered_map<std::string, PythonVersionPolicy::KeyAction>
        PythonVersionPolicy::KEY_REGISTRY = {
            {"PYTHON_DEFAULT_MIN_VERSION",
             {true,
              [](PythonVersionPolicy &p, const Version &v)
              {
                  if (v.valid && v > p.default_min())
                      p.defaultMinVer = v;
              }}},
            {"PYTHON_MIN_VERSION",
             {true,
              [](PythonVersionPolicy &p, const Version &v)
              {
                  if (v.valid)
                      p.minVer = v;
              }}},
            {"PYTHON_MAX_VERSION",
             {true,
              [](PythonVersionPolicy &p, const Version &v)
              {
                  if (v.valid)
                      p.maxVer = v;
              }}},
            {"PYTHON_PREFERRED_VERSION",
             {true,
              [](PythonVersionPolicy &p, const Version &v)
              {
                  if (v.valid)
                      p.preferredVer = v;
              }}}};

    const std::vector<PythonVersionPolicy::Rule> PythonVersionPolicy::RULES = {
        // Rule A: Raise MIN to defaultMinVer if lower
        {
            [](PythonVersionPolicy &p)
            {
                if (p.minVer.valid && p.minVer < p.defaultMinVer)
                    p.minVer = p.defaultMinVer;
            }},
        // Rule B: Raise MAX to defaultMinVer if lower
        {
            [](PythonVersionPolicy &p)
            {
                if (p.maxVer.valid && p.maxVer < p.defaultMinVer)
                    p.maxVer = p.defaultMinVer;
            }},
        // Rule C: If only MIN provided → MAX = MIN
        {
            [](PythonVersionPolicy &p)
            {
                if (p.minVer.valid && !p.maxVer.valid)
                    p.maxVer = p.minVer;
            }},
        // Rule D: If only MAX provided → MIN = MAX
        {
            [](PythonVersionPolicy &p)
            {
                if (!p.minVer.valid && p.maxVer.valid)
                    p.minVer = p.maxVer;
            }},
        // Rule E: Preferred overrides everything
        {
            [](PythonVersionPolicy &p)
            {
                if (p.preferredVer.valid)
                {
                    // Preferred must also respect defaultMinVer
                    if (p.preferredVer < p.defaultMinVer)
                        p.preferredVer = p.defaultMinVer;

                    p.minVer = p.preferredVer;
                    p.maxVer = p.preferredVer;
                }
            }}};

} // namespace pyembed