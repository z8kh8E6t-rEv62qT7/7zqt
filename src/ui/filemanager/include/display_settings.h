#pragma once

#include "portable_settings.h"

namespace z7::ui::filemanager {

inline constexpr const char* kSettingsFmTimestampLevel = "FM/View/TimestampLevel";
inline constexpr const char* kSettingsFmTimestampUtc = "FM/View/TimestampUtc";
inline constexpr const char* kSettingsFmShowDots = "FM/ShowDots";
inline constexpr const char* kSettingsFmShowRealFileIcons = "FM/ShowRealFileIcons";
inline constexpr const char* kSettingsFmFullRowSelect = "FM/FullRow";
inline constexpr const char* kSettingsFmShowGridLines = "FM/ShowGrid";
inline constexpr const char* kSettingsFmSingleClickOpen = "FM/SingleClick";
inline constexpr const char* kSettingsFmAlternativeSelectionMode =
    "FM/AlternativeSelection";

struct DisplaySettings {
  int timestamp_level = -1;
  bool timestamp_show_utc = false;
  bool show_dots = false;
  bool show_real_file_icons = false;
  bool full_row_select = false;
  bool show_grid_lines = false;
  bool single_click_open = false;
  bool alternative_selection_mode = false;
};

DisplaySettings load_display_settings(const z7::platform::qt::PortableSettings& settings);
void save_display_settings(z7::platform::qt::PortableSettings& settings,
                           const DisplaySettings& display_settings);

}  // namespace z7::ui::filemanager
