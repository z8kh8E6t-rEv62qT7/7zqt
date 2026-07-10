// src/ui/filemanager/src/main_window/actions/action_properties.cpp
// Role: Properties dialog logic and table rendering helpers.

#include <QDirIterator>
#include <optional>

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

    namespace {

        QString format_grouped_uint64(uint64_t const value) {
            QString const digits = QString::number(value);
            if (digits.size() <= 3) {
                return digits;
            }

            QString out;
            out.reserve(digits.size() + digits.size() / 3);
            int const first_group = (digits.size() % 3 == 0) ? 3 : (digits.size() % 3);
            out += digits.left(first_group);
            for (int i = first_group; i < digits.size(); i += 3) {
                out += QLatin1Char(' ');
                out += digits.mid(i, 3);
            }
            return out;
        }

        QString localized_property_name(std::optional<uint32_t> const& prop_id, QString const& native_name) {
            if (prop_id.has_value()) {
                uint32_t const lang_id = *prop_id < 1000u ? (1000u + *prop_id) : *prop_id;
                return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(lang_id));
            }

            if (!native_name.trimmed().isEmpty()) {
                return native_name;
            }
            return QString();
        }

        QString localized_text(uint32_t const lang_id) {
            return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(lang_id));
        }

        QString localized_selected_items_text(int const count) {
            return z7::ui::runtime_support::LF(3002, {QString::number(count)});
        }

        bool is_archive_selection_summary_count_line(z7::app::ArchivePropertyLine const& line) {
            return line.kind == z7::app::PropertyLineKind::kPair
                && line.display_group == z7::app::ArchivePropertyDisplayGroup::kSelectionSummary
                && !line.prop_id.has_value()
                && line.name.empty();
        }

        struct FilesystemPropertyStats {
            uint64_t folders = 0;
            uint64_t files = 0;
            uint64_t size = 0;
        };

        void accumulate_directory_stats(QString const& root_path, FilesystemPropertyStats* stats) {
            if (stats == nullptr) {
                return;
            }

            ++stats->folders;
            QDirIterator it(root_path,
                            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                QFileInfo const child = it.fileInfo();
                if (child.isDir()) {
                    ++stats->folders;
                    continue;
                }
                if (child.isFile()) {
                    ++stats->files;
                    stats->size += static_cast<uint64_t>(child.size());
                }
            }
        }

        FilesystemPropertyStats collect_filesystem_stats(QFileInfo const& info) {
            FilesystemPropertyStats stats;
            if (info.isDir()) {
                accumulate_directory_stats(info.absoluteFilePath(), &stats);
                return stats;
            }

            stats.files = 1;
            stats.size = static_cast<uint64_t>(info.size());
            return stats;
        }

        QString format_filesystem_timestamp(QDateTime const& timestamp) {
            if (!timestamp.isValid()) {
                return QString();
            }
            return timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        }

    } // namespace

    void MainWindow::show_properties_dialog() {
        PanelController const& panel = active_panel_controller();
        if (panel.ui.details_view == nullptr
            || panel.ui.details_view->selectionModel() == nullptr
            || panel.model == nullptr) {
            return;
        }
        bool const archive_mode = in_archive_view();
        if (!archive_mode && active_selected_rows_include_parent_link()) {
            return;
        }

        QModelIndexList const selected_rows = panel.selected_rows_including_parent_link();

        QModelIndexList rows;
        rows.reserve(selected_rows.size());
        for (QModelIndex const& index : selected_rows) {
            if (!index.isValid() || panel.model->is_parent_link_for_row(index.row())) {
                continue;
            }
            rows << index;
        }
        if (!archive_mode && rows.isEmpty()) {
            return;
        }

        QVector<PropertiesDialogRow> dialog_rows;
        auto const append_pair_to = [](QVector<PropertiesDialogRow>* rows, QString const& key, QString const& value) {
            if (rows == nullptr || value.trimmed().isEmpty()) {
                return;
            }
            PropertiesDialogRow row;
            row.kind = PropertiesDialogRow::Kind::kPair;
            row.key = key;
            row.value = value;
            rows->push_back(std::move(row));
        };
        auto const append_separator_to = [](QVector<PropertiesDialogRow>* rows, bool const small = false) {
            if (rows == nullptr) {
                return;
            }
            PropertiesDialogRow row;
            row.kind = small ? PropertiesDialogRow::Kind::kSeparatorSmall : PropertiesDialogRow::Kind::kSeparator;
            rows->push_back(std::move(row));
        };
        auto const append_selection_summary_count_to = [append_pair_to](QVector<PropertiesDialogRow>* rows,
                                                                        int const count) {
            append_pair_to(rows, QString(), localized_selected_items_text(count));
        };
        auto const append_archive_properties_rows =
            [append_pair_to, append_separator_to](z7::app::ArchivePropertiesResult const& result,
                                                  QVector<PropertiesDialogRow>* rows) {
                for (z7::app::ArchivePropertyLine const& line : result.lines) {
                    if (line.kind == z7::app::PropertyLineKind::kSeparator) {
                        append_separator_to(rows, false);
                        continue;
                    }
                    if (line.kind == z7::app::PropertyLineKind::kSeparatorSmall) {
                        append_separator_to(rows, true);
                        continue;
                    }

                    QString const native_name = z7::ui::archive_support::from_utf8_string(line.name);
                    QString value = z7::ui::archive_support::from_utf8_string(line.value);
                    if (is_archive_selection_summary_count_line(line)) {
                        bool ok = false;
                        int const count = value.toInt(&ok);
                        value = ok ? localized_selected_items_text(count) : z7::ui::runtime_support::LF(3002, {value});
                    }
                    append_pair_to(rows, localized_property_name(line.prop_id, native_name), value);
                }
            };
        auto const append_filesystem_single_selection_rows =
            [append_pair_to, append_separator_to](QFileInfo const& info, QVector<PropertiesDialogRow>* rows) {
                bool const is_dir = info.isDir();
                FilesystemPropertyStats const stats = collect_filesystem_stats(info);
                QDateTime const created = info.birthTime().isValid() ? info.birthTime() : info.lastModified();

                append_pair_to(rows, localized_text(1004), info.fileName());
                append_pair_to(rows, localized_text(1020), is_dir ? localized_text(1006) : localized_text(500));
                append_pair_to(rows, localized_text(1032), format_grouped_uint64(stats.files));
                append_pair_to(rows, localized_text(1031), format_grouped_uint64(stats.folders));
                append_pair_to(rows, localized_text(1007), format_grouped_uint64(stats.size));
                append_separator_to(rows, false);
                append_pair_to(rows, localized_text(1003), QDir::toNativeSeparators(info.absoluteFilePath()));
                append_pair_to(rows, localized_text(1010), format_filesystem_timestamp(created));
                append_pair_to(rows, localized_text(1012), format_filesystem_timestamp(info.lastModified()));
            };
        auto const append_filesystem_multi_selection_rows =
            [append_pair_to, append_selection_summary_count_to](QStringList const& paths,
                                                                QVector<PropertiesDialogRow>* rows) {
                append_selection_summary_count_to(rows, paths.size());

                FilesystemPropertyStats aggregate;
                for (QString const& path : paths) {
                    QFileInfo const info(path);
                    if (info.isDir()) {
                        accumulate_directory_stats(path, &aggregate);
                        continue;
                    }
                    ++aggregate.files;
                    aggregate.size += static_cast<uint64_t>(info.size());
                }

                append_pair_to(rows, localized_text(1031), format_grouped_uint64(aggregate.folders));
                append_pair_to(rows, localized_text(1032), format_grouped_uint64(aggregate.files));
                append_pair_to(rows, localized_text(1007), format_grouped_uint64(aggregate.size));
            };

        QString const title = localized_text(6600);
        if (archive_mode) {
            if (panel.archive.source_archive.trimmed().isEmpty()
                || (!panel.archive.current_token.is_valid() && !QFileInfo(panel.archive.source_archive).exists())) {
                return;
            }

            QStringList selected_entries;
            selected_entries.reserve(rows.size());
            for (QModelIndex const& index : rows) {
                QString const path =
                    z7::ui::archive_support::normalize_virtual_dir(panel.model->path_for_row(index.row()));
                if (!path.isEmpty()) {
                    selected_entries << path;
                }
            }
            selected_entries.removeDuplicates();

            QPointer<MainWindow> main_window(this);
            QString const source_archive = panel.archive.source_archive;
            QString const virtual_dir = panel.archive.virtual_dir;
            bool const flat_view = panel.model->flat_view();
            QString const type_hint = panel.archive.type_hint;
            z7::app::ArchiveSessionToken const session_token = panel.archive.current_token;
            start_task_with_runner(
                title,
                localized_text(541),
                [source_archive, selected_entries, virtual_dir, flat_view, type_hint, session_token](
                    ArchiveProcessRunner* runner) {
                    return runner != nullptr
                        && runner->start_archive_properties(
                            source_archive, selected_entries, virtual_dir, flat_view, type_hint, session_token);
                },
                [main_window, title, append_archive_properties_rows](
                    bool ok, int, int, QString const&, z7::app::OperationOutcome const& outcome) {
                    if (main_window.isNull() || !ok) {
                        return;
                    }
                    auto const properties_result =
                        z7::app::outcome_payload_as<z7::app::ArchivePropertiesResult>(outcome);
                    if (!properties_result.has_value() || !properties_result->ok) {
                        return;
                    }

                    QVector<PropertiesDialogRow> rows_for_dialog;
                    append_archive_properties_rows(*properties_result, &rows_for_dialog);
                    main_window->show_properties_table(title, rows_for_dialog);
                },
                RunnerTaskUiMode::kSilent);
            return;
        }

        QStringList paths;
        paths.reserve(rows.size());
        for (QModelIndex const& index : rows) {
            QString const path = panel.model->path_for_row(index.row());
            if (!path.isEmpty()) {
                paths << path;
            }
        }
        if (paths.isEmpty()) {
            return;
        }

        if (paths.size() == 1) {
            append_filesystem_single_selection_rows(QFileInfo(paths.front()), &dialog_rows);
        } else {
            append_filesystem_multi_selection_rows(paths, &dialog_rows);
        }

        show_properties_table(title, dialog_rows);
    }

    void MainWindow::show_properties_table(QString const& title, QVector<PropertiesDialogRow> const& rows) {
        if (rows.isEmpty()) {
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(title);
#ifdef Z7_TESTING
        dialog.setObjectName(QStringLiteral("propertiesDialog"));
#endif
        dialog.resize(820, 520);

        auto* layout = new QVBoxLayout(&dialog);
        auto* table = new QTableWidget(rows.size(), 2, &dialog);
#ifdef Z7_TESTING
        table->setObjectName(QStringLiteral("propertiesTable"));
#endif
        table->setHorizontalHeaderLabels({QString(), QString()});
        table->horizontalHeader()->setVisible(true);
        table->horizontalHeader()->setSectionsClickable(false);
        table->horizontalHeader()->setHighlightSections(false);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setWordWrap(false);
        table->setShowGrid(false);

        for (int i = 0; i < rows.size(); ++i) {
            auto const& row = rows.at(i);
            if (row.kind == PropertiesDialogRow::Kind::kSeparator
                || row.kind == PropertiesDialogRow::Kind::kSeparatorSmall) {
                QString const text = row.key.isEmpty() ? (row.kind == PropertiesDialogRow::Kind::kSeparatorSmall
                                                              ? QStringLiteral("----------------")
                                                              : QStringLiteral("------------------------"))
                                                       : row.key;
                auto* sep_key_item = new QTableWidgetItem(text);
                auto* sep_value_item = new QTableWidgetItem(QString());
                sep_key_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                sep_value_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                table->setItem(i, 0, sep_key_item);
                table->setItem(i, 1, sep_value_item);
                continue;
            }

            auto* key_item = new QTableWidgetItem(row.key);
            auto* value_item = new QTableWidgetItem(row.value);
            key_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            value_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            table->setItem(i, 0, key_item);
            table->setItem(i, 1, value_item);
        }
        table->resizeColumnsToContents();
        table->horizontalHeader()->resizeSection(0, table->columnWidth(0) + 12);
        table->resizeRowsToContents();
        layout->addWidget(table);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
        z7::platform::qt::apply_dialog_button_baseline(buttons);
        if (auto* ok = buttons->button(QDialogButtonBox::Ok)) {
            ok->setText(z7::ui::runtime_support::L(401));
        }
        if (auto* cancel = buttons->button(QDialogButtonBox::Cancel)) {
            cancel->setText(z7::ui::runtime_support::L(402));
        }
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        dialog.exec();
    }

} // namespace z7::ui::filemanager
