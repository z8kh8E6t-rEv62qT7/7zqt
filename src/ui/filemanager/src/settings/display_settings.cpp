#include "display_settings.h"

#include <QString>

namespace z7::ui::filemanager {

DisplaySettings load_display_settings(const z7::platform::qt::PortableSettings& settings) {
  DisplaySettings display_settings;
  display_settings.show_dots =
      settings.value(QString::fromLatin1(kSettingsFmShowDots), false).toBool();
  display_settings.show_real_file_icons =
      settings.value(QString::fromLatin1(kSettingsFmShowRealFileIcons), false).toBool();
  display_settings.full_row_select =
      settings.value(QString::fromLatin1(kSettingsFmFullRowSelect), false).toBool();
  display_settings.show_grid_lines =
      settings.value(QString::fromLatin1(kSettingsFmShowGridLines), false).toBool();
  display_settings.single_click_open =
      settings.value(QString::fromLatin1(kSettingsFmSingleClickOpen), false).toBool();
  display_settings.alternative_selection_mode = settings.value(
      QString::fromLatin1(kSettingsFmAlternativeSelectionMode), false).toBool();
  display_settings.timestamp_level =
      settings.value(QString::fromLatin1(kSettingsFmTimestampLevel), display_settings.timestamp_level)
          .toInt();
  display_settings.timestamp_show_utc =
      settings.value(QString::fromLatin1(kSettingsFmTimestampUtc), false).toBool();
  return display_settings;
}

void save_display_settings(z7::platform::qt::PortableSettings& settings,
                           const DisplaySettings& display_settings) {
  settings.setValue(QString::fromLatin1(kSettingsFmTimestampLevel),
                    display_settings.timestamp_level);
  settings.setValue(QString::fromLatin1(kSettingsFmTimestampUtc),
                    display_settings.timestamp_show_utc);
  settings.setValue(QString::fromLatin1(kSettingsFmShowDots), display_settings.show_dots);
  settings.setValue(QString::fromLatin1(kSettingsFmShowRealFileIcons),
                    display_settings.show_real_file_icons);
  settings.setValue(QString::fromLatin1(kSettingsFmFullRowSelect),
                    display_settings.full_row_select);
  settings.setValue(QString::fromLatin1(kSettingsFmShowGridLines),
                    display_settings.show_grid_lines);
  settings.setValue(QString::fromLatin1(kSettingsFmSingleClickOpen),
                    display_settings.single_click_open);
  settings.setValue(QString::fromLatin1(kSettingsFmAlternativeSelectionMode),
                    display_settings.alternative_selection_mode);
}

}  // namespace z7::ui::filemanager
