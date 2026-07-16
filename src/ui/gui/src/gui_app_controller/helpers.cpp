// src/ui/gui/src/gui_app_controller/helpers.cpp
// Role: Command parsing and dialog preparation helpers for GuiAppController.

#include "helpers.h"

#include <QDialog>
#include <QFileInfo>
#include <cstdlib>
#include <limits>
#include <string>
#ifdef Z7_TESTING
#include <QString>
#include <QTimer>
#endif

#include "archive_string_codec_qt.h"
#include "common/archive_type_normalization.h"
#include "compress_dialog.h"
#include "extract_dialog.h"
#include "official_lang_catalog.h"
#include "platform_support.h"

namespace z7::ui::gui::gui_app_controller_helpers {
    namespace {

        using z7::ui::runtime_support::L;
        using z7::ui::runtime_support::strip_mnemonic;

        bool is_unsigned_decimal(std::string_view value) {
            if (value.empty()) {
                return false;
            }
            for (char c : value) {
                if (c < '0' || c > '9') {
                    return false;
                }
            }
            return true;
        }

#ifdef Z7_TESTING
        constexpr char const* kSuppressResultDialogsEnv = "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS";
        constexpr char const* kAutoAcceptTaskSetupDialogsEnv = "Z7_TEST_AUTO_ACCEPT_TASK_SETUP_DIALOGS";

        void schedule_task_setup_dialog_auto_accept(QDialog* dialog) {
            if (dialog == nullptr
                || qEnvironmentVariable(kAutoAcceptTaskSetupDialogsEnv).trimmed() != QStringLiteral("1")) {
                return;
            }
            QTimer::singleShot(0, dialog, &QDialog::accept);
        }
#endif

        struct AddDialogInputContext {
            bool single_file_input = false;
            std::string single_file_name;
        };

        AddDialogInputContext input_context_for_add_dialog(AddTaskSpec const& spec) {
            AddDialogInputContext context;
            if (spec.input_paths.size() != 1) {
                return context;
            }

            QString const input_path = z7::ui::archive_support::from_native_string(spec.input_paths.front());
            QFileInfo const info(input_path);
            if (!info.exists() || info.isDir()) {
                return context;
            }

            context.single_file_input = true;
            context.single_file_name = z7::ui::archive_support::to_native_string(info.fileName());
            return context;
        }

        bool add_dialog_format_allows_encrypted_headers(std::string const& archive_type) {
            return z7::common::canonical_archive_type_token_copy(archive_type) == "7z";
        }

        CompressCommandOptions compress_options_from_add_task_spec(AddTaskSpec const& spec) {
            CompressCommandOptions options;
            AddDialogInputContext const input_context = input_context_for_add_dialog(spec);
            options.archive_path = spec.archive_path;
            options.archive_type = spec.archive_type;
            options.keep_archive_name_extension = !input_context.single_file_input;
            options.single_file_input = input_context.single_file_input;
            options.single_file_name = input_context.single_file_name;
            options.update_mode = spec.update_mode;
            options.path_mode = spec.path_mode;
            options.create_sfx = spec.create_sfx;
            options.share_for_write = spec.share_for_write;
            options.delete_after_compressing = spec.delete_after_compressing;
            options.compression_level = spec.compression_level;
            options.method = spec.method_value;
            options.dictionary_size = spec.dictionary_size;
            options.word_size = spec.word_size;
            options.solid_block_size = spec.solid_block_size;
            options.thread_count = spec.thread_count;
            options.volume_size = spec.volume_size;
            options.password = spec.password;
            if (!spec.password.empty()) {
                options.encryption_method = spec.encryption_method;
                options.encrypt_headers = spec.encrypt_headers_defined && spec.encrypt_headers;
            }
            options.symbolic_links_defined = spec.symbolic_links_defined;
            options.symbolic_links = spec.symbolic_links;
            options.hard_links_defined = spec.hard_links_defined;
            options.hard_links = spec.hard_links;
            options.alternate_streams_defined = spec.alternate_streams_defined;
            options.alternate_streams = spec.alternate_streams;
            options.file_security_defined = spec.file_security_defined;
            options.file_security = spec.file_security;
            options.preserve_access_time = spec.preserve_access_time;
            options.write_mtime_defined = spec.write_mtime_defined;
            options.write_mtime = spec.write_mtime;
            options.write_ctime_defined = spec.write_ctime_defined;
            options.write_ctime = spec.write_ctime;
            options.write_atime_defined = spec.write_atime_defined;
            options.write_atime = spec.write_atime;
            options.set_archive_mtime = spec.set_archive_mtime;
            options.extra_parameters = spec.extra_parameters;
            options.opaque_add_task.raw_update_switch = spec.raw_update_switch;
            options.opaque_add_task.raw_update_switches = spec.raw_update_switches;
            return options;
        }

