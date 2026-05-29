// src/ui/filemanager/src/main_window/internal.h
// Role: Shared internal declarations for MainWindow split translation units.

#pragma once

#include "main_window.h"
#include "model/model.h"
#include "archive_string_codec_qt.h"
#include "archive_virtual_path.h"
#include "custom_localization.h"
#include "main_window/dialogs/archive_add_sources_dialog.h"
#include "platform_support.h"
#include "shared/external_command_parser.h"
#include "shell_integration_menu.h"

#include <QComboBox>
#include <QIcon>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <functional>
#include <memory>
#include <string>

class QAction;
class QModelIndex;
class QSplitter;
class QWidget;

namespace z7::app {
struct OperationResult;
}

namespace z7::platform::qt {
class PortableSettings;
}

namespace z7::ui::filemanager {

class PathComboBox final : public QComboBox {
 public:
  explicit PathComboBox(QWidget* parent = nullptr);

  std::function<void()> before_show_popup;

 protected:
  void showPopup() override;
};

inline constexpr const char* kSettingsFmToolbars = "FM/Toolbars";
inline constexpr const char* kSettingsFmAutoRefresh = "FM/View/AutoRefresh";
inline constexpr const char* kSettingsFmPanels = "FM/Panels";
inline constexpr const char* kSettingsFmListMode = "FM/ListMode";
inline constexpr const char* kSettingsFmPosition = "FM/Position";
inline constexpr const char* kSettingsFmConfirmDelete = "FM/System/ConfirmDelete";
inline constexpr const char* kSettingsFmFoldersWorkDirMode = "Options/WorkDirType";
inline constexpr const char* kSettingsFmFoldersWorkDirPath = "Options/WorkDirPath";
inline constexpr const char* kSettingsFmFoldersWorkForRemovableOnly =
    "Options/TempRemovableOnly";
inline constexpr const char* kSettingsFmColumnsPanel0 = "FM/Columns/Panel0";
inline constexpr const char* kSettingsFmColumnsPanel1 = "FM/Columns/Panel1";
inline constexpr const char* kSettingsFmPanelPath0 = "FM/PanelPath0";
inline constexpr const char* kSettingsFmPanelPath1 = "FM/PanelPath1";
inline constexpr const char* kSettingsFmFolderHistory = "FM/FolderHistory";
inline constexpr const char* kSettingsFmFlatViewArc0 = "FM/FlatViewArc0";
inline constexpr const char* kSettingsFmFlatViewArc1 = "FM/FlatViewArc1";
inline constexpr const char* kSettingsFmViewer = "FM/Viewer";
inline constexpr const char* kSettingsFmEditor = "FM/Editor";
inline constexpr const char* kSettingsFmDiff = "FM/Diff";
inline constexpr const char* kSettingsFmVersionControl = "FM/7vc";
inline constexpr const char* kSettingsFmCopyToHistory = "FM/CopyHistory";
inline constexpr const char* kSettingsOptionsElimDupExtract =
    "Options/ElimDupExtract";
inline constexpr const char* kSettingsOptionsWriteZoneIdExtract =
    "Options/WriteZoneIdExtract";
inline constexpr const char* kArchivePathBarDataPrefix = "__z7_archive_dir__:";
inline constexpr int kDefaultMainWindowWidth = 1380;
inline constexpr int kDefaultMainWindowHeight = 840;

QStringList ancestor_paths(const QString& path);
QString lang_or(uint32_t id);
bool extract_eliminate_root_duplication_from_settings();
QString extract_zone_id_mode_from_settings();

struct FmPanelsState {
  bool present = false;
  bool two_panels = false;
  int active_panel = 0;
  int splitter_pos = 0;
};

FmPanelsState read_fm_panels_state(
    const z7::platform::qt::PortableSettings& settings);
void write_fm_panels_state(z7::platform::qt::PortableSettings* settings,
                           bool two_panels,
                           int active_panel,
                           int splitter_pos);
int current_fm_splitter_pos(const QSplitter* splitter);

QStringList normalize_copy_history(QStringList history,
                                   const QString& new_item,
                                   int max_items = 20);
QStringList read_copy_history();
void save_copy_history(const QStringList& history);
struct CopyTransferDestinationPlan {
  bool valid = false;
  QString destination_dir;
  QString destination_path;
  QString history_path;
  QString display_path;
};
CopyTransferDestinationPlan build_copy_transfer_destination_plan(
    const QString& raw_destination,
    const QString& relative_base,
    int source_count,
    bool force_directory_mode);
bool ensure_copy_transfer_destination_directories(
    const CopyTransferDestinationPlan& plan);
bool validate_basename_only_name(const QString& raw_name,
                                 QString* normalized_name,
                                 QString* error_message);

struct CopyMoveDialogResult {
  bool accepted = false;
  QString destination_path;
};

CopyMoveDialogResult show_copy_move_dialog(QWidget* parent,
                                           const QString& title,
                                           const QString& prompt,
                                           const QString& info_text,
                                           const QString& initial_path);

void reduce_menu_string(QString* value);
QString quoted_reduced_menu_name(QString value);
QString format_menu_name(uint32_t lang_id,
                         const QString& value);
bool is_archive_file(const QString& path);
const QSet<QString>& always_start_extensions();
bool should_always_start_externally(const QString& path);

// File-manager sort-menu semantic identifiers. These are *not* column indices;
// they are mapped to concrete (column, order) pairs by the MainWindow sort
// action handlers.
enum SortAction {
  kSortActionName = 0,
  kSortActionType = 1,
  kSortActionDate = 2,
  kSortActionSize = 3,
  kSortActionUnsorted = 4,
};

inline constexpr const char* kActionCapabilityKeyProperty = "z7.fm.capability.key";
inline constexpr const char* kActionCapabilityReasonProperty = "z7.fm.capability.reason";

enum class ActionCapabilityKey {
  kSplit,
  kCombine,
  kComment,
  kLink,
  kAlternateStreams,
  kSelect,
  kDeselect,
  kSelectByType,
  kDeselectByType,
  kAddToFavorites,
  kContents,
  kTempFiles,
  kSevenZipOpenAs
};

QString action_capability_key(ActionCapabilityKey key);
QIcon load_masked_toolbar_icon(const QString& resource_path);
QString stable_error_message(int error_domain);
}  // namespace z7::ui::filemanager
