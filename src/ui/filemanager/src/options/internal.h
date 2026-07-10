#pragma once

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
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <QtGlobal>
#include <algorithm>

#include "app_startup_qt.h"
#include "custom_localization.h"
#include "display_settings.h"
#include "extract_memory_settings.h"
#include "large_pages_settings.h"
#include "official_lang_catalog.h"
#include "options_dialog.h"
#include "platform_support.h"
#include "portable_settings.h"
#include "shared/column_width_persistence.h"
#include "shared/external_command_parser.h"

namespace z7::ui::filemanager { namespace options_internal {

    inline constexpr char kSettingsFmFoldersWorkDirMode[] = "Options/WorkDirType";
    inline constexpr char kSettingsFmFoldersWorkDirPath[] = "Options/WorkDirPath";
    inline constexpr char kSettingsFmFoldersWorkForRemovableOnly[] = "Options/TempRemovableOnly";
    inline constexpr char kSettingsFmViewer[] = "FM/Viewer";
    inline constexpr char kSettingsFmEditor[] = "FM/Editor";
    inline constexpr char kSettingsFmDiff[] = "FM/Diff";
    inline constexpr char kSettingsFmOptionsAssociationsColumns[] = "FM/View/OptionsAssociationsColumns";
    inline constexpr char kSettingsOptionsIntegrateShell[] = "Options/IntegrateToShellMenu";
    inline constexpr char kSettingsOptionsIntegrateShell32[] = "Options/IntegrateToShellMenu32";
    inline constexpr char kSettingsOptionsCascadedMenu[] = "Options/CascadedMenu";
    inline constexpr char kSettingsOptionsMenuIcons[] = "Options/MenuIcons";
    inline constexpr char kSettingsOptionsElimDupExtract[] = "Options/ElimDupExtract";
    inline constexpr char kSettingsOptionsWriteZoneIdExtract[] = "Options/WriteZoneIdExtract";
    inline constexpr char kSettingsOptionsContextMenu[] = "Options/ContextMenu";
    QString current_user_label();
    QString command_program_part(QString const& cmd_line);
    QString rebuild_command_line_with_program(QString const& cmd_line, QString const& selected_program_path);
    QString ensure_colon_suffix(QString const& text);
    QString unsupported_suffix();
    QString qt_filemanager_unsupported_tooltip();
    bool windows_only_supported();
    QString windows_only_suffix();
    QString windows_only_tooltip();
    QString with_windows_only_suffix_if_unsupported(QString const& text);
    bool finder_shell_supported();
    QString finder_shell_suffix();
    QString finder_shell_tooltip();
    QString with_finder_shell_suffix_if_unsupported(QString const& text);
    bool extract_memory_limit_supported();
    QString extract_memory_limit_suffix();
    QString extract_memory_limit_tooltip();
    QString with_extract_memory_limit_suffix_if_unsupported(QString const& text);
    bool large_pages_supported();
    QString large_pages_suffix();
    QString large_pages_tooltip();
    QString with_large_pages_suffix_if_unsupported(QString const& text);
    int find_combo_index_by_data(QComboBox const* combo, QString const& value);
    void select_combo_value_or_insert(QComboBox* combo, QString const& value, QString const& unavailable_suffix);
    Qt::HighDpiScaleFactorRoundingPolicy hidpi_policy_from_combo(QComboBox const* combo,
                                                                 Qt::HighDpiScaleFactorRoundingPolicy default_policy);
    void
    add_hidpi_policy_combo_item(QComboBox* combo, QString const& label, Qt::HighDpiScaleFactorRoundingPolicy policy);
    quint64 detect_total_ram_bytes();
    quint64 rounded_ram_gb(quint64 ram_bytes);
    int max_mem_limit_gb(quint64 ram_bytes);
    QString format_mem_suffix(quint64 ram_bytes);
    QStringList const& association_extensions();
    QString format_language_summary_line(z7::ui::runtime_support::LangInfo const& info);
    void append_limited_lines(QStringList* out, QStringList const& lines, QString const& title, int max_lines = 40);

}} // namespace z7::ui::filemanager::options_internal
