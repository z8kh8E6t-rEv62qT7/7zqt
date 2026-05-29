#pragma once

#include <array>
#include <cstdint>

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace z7::shell_integration {

inline constexpr const char* kProgram7zFM = "7zFM";

inline constexpr const char* kActionOpen = "open";
inline constexpr const char* kActionOpenAsMenu = "open_as";
inline constexpr const char* kActionOpenAsStar = "open_as_star";
inline constexpr const char* kActionOpenAsHash = "open_as_hash";
inline constexpr const char* kActionOpenAsHashE = "open_as_hash_e";
inline constexpr const char* kActionOpenAs7z = "open_as_7z";
inline constexpr const char* kActionOpenAsZip = "open_as_zip";
inline constexpr const char* kActionOpenAsCab = "open_as_cab";
inline constexpr const char* kActionOpenAsRar = "open_as_rar";
inline constexpr const char* kActionExtractFiles = "extract_files";
inline constexpr const char* kActionExtractHere = "extract_here";
inline constexpr const char* kActionExtractTo = "extract_to";
inline constexpr const char* kActionTestArchive = "test_archive";
inline constexpr const char* kActionAddToArchive = "add_to_archive";
inline constexpr const char* kActionAddTo7z = "add_to_7z";
inline constexpr const char* kActionAddToZip = "add_to_zip";
inline constexpr const char* kActionCrcShaMenu = "crc_sha_menu";
inline constexpr const char* kActionCrc32 = "crc32";
inline constexpr const char* kActionCrc64 = "crc64";
inline constexpr const char* kActionXxh64 = "xxh64";
inline constexpr const char* kActionMd5 = "md5";
inline constexpr const char* kActionSha1 = "sha1";
inline constexpr const char* kActionSha256 = "sha256";
inline constexpr const char* kActionSha384 = "sha384";
inline constexpr const char* kActionSha512 = "sha512";
inline constexpr const char* kActionSha3_256 = "sha3_256";
inline constexpr const char* kActionBlake2sp = "blake2sp";
inline constexpr const char* kActionCrcAll = "crc_all";
inline constexpr const char* kActionGenerateSha256 = "generate_sha256";
inline constexpr const char* kActionChecksumTest = "checksum_test";

struct ShellIntegrationSelection {
  QStringList selected_paths;
  bool shift_pressed = false;
  QString working_directory;
};

struct ShellIntegrationConfig {
  bool enabled = true;
  bool cascaded_menu = true;
  bool show_menu_icons = false;
  bool visible_actions_configured = false;
  QString locale_preferred;
  QSet<QString> visible_actions;
};

struct ShellIntegrationHashMethodDef {
  const char* label;
  const char* method;
};

struct ShellIntegrationMenuAction {
  QString action_id;
  QString title;
};

struct ShellIntegrationMenuPlan {
  bool menu_visible = false;
  QString base_folder;
  QString extract_subdir;
  QString archive_name;
  QString archive_name_7z;
  QString archive_name_zip;
  QStringList selected_paths;
  QStringList selected_files;

  bool show_open = false;
  bool show_open_as = false;
  bool show_extract_group = false;
  bool show_test = false;
  bool show_compress_group = false;
  bool show_crc_group = false;

  QList<ShellIntegrationMenuAction> actions;
};

struct ShellIntegrationValidationResult {
  bool ok = false;
  QString error;
};

const std::array<ShellIntegrationHashMethodDef, 11>& shell_integration_hash_method_defs();

QString shell_integration_normalize_fs_name(QString name);
bool shell_integration_do_need_extract_name(const QString& name);
QString shell_integration_extract_subfolder_name(const QString& archive_name);
QString shell_integration_create_archive_name_from_paths(const QStringList& paths,
                                                         bool is_hash,
                                                         QString* base_name_out);

QStringList shell_integration_context_menu_action_ids();
QStringList normalize_shell_integration_visible_actions(const QStringList& action_ids);
QStringList default_shell_integration_visible_actions();
std::uint32_t shell_integration_context_menu_flags_from_visible_actions(
    const QStringList& action_ids);
QStringList shell_integration_visible_actions_from_context_menu_flags(
    std::uint32_t flags);

ShellIntegrationMenuPlan build_shell_integration_menu_plan(
    const ShellIntegrationSelection& selection,
    const ShellIntegrationConfig& config,
    const QString& locale_hint = QString());
ShellIntegrationValidationResult validate_shell_integration_action(
    const QString& action_id,
    const ShellIntegrationSelection& selection,
    const ShellIntegrationConfig& config,
    const QString& locale_hint = QString());

}  // namespace z7::shell_integration