        void apply_compress_options_to_add_task_spec(CompressCommandOptions const& options, AddTaskSpec* spec) {
            if (spec == nullptr) {
                return;
            }
            spec->archive_path = options.archive_path;
            spec->archive_type = options.archive_type;
            spec->update_mode = options.update_mode;
            spec->path_mode = options.path_mode;
            spec->create_sfx = options.create_sfx;
            spec->share_for_write = options.share_for_write;
            spec->delete_after_compressing = options.delete_after_compressing;
            spec->compression_level = options.compression_level;
            spec->method_value = options.method;
            spec->dictionary_size = options.dictionary_size;
            spec->word_size = options.word_size;
            spec->solid_block_size = options.solid_block_size;
            spec->thread_count = options.thread_count;
            spec->volume_size = options.volume_size;
            spec->password = options.password;
            bool const has_password = !options.password.empty();
            bool const encrypt_headers_allowed =
                has_password && add_dialog_format_allows_encrypted_headers(options.archive_type);
            spec->encrypt_headers_defined = encrypt_headers_allowed;
            spec->encrypt_headers = encrypt_headers_allowed && options.encrypt_headers;
            spec->encryption_method = has_password ? options.encryption_method : std::string();
            spec->symbolic_links_defined = options.symbolic_links_defined;
            spec->symbolic_links = options.symbolic_links;
            spec->hard_links_defined = options.hard_links_defined;
            spec->hard_links = options.hard_links;
            spec->alternate_streams_defined = options.alternate_streams_defined;
            spec->alternate_streams = options.alternate_streams;
            spec->file_security_defined = options.file_security_defined;
            spec->file_security = options.file_security;
            spec->preserve_access_time = options.preserve_access_time;
            spec->write_mtime_defined = options.write_mtime_defined;
            spec->write_mtime = options.write_mtime;
            spec->write_ctime_defined = options.write_ctime_defined;
            spec->write_ctime = options.write_ctime;
            spec->write_atime_defined = options.write_atime_defined;
            spec->write_atime = options.write_atime;
            spec->set_archive_mtime = options.set_archive_mtime;
            spec->extra_parameters = options.extra_parameters;
            spec->raw_update_switch = options.opaque_add_task.raw_update_switch;
            spec->raw_update_switches = options.opaque_add_task.raw_update_switches;
        }

    } // namespace

#ifdef Z7_TESTING
    bool suppress_result_dialogs_for_tests() {
        return qEnvironmentVariable(kSuppressResultDialogsEnv).trimmed() == QStringLiteral("1");
    }
#endif

    uint32_t benchmark_iterations_or_default(GuiTaskSpec const& spec) {
        return std::visit(
            [](auto const& typed_spec) -> uint32_t {
                using T = std::decay_t<decltype(typed_spec)>;
                if constexpr (std::is_same_v<T, BenchmarkTaskSpec>) {
                    for (std::string const& operand : typed_spec.operands) {
                        if (!is_unsigned_decimal(operand)) {
                            continue;
                        }
                        unsigned long long const parsed = std::strtoull(operand.c_str(), nullptr, 10);
                        if (parsed == 0 || parsed > std::numeric_limits<uint32_t>::max()) {
                            continue;
                        }
                        return static_cast<uint32_t>(parsed);
                    }
                }
                return 10U;
            },
            spec);
    }

    QString task_title(GuiTaskSpec const& spec) {
        return std::visit(
            [](auto const& typed_spec) {
                using T = std::decay_t<decltype(typed_spec)>;
                if constexpr (std::is_same_v<T, AddTaskSpec>) {
                    return QStringLiteral("7zG - %1").arg(strip_mnemonic(L(7200)));
                } else if constexpr (std::is_same_v<T, ExtractTaskSpec>) {
                    return QStringLiteral("7zG - %1").arg(strip_mnemonic(L(7201)));
                } else if constexpr (std::is_same_v<T, ArchiveExportTaskSpec>) {
                    return QStringLiteral("7zG - %1").arg(strip_mnemonic(L(6000)));
                } else if constexpr (std::is_same_v<T, TestTaskSpec>) {
                    return QStringLiteral("7zG - %1").arg(strip_mnemonic(L(7202)));
                } else if constexpr (std::is_same_v<T, HashTaskSpec>) {
                    return QStringLiteral("7zG - %1").arg(strip_mnemonic(L(7500)));
                } else if constexpr (std::is_same_v<T, ArchiveHashTaskSpec>) {
                    return QStringLiteral("7zG - %1").arg(strip_mnemonic(L(7500)));
                } else if constexpr (std::is_same_v<T, ArchiveTestTaskSpec>) {
                    return QStringLiteral("7zG - %1").arg(strip_mnemonic(L(7202)));
                } else if constexpr (std::is_same_v<T, BenchmarkTaskSpec>) {
                    return QStringLiteral("7zG - %1").arg(strip_mnemonic(L(7600)));
                } else if constexpr (std::is_same_v<T, OpenTaskSpec>) {
                    return QStringLiteral("7zG - %1").arg(QStringLiteral("Open"));
                } else {
                    return QStringLiteral("7zG - %1").arg(QStringLiteral("Quick Look Export"));
                }
            },
            spec);
    }

