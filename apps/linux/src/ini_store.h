#pragma once
#include <string>
#include <unordered_map>
#include <optional>

struct IniSection {
    std::unordered_map<std::string, std::string> kv;
};

class IniFile {
public:
    explicit IniFile(const std::string& path);

    bool load();
    bool save() const;

    std::optional<std::string> get(const std::string& section,
                                   const std::string& key) const;

    void set(const std::string& section,
             const std::string& key,
             const std::string& value);

private:
    std::string m_path;
    std::unordered_map<std::string, IniSection> m_sections;
};
