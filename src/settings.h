#pragma once

#define NOMINMAX
#include <windows.h>

#include <string>

#include "config.h"

namespace sf {

// Settings flow:
//   loadSettings()   reads the registry and clamps bad values (never fails hard)
//   applyOverride()  applies CLI values on top of the registry
//   validateSettings() rejects out-of-range dialog input with a user-facing message
//   saveSettings()   persists only after the dialog is accepted
void deriveTimings(cfg::Settings& settings);
cfg::Settings defaultSettings();

void clampSettings(cfg::Settings& settings);
bool validateSettings(const cfg::Settings& settings, std::wstring& error);

bool loadSettings(cfg::Settings& settings);
bool saveSettings(const cfg::Settings& settings);

bool applySettingOverride(cfg::Settings& settings, const std::wstring& key,
                          const std::wstring& value, std::wstring& error);

bool showSettingsDialog(HINSTANCE instance, HWND owner, cfg::Settings& settings);

} // namespace sf