    TaskSpecPreparationResult prepare_task_spec_with_optional_dialog(GuiTaskSpec const& requested_spec) {
        TaskSpecPreparationResult result;
        result.status = TaskSpecPreparationStatus::kPrepared;
        result.spec = requested_spec;
        TaskSpecPreparationStatus const status = std::visit(
            [&](auto& typed_spec) -> TaskSpecPreparationStatus {
                using T = std::decay_t<decltype(typed_spec)>;
                if constexpr (std::is_same_v<T, AddTaskSpec>) {
                    typed_spec.show_dialog = false;
                    if (!std::get<AddTaskSpec>(requested_spec).show_dialog) {
                        return TaskSpecPreparationStatus::kPrepared;
                    }
                    CompressCommandOptions const options = compress_options_from_add_task_spec(typed_spec);
                    CompressDialog dialog(options);
#ifdef Z7_TESTING
                    schedule_task_setup_dialog_auto_accept(&dialog);
#endif
                    int const dialog_result = dialog.exec();
                    if (dialog_result == QDialog::Rejected) {
                        return TaskSpecPreparationStatus::kCanceled;
                    }
                    if (dialog_result != QDialog::Accepted) {
                        return TaskSpecPreparationStatus::kFailed;
                    }
                    CompressCommandOptions const accepted = dialog.options();
                    apply_compress_options_to_add_task_spec(accepted, &typed_spec);
                    return TaskSpecPreparationStatus::kPrepared;
                } else if constexpr (std::is_same_v<T, ExtractTaskSpec>) {
                    typed_spec.show_dialog = false;
                    if (!std::get<ExtractTaskSpec>(requested_spec).show_dialog) {
                        return TaskSpecPreparationStatus::kPrepared;
                    }
                    ExtractCommandOptions options;
                    options.output_dir = typed_spec.output_dir;
                    options.split_dest_enabled = typed_spec.split_dest_enabled;
                    options.split_dest_name = typed_spec.split_dest_name;
                    options.overwrite_switch = typed_spec.overwrite_switch;
                    options.path_mode = typed_spec.path_mode;
                    options.eliminate_root_duplication = typed_spec.eliminate_root_duplication;
                    options.password = typed_spec.password;
                    options.restore_file_security = typed_spec.restore_file_security
                                                 && z7::ui::runtime_support::is_platform_supported(
                                                        z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
                    if (!typed_spec.archive_inputs.empty()) {
                        options.archive_name = typed_spec.archive_inputs.front();
                    }

                    ExtractDialog dialog(options);
#ifdef Z7_TESTING
                    schedule_task_setup_dialog_auto_accept(&dialog);
#endif
                    int const dialog_result = dialog.exec();
                    if (dialog_result == QDialog::Rejected) {
                        return TaskSpecPreparationStatus::kCanceled;
                    }
                    if (dialog_result != QDialog::Accepted) {
                        return TaskSpecPreparationStatus::kFailed;
                    }
                    ExtractCommandOptions const accepted = dialog.options();
                    typed_spec.output_dir = accepted.output_dir;
                    typed_spec.split_dest_enabled = accepted.split_dest_enabled;
                    typed_spec.split_dest_name = accepted.split_dest_name;
                    typed_spec.overwrite_switch = accepted.overwrite_switch;
                    typed_spec.path_mode = accepted.path_mode;
                    typed_spec.eliminate_root_duplication = accepted.eliminate_root_duplication;
                    typed_spec.password = accepted.password;
                    typed_spec.restore_file_security = accepted.restore_file_security;
                    return TaskSpecPreparationStatus::kPrepared;
                } else {
                    return TaskSpecPreparationStatus::kPrepared;
                }
            },
            result.spec);
        result.status = status;
        return result;
    }

} // namespace z7::ui::gui::gui_app_controller_helpers
