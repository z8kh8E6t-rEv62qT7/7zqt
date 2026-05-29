#pragma once

#include <algorithm>

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <Qt>
#include <QtGlobal>
#include <QVBoxLayout>
#include <QWidget>

#include "shared/column_width_persistence.h"
#include "display_settings.h"
#include "app_startup_qt.h"
#include "extract_memory_settings.h"
#include "large_pages_settings.h"
#include "portable_settings.h"
#include "platform_support.h"
#include "shared/external_command_parser.h"
#include "custom_localization.h"
#include "official_lang_catalog.h"
#include "options_dialog.h"

namespace z7::ui::filemanager {

namespace options_internal {

inline constexpr char kSettingsFmFoldersWorkDirMode[] = "Options/WorkDirType";
inline constexpr char kSettingsFmFoldersWorkDirPath[] = "Options/WorkDirPath";
inline constexpr char kSettingsFmFoldersWorkForRemovableOnly[] =
    "Options/TempRemovableOnly";
inline constexpr char kSettingsFmViewer[] = "FM/Viewer";
inline constexpr char kSettingsFmEditor[] = "FM/Editor";
inline constexpr char kSettingsFmDiff[] = "FM/Diff";
inline constexpr char kSettingsFmOptionsAssociationsColumns[] =
    "FM/View/OptionsAssociationsColumns";
inline constexpr char kSettingsOptionsIntegrateShell[] = "Options/IntegrateToShellMenu";
inline constexpr char kSettingsOptionsIntegrateShell32[] = "Options/IntegrateToShellMenu32";
inline constexpr char kSettingsOptionsCascadedMenu[] = "Options/CascadedMenu";
inline constexpr char kSettingsOptionsMenuIcons[] = "Options/MenuIcons";
inline constexpr char kSettingsOptionsElimDupExtract[] = "Options/ElimDupExtract";
inline constexpr char kSettingsOptionsWriteZoneIdExtract[] = "Options/WriteZoneIdExtract";
inline constexpr char kSettingsOptionsContextMenu[] = "Options/ContextMenu";
QString current_user_label();
QString command_program_part(const QString& cmd_line);
QString rebuild_command_line_with_program(const QString& cmd_line,
                                          const QString& selected_program_path);
QString ensure_colon_suffix(const QString& text);
QString unsupported_suffix();
QString qt_filemanager_unsupported_tooltip();
bool windows_only_supported();
QString windows_only_suffix();
QString windows_only_tooltip();
QString with_windows_only_suffix_if_unsupported(const QString& text);
bool finder_shell_supported();
QString finder_shell_suffix();
QString finder_shell_tooltip();
QString with_finder_shell_suffix_if_unsupported(const QString& text);
bool extract_memory_limit_supported();
QString extract_memory_limit_suffix();
QString extract_memory_limit_tooltip();
QString with_extract_memory_limit_suffix_if_unsupported(const QString& text);
bool large_pages_supported();
QString large_pages_suffix();
QString large_pages_tooltip();
QString with_large_pages_suffix_if_unsupported(const QString& text);
int find_combo_index_by_data(const QComboBox* combo, const QString& value);
void select_combo_value_or_insert(QComboBox* combo,
                                  const QString& value,
                                  const QString& unavailable_suffix);
Qt::HighDpiScaleFactorRoundingPolicy hidpi_policy_from_combo(
    const QComboBox* combo,
    Qt::HighDpiScaleFactorRoundingPolicy default_policy);
void add_hidpi_policy_combo_item(QComboBox* combo,
                                 const QString& label,
                                 Qt::HighDpiScaleFactorRoundingPolicy policy);
quint64 detect_total_ram_bytes();
quint64 rounded_ram_gb(quint64 ram_bytes);
int max_mem_limit_gb(quint64 ram_bytes);
QString format_mem_suffix(quint64 ram_bytes);
const QStringList& association_extensions();
QString format_language_summary_line(const z7::ui::runtime_support::LangInfo& info);
void append_limited_lines(QStringList* out,
                          const QStringList& lines,
                          const QString& title,
                          int max_lines = 40);
void sync_finder_extension_snapshot_from_options(QWidget* parent);

}  // namespace options_internal
}  // namespace z7::ui::filemanager
