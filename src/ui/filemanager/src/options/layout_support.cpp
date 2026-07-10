// src/ui/filemanager/src/options/layout_support.cpp
// Role: Options dialog shared helper functions.

#include <QComboBox>

#include "archive_suffix_catalog.h"
#include "internal.h"

namespace z7::ui::filemanager::options_internal {

    QString current_user_label() {
        QStringList const candidates = {qEnvironmentVariable("USER").trimmed(),
                                        qEnvironmentVariable("USERNAME").trimmed(),
                                        QDir(QDir::homePath()).dirName().trimmed()};
        for (QString const& candidate : candidates) {
            if (!candidate.isEmpty()) {
                return candidate;
            }
        }
        return QStringLiteral("Current user");
    }

    QString command_program_part(QString const& cmd_line) {
        return parse_external_command_line(cmd_line).program;
    }

    QString rebuild_command_line_with_program(QString const& cmd_line, QString const& selected_program_path) {
        QString const trimmed_program_path = selected_program_path.trimmed();
        if (trimmed_program_path.isEmpty()) {
            return QString();
        }

        ExternalCommandParts const command = parse_external_command_line(cmd_line);

        QString rebuilt = QStringLiteral("\"%1\"").arg(trimmed_program_path);
        if (!command.arguments.isEmpty()) {
            rebuilt += QLatin1Char(' ');
            rebuilt += command.arguments;
        }
        return rebuilt;
    }

    QString ensure_colon_suffix(QString const& text) {
        QString const trimmed = text.trimmed();
        if (trimmed.endsWith(QLatin1Char(':')) || trimmed.endsWith(QChar(0xFF1A))) {
            return text;
        }
        return text + QStringLiteral(":");
    }

    QString unsupported_suffix() {
        return QStringLiteral(" (Unsupported)");
    }

    QString qt_filemanager_unsupported_tooltip() {
        return QStringLiteral("Not supported in this Qt file manager.");
    }

    bool windows_only_supported() {
        return z7::ui::runtime_support::is_platform_supported(z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
    }

    QString windows_only_suffix() {
        return z7::ui::runtime_support::platform_suffix(z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
    }

    QString windows_only_tooltip() {
        return z7::ui::runtime_support::platform_tooltip(z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
    }

    QString with_windows_only_suffix_if_unsupported(QString const& text) {
        return z7::ui::runtime_support::with_platform_suffix_if_unsupported(
            text, z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
    }

    bool finder_shell_supported() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        return true;
#else
        return false;
#endif
    }

    QString finder_shell_suffix() {
        return QStringLiteral(" (Windows/macOS)");
    }

    QString finder_shell_tooltip() {
        return QStringLiteral("Windows and macOS only");
    }

    QString with_finder_shell_suffix_if_unsupported(QString const& text) {
        if (finder_shell_supported()) {
            return text;
        }
        return text + finder_shell_suffix();
    }

    bool extract_memory_limit_supported() {
        return z7::ui::runtime_support::extract_memory_limit_supported();
    }

    QString extract_memory_limit_suffix() {
        return z7::ui::runtime_support::extract_memory_limit_platform_suffix();
    }

    QString extract_memory_limit_tooltip() {
        return z7::ui::runtime_support::extract_memory_limit_platform_tooltip();
    }

    QString with_extract_memory_limit_suffix_if_unsupported(QString const& text) {
        return z7::ui::runtime_support::with_extract_memory_limit_platform_suffix_if_unsupported(text);
    }

    bool large_pages_supported() {
        return z7::ui::runtime_support::large_pages_supported();
    }

    QString large_pages_suffix() {
        return z7::ui::runtime_support::large_pages_platform_suffix();
    }

    QString large_pages_tooltip() {
        return z7::ui::runtime_support::large_pages_platform_tooltip();
    }

    QString with_large_pages_suffix_if_unsupported(QString const& text) {
        return z7::ui::runtime_support::with_large_pages_platform_suffix_if_unsupported(text);
    }

    int find_combo_index_by_data(QComboBox const* combo, QString const& value) {
        if (combo == nullptr) {
            return -1;
        }
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i).toString() == value) {
                return i;
            }
        }
        return -1;
    }

    void select_combo_value_or_insert(QComboBox* combo, QString const& value, QString const& unavailable_suffix) {
        if (combo == nullptr) {
            return;
        }

        int index = find_combo_index_by_data(combo, value);
        if (index < 0 && !value.isEmpty()) {
            combo->addItem(value + unavailable_suffix, value);
            index = combo->count() - 1;
        }
        if (index < 0) {
            index = 0;
        }
        combo->setCurrentIndex(index);
    }

    Qt::HighDpiScaleFactorRoundingPolicy hidpi_policy_from_combo(QComboBox const* combo,
                                                                 Qt::HighDpiScaleFactorRoundingPolicy default_policy) {
        if (combo == nullptr) {
            return default_policy;
        }
        bool ok = false;
        int const value = combo->currentData(Qt::UserRole).toInt(&ok);
        if (!ok) {
            return default_policy;
        }
        switch (static_cast<Qt::HighDpiScaleFactorRoundingPolicy>(value)) {
            case Qt::HighDpiScaleFactorRoundingPolicy::Round:
            case Qt::HighDpiScaleFactorRoundingPolicy::Ceil:
            case Qt::HighDpiScaleFactorRoundingPolicy::Floor:
            case Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor:
            case Qt::HighDpiScaleFactorRoundingPolicy::PassThrough:
                return static_cast<Qt::HighDpiScaleFactorRoundingPolicy>(value);
            case Qt::HighDpiScaleFactorRoundingPolicy::Unset:
            default:
                return default_policy;
        }
    }

    void
    add_hidpi_policy_combo_item(QComboBox* combo, QString const& label, Qt::HighDpiScaleFactorRoundingPolicy policy) {
        if (combo == nullptr) {
            return;
        }
        combo->addItem(label, static_cast<int>(policy));
    }

    quint64 detect_total_ram_bytes() {
        return z7::ui::runtime_support::detect_total_ram_bytes();
    }

    quint64 rounded_ram_gb(quint64 ram_bytes) {
        return z7::ui::runtime_support::rounded_ram_gb(ram_bytes);
    }

    int max_mem_limit_gb(quint64 ram_bytes) {
        return z7::ui::runtime_support::max_extract_memory_limit_gb(ram_bytes);
    }

    QString format_mem_suffix(quint64 ram_bytes) {
        return z7::ui::runtime_support::format_extract_memory_limit_suffix(ram_bytes);
    }

    QStringList const& association_extensions() {
        return z7::ui::common::ordered_archive_suffixes();
    }

    QString format_language_summary_line(z7::ui::runtime_support::LangInfo const& info) {
        int const percent = info.total_lines > 0 ? (info.translated_lines * 100 / info.total_lines) : 0;
        return QStringLiteral("%1 : %2 / %3 = %4%")
            .arg(info.id)
            .arg(info.translated_lines)
            .arg(info.total_lines)
            .arg(percent);
    }

    void append_limited_lines(QStringList* out, QStringList const& lines, QString const& title, int max_lines) {
        if (out == nullptr || lines.isEmpty()) {
            return;
        }

        out->append(QString());
        out->append(QStringLiteral("------ %1: %2 :").arg(title).arg(lines.size()));

        int const shown = std::min(max_lines, static_cast<int>(lines.size()));
        for (int i = 0; i < shown; ++i) {
            out->append(lines.at(i));
        }

        if (shown < lines.size()) {
            out->append(QStringLiteral("..."));
        }
    }

} // namespace z7::ui::filemanager::options_internal
