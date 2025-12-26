#include "ini_store.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace {
    inline std::string trim(const std::string& s) {
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
            ++start;
        }
        size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
            --end;
        }
        return s.substr(start, end - start);
    }
}

IniFile::IniFile(const std::string& path)
    : m_path(path)
{}

bool IniFile::load() {
    m_sections.clear();
    std::ifstream in(m_path);
    if (!in.is_open()) {
        // Not an error: missing file is treated as empty config.
        return true;
    }

    std::string line;
    std::string current_section;

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            current_section = trim(line.substr(1, line.size() - 2));
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        std::string key   = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        m_sections[current_section].kv[key] = value;
    }

    return true;
}

bool IniFile::save() const {
    std::ofstream out(m_path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    for (const auto& pair : m_sections) {
        const std::string& section_name = pair.first;
        const IniSection& sect = pair.second;

        if (!section_name.empty()) {
            out << "[" << section_name << "]\n";
        }
        for (const auto& kv : sect.kv) {
            out << kv.first << " = " << kv.second << "\n";
        }
        out << "\n";
    }

    return true;
}

std::optional<std::string> IniFile::get(const std::string& section,
                                        const std::string& key) const {
    auto it = m_sections.find(section);
    if (it == m_sections.end()) {
        return std::nullopt;
    }
    auto it2 = it->second.kv.find(key);
    if (it2 == it->second.kv.end()) {
        return std::nullopt;
    }
    return it2->second;
}

void IniFile::set(const std::string& section,
                  const std::string& key,
                  const std::string& value) {
    m_sections[section].kv[key] = value;
}
