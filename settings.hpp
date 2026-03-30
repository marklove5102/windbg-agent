#pragma once

#include <string>

namespace windbg_agent
{

// Settings stored in ~/.windbg_agent/settings.json
struct Settings
{
};

// Get the settings directory path (~/.windbg_agent)
std::string GetSettingsDir();

// Get the settings file path (~/.windbg_agent/settings.json)
std::string GetSettingsPath();

// Load settings from disk (creates default if not exists)
Settings LoadSettings();

// Save settings to disk
void SaveSettings(const Settings& settings);

} // namespace windbg_agent
