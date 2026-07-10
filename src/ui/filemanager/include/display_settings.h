#pragma once

#include "portable_settings.h"

namespace z7::ui::filemanager {

    inline constexpr char const* kSettingsFmTimestampLevel = "FM/View/TimestampLevel";
    inline constexpr char const* kSettingsFmTimestampUtc = "FM/View/TimestampUtc";
    inline constexpr char const* kSettingsFmShowDots = "FM/ShowDots";
    inline constexpr char const* kSettingsFmShowRealFileIcons = "FM/ShowRealFileIcons";
    inline constexpr char const* kSettingsFmFullRowSelect = "FM/FullRow";
    inline constexpr char const* kSettingsFmShowGridLines = "FM/ShowGrid";
    inline constexpr char const* kSettingsFmSingleClickOpen = "FM/SingleClick";
    inline constexpr char const* kSettingsFmAlternativeSelectionMode = "FM/AlternativeSelection";

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

    DisplaySettings load_display_settings(z7::platform::qt::PortableSettings const& settings);
    void save_display_settings(z7::platform::qt::PortableSettings& settings, DisplaySettings const& display_settings);

} // namespace z7::ui::filemanager
