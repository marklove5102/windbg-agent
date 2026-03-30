#include "settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace windbg_agent
{

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string GetSettingsDir()
{
    // Get user home directory
    const char* home = std::getenv("USERPROFILE");
    if (!home)
        home = std::getenv("HOME");
    if (!home)
        return ".windbg_agent";
    return std::string(home) + "\\.windbg_agent";
}

std::string GetSettingsPath()
{
    return GetSettingsDir() + "\\settings.json";
}

Settings LoadSettings()
{
    Settings settings;
    std::string path = GetSettingsPath();

    // Create directory if it doesn't exist
    std::string dir = GetSettingsDir();
    if (!fs::exists(dir))
        fs::create_directories(dir);

    if (!fs::exists(path))
    {
        // Create default settings file
        SaveSettings(settings);
    }

    return settings;
}

void SaveSettings(const Settings& settings)
{
    std::string dir = GetSettingsDir();
    if (!fs::exists(dir))
        fs::create_directories(dir);

    json j = json::object();

    std::ofstream file(GetSettingsPath());
    if (file.is_open())
        file << j.dump(2);
}

} // namespace windbg_agent
