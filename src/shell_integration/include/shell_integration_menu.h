#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <array>
#include <cstdint>

namespace z7::shell_integration {

    inline constexpr char const* kProgram7zFM = "7zFM";

    inline constexpr char const* kActionOpen = "open";
    inline constexpr char const* kActionOpenAsMenu = "open_as";
    inline constexpr char const* kActionOpenAsStar = "open_as_star";
    inline constexpr char const* kActionOpenAsHash = "open_as_hash";
    inline constexpr char const* kActionOpenAsHashE = "open_as_hash_e";
    inline constexpr char const* kActionOpenAs7z = "open_as_7z";
    inline constexpr char const* kActionOpenAsZip = "open_as_zip";
    inline constexpr char const* kActionOpenAsCab = "open_as_cab";
    inline constexpr char const* kActionOpenAsRar = "open_as_rar";
    inline constexpr char const* kActionExtractFiles = "extract_files";
    inline constexpr char const* kActionExtractHere = "extract_here";
    inline constexpr char const* kActionExtractTo = "extract_to";
    inline constexpr char const* kActionTestArchive = "test_archive";
    inline constexpr char const* kActionAddToArchive = "add_to_archive";
    inline constexpr char const* kActionAddTo7z = "add_to_7z";
    inline constexpr char const* kActionAddToZip = "add_to_zip";
    inline constexpr char const* kActionCrcShaMenu = "crc_sha_menu";
    inline constexpr char const* kActionCrc32 = "crc32";
    inline constexpr char const* kActionCrc64 = "crc64";
    inline constexpr char const* kActionXxh64 = "xxh64";
    inline constexpr char const* kActionMd5 = "md5";
    inline constexpr char const* kActionSha1 = "sha1";
    inline constexpr char const* kActionSha256 = "sha256";
    inline constexpr char const* kActionSha384 = "sha384";
    inline constexpr char const* kActionSha512 = "sha512";
    inline constexpr char const* kActionSha3_256 = "sha3_256";
    inline constexpr char const* kActionBlake2sp = "blake2sp";
    inline constexpr char const* kActionCrcAll = "crc_all";
    inline constexpr char const* kActionGenerateSha256 = "generate_sha256";
    inline constexpr char const* kActionChecksumTest = "checksum_test";

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
        char const* label;
        char const* method;
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

    std::array<ShellIntegrationHashMethodDef, 11> const& shell_integration_hash_method_defs();

    QString shell_integration_normalize_fs_name(QString name);
    bool shell_integration_do_need_extract_name(QString const& name);
    QString shell_integration_extract_subfolder_name(QString const& archive_name);
    QString
    shell_integration_create_archive_name_from_paths(QStringList const& paths, bool is_hash, QString* base_name_out);

    QStringList shell_integration_context_menu_action_ids();
    QStringList normalize_shell_integration_visible_actions(QStringList const& action_ids);
    QStringList default_shell_integration_visible_actions();
    std::uint32_t shell_integration_context_menu_flags_from_visible_actions(QStringList const& action_ids);
    QStringList shell_integration_visible_actions_from_context_menu_flags(std::uint32_t flags);

    ShellIntegrationMenuPlan build_shell_integration_menu_plan(ShellIntegrationSelection const& selection,
                                                               ShellIntegrationConfig const& config);
    ShellIntegrationValidationResult validate_shell_integration_action(QString const& action_id,
                                                                       ShellIntegrationSelection const& selection,
                                                                       ShellIntegrationConfig const& config);

} // namespace z7::shell_integration
