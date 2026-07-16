// Role: Archive filename-encoding menu model and transactional session reload UI.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {
    namespace {

        struct EncodingMenuEntry {
            char const* region_id;
            char const* region_key;
            uint32_t code_page;
            char const* label;
        };

        constexpr EncodingMenuEntry kEncodingEntries[] = {
            {"arabic", "ui.encoding.region.arabic", 28596, "ISO-8859-6 (28596)"},
            {"arabic", "ui.encoding.region.arabic", 720, "OEM 720"},
            {"arabic", "ui.encoding.region.arabic", 1256, "Windows-1256"},
            {"baltic", "ui.encoding.region.baltic", 28594, "ISO-8859-4 (28594)"},
            {"baltic", "ui.encoding.region.baltic", 28603, "ISO-8859-13 (28603)"},
            {"baltic", "ui.encoding.region.baltic", 775, "OEM 775"},
            {"baltic", "ui.encoding.region.baltic", 1257, "Windows-1257"},
            {"celtic", "ui.encoding.region.celtic", 28604, "ISO-8859-14 (28604)"},
            {"cyrillic", "ui.encoding.region.cyrillic", 28595, "ISO-8859-5 (28595)"},
            {"cyrillic", "ui.encoding.region.cyrillic", 20866, "KOI8-R (20866)"},
            {"cyrillic", "ui.encoding.region.cyrillic", 21866, "KOI8-U (21866)"},
            {"cyrillic", "ui.encoding.region.cyrillic", 10007, "Mac Cyrillic (10007)"},
            {"cyrillic", "ui.encoding.region.cyrillic", 855, "OEM 855"},
            {"cyrillic", "ui.encoding.region.cyrillic", 866, "OEM 866"},
            {"cyrillic", "ui.encoding.region.cyrillic", 1251, "Windows-1251"},
            {"central_european", "ui.encoding.region.central_european", 852, "OEM 852"},
            {"central_european", "ui.encoding.region.central_european", 1250, "Windows-1250"},
            {"chinese", "ui.encoding.region.chinese", 950, "Big5 (950)"},
            {"chinese", "ui.encoding.region.chinese", 936, "GBK (936)"},
            {"chinese", "ui.encoding.region.chinese", 54936, "GB18030 (54936)"},
            {"eastern_european", "ui.encoding.region.eastern_european", 28592, "ISO-8859-2 (28592)"},
            {"greek", "ui.encoding.region.greek", 28597, "ISO-8859-7 (28597)"},
            {"greek", "ui.encoding.region.greek", 737, "OEM 737"},
            {"greek", "ui.encoding.region.greek", 869, "OEM 869"},
            {"greek", "ui.encoding.region.greek", 1253, "Windows-1253"},
            {"hebrew", "ui.encoding.region.hebrew", 28598, "ISO-8859-8 (28598)"},
            {"hebrew", "ui.encoding.region.hebrew", 862, "OEM 862"},
            {"hebrew", "ui.encoding.region.hebrew", 1255, "Windows-1255"},
            {"japanese", "ui.encoding.region.japanese", 932, "Shift-JIS (932)"},
            {"japanese", "ui.encoding.region.japanese", 51932, "EUC-JP (51932)"},
            {"korean", "ui.encoding.region.korean", 949, "Windows-949"},
            {"korean", "ui.encoding.region.korean", 51949, "EUC-KR (51949)"},
            {"north_western_european", "ui.encoding.region.north_western_european", 861, "OEM 861"},
            {"north_western_european", "ui.encoding.region.north_western_european", 865, "OEM 865"},
            {"thai", "ui.encoding.region.thai", 874, "Windows-874"},
            {"turkish", "ui.encoding.region.turkish", 28593, "ISO-8859-3 (28593)"},
            {"turkish", "ui.encoding.region.turkish", 28599, "ISO-8859-9 (28599)"},
            {"turkish", "ui.encoding.region.turkish", 857, "OEM 857"},
            {"turkish", "ui.encoding.region.turkish", 1254, "Windows-1254"},
            {"north_western_european", "ui.encoding.region.north_western_european", 28591, "ISO-8859-1 (28591)"},
            {"north_western_european", "ui.encoding.region.north_western_european", 28605, "ISO-8859-15 (28605)"},
            {"north_western_european", "ui.encoding.region.north_western_european", 850, "OEM 850"},
            {"north_western_european", "ui.encoding.region.north_western_european", 858, "OEM 858"},
            {"north_western_european", "ui.encoding.region.north_western_european", 860, "OEM 860"},
            {"north_western_european", "ui.encoding.region.north_western_european", 863, "OEM 863"},
            {"north_western_european", "ui.encoding.region.north_western_european", 437, "OEM 437"},
            {"north_western_european", "ui.encoding.region.north_western_european", 1252, "Windows-1252"},
            {"vietnamese", "ui.encoding.region.vietnamese", 1258, "Windows-1258"},
        };

        QString encoding_error_message(QString const& summary) {
            if (summary.contains(QStringLiteral("active descendant session"), Qt::CaseInsensitive)) {
                return z7::ui::runtime_support::J(QStringLiteral("ui.encoding.error.active_descendant"));
            }
            if (summary.contains(QStringLiteral("unsupported filename code page"), Qt::CaseInsensitive)) {
                return z7::ui::runtime_support::J(QStringLiteral("ui.encoding.error.unsupported"));
            }
            return summary.trimmed().isEmpty()
                     ? z7::ui::runtime_support::J(QStringLiteral("ui.encoding.error.reload_failed"))
                     : summary;
        }

    } // namespace

    void MainWindow::setup_encoding_menu() {
        encoding_menu_->setObjectName(QStringLiteral("encodingMenu"));
        encoding_action_group_ = new QActionGroup(this);
        encoding_action_group_->setExclusive(true);

        encoding_auto_action_ = encoding_menu_->addAction(QString());
        encoding_auto_action_->setObjectName(QStringLiteral("encodingAutoAction"));
        encoding_auto_action_->setCheckable(true);
        encoding_action_group_->addAction(encoding_auto_action_);
        connect(encoding_auto_action_, &QAction::triggered, this, [this]() {
            on_filename_code_page_requested(std::nullopt);
        });

        encoding_utf8_action_ = encoding_menu_->addAction(QStringLiteral("UTF-8 (65001)"));
        encoding_utf8_action_->setObjectName(QStringLiteral("encodingCodePage65001Action"));
        encoding_utf8_action_->setCheckable(true);
        encoding_utf8_action_->setProperty("filenameCodePage", 65001u);
        encoding_action_group_->addAction(encoding_utf8_action_);
        encoding_code_page_actions_.insert(65001, encoding_utf8_action_);
        connect(encoding_utf8_action_, &QAction::triggered, this, [this]() {
            on_filename_code_page_requested(uint32_t{65001});
        });

        encoding_menu_->addSeparator();
        encoding_charsets_menu_ = encoding_menu_->addMenu(QString());
        encoding_charsets_menu_->setObjectName(QStringLiteral("encodingCharsetsMenu"));

        QHash<QString, QMenu*> region_by_id;
        for (EncodingMenuEntry const& entry : kEncodingEntries) {
            QString const region_id = QString::fromLatin1(entry.region_id);
            QMenu* region_menu = region_by_id.value(region_id, nullptr);
            if (region_menu == nullptr) {
                region_menu = encoding_charsets_menu_->addMenu(QString());
                region_menu->setObjectName(QStringLiteral("encodingRegion_%1").arg(region_id));
                region_menu->setProperty("i18nKey", QString::fromLatin1(entry.region_key));
                region_by_id.insert(region_id, region_menu);
                encoding_region_menus_.push_back(region_menu);
            }

            QAction* const action = region_menu->addAction(QString::fromLatin1(entry.label));
            action->setObjectName(QStringLiteral("encodingCodePage%1Action").arg(entry.code_page));
            action->setCheckable(true);
            action->setProperty("filenameCodePage", entry.code_page);
            encoding_action_group_->addAction(action);
            encoding_code_page_actions_.insert(entry.code_page, action);
            connect(action, &QAction::triggered, this, [this, code_page = entry.code_page]() {
                on_filename_code_page_requested(code_page);
            });
        }

        encoding_menu_->addSeparator();
        encoding_custom_action_ = encoding_menu_->addAction(QString());
        encoding_custom_action_->setObjectName(QStringLiteral("encodingInputCodePageAction"));
        encoding_custom_action_->setCheckable(true);
        encoding_action_group_->addAction(encoding_custom_action_);
        connect(encoding_custom_action_, &QAction::triggered, this, &MainWindow::on_custom_filename_code_page_requested);
    }

    void MainWindow::retranslate_encoding_menu() {
        encoding_menu_->setTitle(z7::ui::runtime_support::J(QStringLiteral("ui.encoding.menu")));
        encoding_auto_action_->setText(z7::ui::runtime_support::J(QStringLiteral("ui.encoding.auto")));
        encoding_charsets_menu_->setTitle(z7::ui::runtime_support::J(QStringLiteral("ui.encoding.charsets")));
        encoding_custom_action_->setText(z7::ui::runtime_support::J(QStringLiteral("ui.encoding.input_code_page")));
        for (QMenu* const menu : encoding_region_menus_) {
            menu->setTitle(z7::ui::runtime_support::J(menu->property("i18nKey").toString()));
        }
    }

    void MainWindow::update_encoding_menu_state() {
        if (encoding_menu_ == nullptr || encoding_auto_action_ == nullptr) {
            return;
        }
        bool const archive_view = in_archive_view();
        encoding_menu_->setEnabled(archive_view);
        z7::app::FilenameCodePage const current =
            archive_view ? active_panel_controller().archive.filename_code_page : z7::app::FilenameCodePage{};

        QAction* checked_action = encoding_auto_action_;
        if (current.has_value()) {
            checked_action = encoding_code_page_actions_.value(*current, encoding_custom_action_);
        }
        checked_action->setChecked(true);

        QString custom_text = z7::ui::runtime_support::J(QStringLiteral("ui.encoding.input_code_page"));
        if (current.has_value() && checked_action == encoding_custom_action_) {
            custom_text += QStringLiteral(" (%1)").arg(*current);
        }
        encoding_custom_action_->setText(custom_text);
    }

    void MainWindow::on_custom_filename_code_page_requested() {
        int initial = 1252;
        if (in_archive_view() && active_panel_controller().archive.filename_code_page.has_value()) {
            initial = static_cast<int>(*active_panel_controller().archive.filename_code_page);
        }
        bool accepted = false;
        int const value = QInputDialog::getInt(
            this,
            z7::ui::runtime_support::J(QStringLiteral("ui.encoding.input_title")),
            z7::ui::runtime_support::J(QStringLiteral("ui.encoding.input_prompt")),
            initial,
            2,
            65535,
            1,
            &accepted);
        if (!accepted) {
            update_encoding_menu_state();
            return;
        }
        on_filename_code_page_requested(static_cast<uint32_t>(value));
    }

    void MainWindow::on_filename_code_page_requested(z7::app::FilenameCodePage code_page) {
        if (!in_archive_view()) {
            update_encoding_menu_state();
            return;
        }
        PanelController const& active_panel = active_panel_controller();
        z7::app::ArchiveSessionToken const token = active_panel.archive.current_token;
        if (!token.is_valid() || active_panel.archive.filename_code_page == code_page) {
            update_encoding_menu_state();
            return;
        }

        QString const caption = z7::ui::runtime_support::J(QStringLiteral("ui.encoding.menu"));
        bool const started = start_task_with_runner(
            caption,
            caption,
            [token, code_page](ArchiveProcessRunner* runner) {
                return runner != nullptr && runner->start_set_session_filename_code_page(token, code_page);
            },
            [this, token, code_page, caption](
                bool ok,
                int,
                int,
                QString const& summary,
                z7::app::OperationOutcome const&) {
                if (!ok) {
                    update_encoding_menu_state();
                    QMessageBox::warning(this, caption, encoding_error_message(summary));
                    return;
                }

                QVector<int> shared_panels;
                for (int panel_index = 0; panel_index < 2; ++panel_index) {
                    PanelController& panel = panel_controller(panel_index);
                    if (panel.in_archive_view() && panel.archive.current_token == token) {
                        panel.archive.filename_code_page = code_page;
                        panel.archive.virtual_dir.clear();
                        shared_panels.push_back(panel_index);
                    }
                }
                reload_archive_virtual_directories_serially(std::move(shared_panels), [this]() {
                    update_encoding_menu_state();
                    sync_path_bar_from_current_dir();
                    update_window_title();
                });
            },
            RunnerTaskUiMode::kSilent,
            [](int, QString const&) { return false; });
        if (!started) {
            update_encoding_menu_state();
        }
    }

} // namespace z7::ui::filemanager
