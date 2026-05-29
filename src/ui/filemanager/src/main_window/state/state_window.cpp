// src/ui/filemanager/src/main_window/state/state_window.cpp
// Role: MainWindow state persistence and panel accessors.

#include "main_window/deps.h"
#include "main_window/internal.h"

#include <limits>

namespace z7::ui::filemanager
{

    namespace
    {
        std::optional<int> read_int_setting(
            z7::platform::qt::PortableSettings const& settings,
            char const* key)
        {
            bool ok = false;
            int const value =
                settings.value(QString::fromLatin1(key)).toInt(&ok);
            if (!ok)
            {
                return std::nullopt;
            }
            return value;
        }

        void append_u32_le(QByteArray* out, quint32 value)
        {
            if (out == nullptr)
            {
                return;
            }
            out->append(static_cast<char>(value & 0xFFu));
            out->append(static_cast<char>((value >> 8) & 0xFFu));
            out->append(static_cast<char>((value >> 16) & 0xFFu));
            out->append(static_cast<char>((value >> 24) & 0xFFu));
        }

        std::optional<quint32> read_u32_le(QByteArray const& bytes, int offset)
        {
            if (offset < 0 || bytes.size() < offset + 4)
            {
                return std::nullopt;
            }
            return static_cast<quint32>(static_cast<unsigned char>(bytes.at(offset))) |
                   (static_cast<quint32>(static_cast<unsigned char>(bytes.at(offset + 1))) << 8) |
                   (static_cast<quint32>(static_cast<unsigned char>(bytes.at(offset + 2))) << 16) |
                   (static_cast<quint32>(static_cast<unsigned char>(bytes.at(offset + 3))) << 24);
        }

        int signed_i32_from_u32(quint32 value)
        {
            if (value <= 0x7fffffffu)
            {
                return static_cast<int>(value);
            }
            return static_cast<int>(static_cast<qint64>(value) - 0x100000000LL);
        }

        quint32 u32_from_signed_i32(int value)
        {
            return static_cast<quint32>(static_cast<qint64>(value) & 0xffffffffLL);
        }

        constexpr int kPositionPayloadSize = 5 * 4;
        constexpr int kPanelsPayloadSize = 3 * 4;
        constexpr int kListViewHeaderSize = 3 * 4;
        constexpr int kColumnInfoSize = 3 * 4;
        constexpr quint32 kListViewVersion = 1;
        constexpr quint32 kNoSortColumn = 0xffffffffu;

        int intersection_area(QRect const& left, QRect const& right)
        {
            QRect const intersection = left.intersected(right);
            if (intersection.isEmpty())
            {
                return 0;
            }
            return intersection.width() * intersection.height();
        }

        std::optional<QRect> fit_window_geometry_to_available_screens(
            QRect const& requested)
        {
            if (!requested.isValid() || requested.width() <= 0 ||
                requested.height() <= 0)
            {
                return std::nullopt;
            }

            QScreen const* best_screen = nullptr;
            int best_area = 0;
            for (QScreen const* screen : QGuiApplication::screens())
            {
                if (screen == nullptr)
                {
                    continue;
                }
                QRect const available = screen->availableGeometry();
                int const area = intersection_area(requested, available);
                if (area > best_area)
                {
                    best_area = area;
                    best_screen = screen;
                }
            }
            if (best_screen == nullptr || best_area <= 0)
            {
                return std::nullopt;
            }

            QRect const available = best_screen->availableGeometry();
            QSize const size(qMin(requested.width(), available.width()),
                             qMin(requested.height(), available.height()));
            if (size.width() <= 0 || size.height() <= 0)
            {
                return std::nullopt;
            }

            int const min_x = available.x();
            int const min_y = available.y();
            int const max_x = min_x + available.width() - size.width();
            int const max_y = min_y + available.height() - size.height();
            QPoint const top_left(qBound(min_x, requested.x(), qMax(min_x, max_x)),
                                  qBound(min_y, requested.y(), qMax(min_y, max_y)));
            return QRect(top_left, size);
        }

        struct DetailsColumnSectionState
        {
            int logical = -1;
            bool visible = true;
            int width = column_width_persistence::kMinColumnWidth;
            int visual = -1;
        };

        struct DetailsColumnState
        {
            QVector<DetailsColumnSectionState> sections;
            int sort_section = DirectoryListModel::kNameColumn;
            Qt::SortOrder sort_order = Qt::AscendingOrder;
        };

        int default_sort_action_for_column(int column)
        {
            switch (column)
            {
                case DirectoryListModel::kNameColumn:
                    return kSortActionName;
                case DirectoryListModel::kTypeSortColumn:
                    return kSortActionType;
                case DirectoryListModel::kModifiedColumn:
                    return kSortActionDate;
                case DirectoryListModel::kSizeColumn:
                    return kSortActionSize;
                case -1:
                    return kSortActionUnsorted;
                default:
                    return -1;
            }
        }

        QByteArray encode_details_column_state(QHeaderView const* header,
                                               int expected_count)
        {
            if (header == nullptr || expected_count <= 0 ||
                header->count() < expected_count)
            {
                return {};
            }

            QByteArray encoded;
            encoded.reserve(kListViewHeaderSize + kColumnInfoSize * expected_count);
            append_u32_le(&encoded, kListViewVersion);
            int const sort_section = header->sortIndicatorSection();
            append_u32_le(&encoded,
                          sort_section < 0
                              ? kNoSortColumn
                              : static_cast<quint32>(sort_section));
            append_u32_le(&encoded,
                          header->sortIndicatorOrder() == Qt::AscendingOrder
                              ? 1u
                              : 0u);

            QVector<DetailsColumnSectionState> sections;
            sections.reserve(expected_count);
            for (int logical = 0; logical < expected_count; ++logical)
            {
                DetailsColumnSectionState section;
                section.logical = logical;
                section.visible = !header->isSectionHidden(logical);
                section.width = column_width_persistence::clamp_column_width(
                    header->sectionSize(logical));
                section.visual = header->visualIndex(logical);
                sections.push_back(section);
            }
            std::sort(sections.begin(),
                      sections.end(),
                      [](DetailsColumnSectionState const& left,
                         DetailsColumnSectionState const& right)
                      {
                          return left.visual < right.visual;
                      });

            for (DetailsColumnSectionState const& section : sections)
            {
                append_u32_le(&encoded, static_cast<quint32>(section.logical));
                append_u32_le(&encoded, section.visible ? 1u : 0u);
                append_u32_le(&encoded, static_cast<quint32>(section.width));
            }
            return encoded;
        }

        DetailsColumnState default_details_column_state(int expected_count,
                                                        QVector<int> const& defaults)
        {
            DetailsColumnState state;
            state.sections.reserve(expected_count);
            for (int logical = 0; logical < expected_count; ++logical)
            {
                DetailsColumnSectionState section;
                section.logical = logical;
                section.visible = true;
                section.visual = logical;
                if (logical < defaults.size())
                {
                    section.width =
                        column_width_persistence::clamp_column_width(defaults.at(logical));
                }
                state.sections.push_back(section);
            }
            return state;
        }

        bool decode_details_column_state(QByteArray const& encoded,
                                         int expected_count,
                                         QVector<int> const& defaults,
                                         DetailsColumnState* state_out)
        {
            if (state_out == nullptr || expected_count <= 0 ||
                encoded.size() < kListViewHeaderSize ||
                (encoded.size() - kListViewHeaderSize) % kColumnInfoSize != 0 ||
                encoded.size() > kListViewHeaderSize + kColumnInfoSize * 1000)
            {
                return false;
            }

            std::optional<quint32> const version = read_u32_le(encoded, 0);
            std::optional<quint32> const sort_id = read_u32_le(encoded, 4);
            std::optional<quint32> const ascending = read_u32_le(encoded, 8);
            if (!version.has_value() || *version != kListViewVersion ||
                !sort_id.has_value() || !ascending.has_value() ||
                (*ascending != 0u && *ascending != 1u))
            {
                return false;
            }

            int sort_section = -1;
            if (*sort_id != kNoSortColumn)
            {
                if (*sort_id >= static_cast<quint32>(expected_count))
                {
                    return false;
                }
                sort_section = static_cast<int>(*sort_id);
            }

            DetailsColumnState state =
                default_details_column_state(expected_count, defaults);
            state.sort_section = sort_section;
            state.sort_order = *ascending != 0u ? Qt::AscendingOrder
                                                : Qt::DescendingOrder;
            QVector<bool> seen_logical(expected_count, false);
            QVector<DetailsColumnSectionState> ordered_known;
            ordered_known.reserve(expected_count);
            int const stored_count =
                (encoded.size() - kListViewHeaderSize) / kColumnInfoSize;
            for (int i = 0; i < stored_count; ++i)
            {
                int const offset = kListViewHeaderSize + i * kColumnInfoSize;
                std::optional<quint32> const prop_id = read_u32_le(encoded, offset);
                std::optional<quint32> const visible = read_u32_le(encoded, offset + 4);
                std::optional<quint32> const width = read_u32_le(encoded, offset + 8);
                if (!prop_id.has_value() || !visible.has_value() ||
                    !width.has_value() || (*visible != 0u && *visible != 1u))
                {
                    return false;
                }

                if (*prop_id >= static_cast<quint32>(expected_count))
                {
                    continue;
                }

                int const logical = static_cast<int>(*prop_id);
                if (seen_logical.at(logical))
                {
                    return false;
                }
                seen_logical[logical] = true;
                DetailsColumnSectionState& section = state.sections[logical];
                section.visible = *visible != 0u;
                int const stored_width =
                    *width > static_cast<quint32>(std::numeric_limits<int>::max())
                        ? std::numeric_limits<int>::max()
                        : static_cast<int>(*width);
                section.width = column_width_persistence::clamp_column_width(
                    stored_width);
                ordered_known.push_back(section);
            }

            int visual = 0;
            for (DetailsColumnSectionState& section : ordered_known)
            {
                section.visual = visual++;
            }
            for (DetailsColumnSectionState const& section : state.sections)
            {
                if (!seen_logical.at(section.logical))
                {
                    DetailsColumnSectionState appended = section;
                    appended.visual = visual++;
                    ordered_known.push_back(appended);
                }
            }
            std::sort(ordered_known.begin(),
                      ordered_known.end(),
                      [](DetailsColumnSectionState const& left,
                         DetailsColumnSectionState const& right)
                      {
                          return left.logical < right.logical;
                      });
            state.sections = ordered_known;
            *state_out = state;
            return true;
        }

        void apply_details_column_state(DragAwareStructuredListView* view,
                                        DetailsColumnState const& state,
                                        int expected_count,
                                        int* active_sort_action_out)
        {
            if (view == nullptr || view->horizontalHeader() == nullptr ||
                expected_count <= 0)
            {
                return;
            }

            QHeaderView* header = view->horizontalHeader();
            for (DetailsColumnSectionState const& section : state.sections)
            {
                if (section.logical < 0 || section.logical >= expected_count)
                {
                    continue;
                }
                header->resizeSection(section.logical, section.width);
                const bool visible =
                    section.logical == DirectoryListModel::kNameColumn
                        ? true
                        : section.visible;
                view->setColumnHidden(section.logical, !visible);
            }

            QVector<DetailsColumnSectionState> ordered = state.sections;
            std::sort(ordered.begin(),
                      ordered.end(),
                      [](DetailsColumnSectionState const& left,
                         DetailsColumnSectionState const& right)
                      {
                          return left.visual < right.visual;
                      });
            for (int target_visual = 0; target_visual < ordered.size();
                 ++target_visual)
            {
                int const logical = ordered.at(target_visual).logical;
                int const current_visual = header->visualIndex(logical);
                if (current_visual >= 0 && current_visual != target_visual)
                {
                    header->moveSection(current_visual, target_visual);
                }
            }

            if (active_sort_action_out != nullptr)
            {
                *active_sort_action_out =
                    default_sort_action_for_column(state.sort_section);
            }
            if (state.sort_section < 0)
            {
                header->setSortIndicator(-1, Qt::AscendingOrder);
                view->setSortingEnabled(false);
                view->setSortingEnabled(true);
            }
            else
            {
                view->sortByColumn(state.sort_section, state.sort_order);
            }
        }

        QVector<z7::app::ArchiveSessionToken> dedupe_archive_session_tokens(
            QVector<z7::app::ArchiveSessionToken> tokens)
        {
            QSet<quint64> seen;
            QVector<z7::app::ArchiveSessionToken> deduped;
            deduped.reserve(tokens.size());
            for (z7::app::ArchiveSessionToken const token : tokens)
            {
                if (!token.is_valid() || seen.contains(token.value))
                {
                    continue;
                }
                seen.insert(token.value);
                deduped.push_back(token);
            }
            return deduped;
        }

        class DetachedArchiveSessionCloser final : public QObject
        {
        public:
            static void dispatch(QVector<z7::app::ArchiveSessionToken> tokens)
            {
                QVector<z7::app::ArchiveSessionToken> const pending =
                    dedupe_archive_session_tokens(std::move(tokens));
                if (pending.isEmpty())
                {
                    return;
                }

                auto* closer = new DetachedArchiveSessionCloser(pending);
                closer->start_next();
            }

        private:
            explicit DetachedArchiveSessionCloser(QVector<z7::app::ArchiveSessionToken> tokens)
                : tokens_(std::move(tokens))
                , runner_(new ArchiveProcessRunner(this))
            {
                connect(runner_, &ArchiveProcessRunner::finished, this, [this](bool, int, int, QString const&)
                        { start_next(); });
            }

            void start_next()
            {
                if (next_token_index_ >= tokens_.size())
                {
                    deleteLater();
                    return;
                }

                z7::app::ArchiveSessionToken const token = tokens_.at(next_token_index_++);
                // ArchiveProcessRunner emits finished() even when startup fails
                // immediately, so the finished handler remains the single place
                // that advances the queue.
                (void)runner_->start_close_session(token);
            }

            QVector<z7::app::ArchiveSessionToken> tokens_;
            int next_token_index_ = 0;
            ArchiveProcessRunner* runner_ = nullptr;
        };

    } // namespace

    FmPanelsState read_fm_panels_state(
        z7::platform::qt::PortableSettings const& settings)
    {
        FmPanelsState state;
        QByteArray const payload =
            settings.value(QString::fromLatin1(kSettingsFmPanels), QByteArray())
                .toByteArray();
        if (payload.size() != kPanelsPayloadSize)
        {
            return state;
        }

        std::optional<quint32> const num_panels = read_u32_le(payload, 0);
        std::optional<quint32> const current_panel = read_u32_le(payload, 4);
        std::optional<quint32> const splitter_pos = read_u32_le(payload, 8);
        if (!num_panels.has_value() || !current_panel.has_value() ||
            !splitter_pos.has_value())
        {
            return state;
        }

        state.present = true;
        state.two_panels = *num_panels >= 2u;
        state.active_panel = *current_panel == 1u ? 1 : 0;
        state.splitter_pos = *splitter_pos >
                                     static_cast<quint32>(
                                         std::numeric_limits<int>::max())
                                 ? std::numeric_limits<int>::max()
                                 : static_cast<int>(*splitter_pos);
        return state;
    }

    void write_fm_panels_state(z7::platform::qt::PortableSettings* settings,
                               bool two_panels,
                               int active_panel,
                               int splitter_pos)
    {
        if (settings == nullptr)
        {
            return;
        }

        QByteArray payload;
        payload.reserve(kPanelsPayloadSize);
        append_u32_le(&payload, two_panels ? 2u : 1u);
        append_u32_le(&payload, active_panel == 1 ? 1u : 0u);
        append_u32_le(&payload,
                      static_cast<quint32>(qMax(0, splitter_pos)));
        settings->setValue(QString::fromLatin1(kSettingsFmPanels), payload);
    }

    int current_fm_splitter_pos(QSplitter const* splitter)
    {
        if (splitter == nullptr)
        {
            return 0;
        }
        QList<int> const sizes = splitter->sizes();
        if (sizes.isEmpty())
        {
            return 0;
        }
        return qMax(0, sizes.at(0));
    }

    void MainWindow::restore_main_window_geometry()
    {
        z7::platform::qt::PortableSettings settings;
        QByteArray const payload =
            settings.value(QString::fromLatin1(kSettingsFmPosition), QByteArray())
                .toByteArray();
        if (payload.size() != kPositionPayloadSize)
        {
            return;
        }

        std::optional<quint32> const left_raw = read_u32_le(payload, 0);
        std::optional<quint32> const top_raw = read_u32_le(payload, 4);
        std::optional<quint32> const right_raw = read_u32_le(payload, 8);
        std::optional<quint32> const bottom_raw = read_u32_le(payload, 12);
        std::optional<quint32> const maximized_raw = read_u32_le(payload, 16);
        if (!left_raw.has_value() || !top_raw.has_value() ||
            !right_raw.has_value() || !bottom_raw.has_value() ||
            !maximized_raw.has_value())
        {
            return;
        }

        int const left = signed_i32_from_u32(*left_raw);
        int const top = signed_i32_from_u32(*top_raw);
        int const right = signed_i32_from_u32(*right_raw);
        int const bottom = signed_i32_from_u32(*bottom_raw);
        QRect const requested(QPoint(left, top),
                              QSize(right - left, bottom - top));
        std::optional<QRect> const restored =
            fit_window_geometry_to_available_screens(requested);
        if (restored.has_value())
        {
            setGeometry(*restored);
        }
        if (*maximized_raw != 0u)
        {
            setWindowState((windowState() & ~Qt::WindowMinimized) |
                           Qt::WindowMaximized);
        }
    }

    void MainWindow::save_main_window_geometry() const
    {
        QRect geometry = normalGeometry();
        if (!geometry.isValid() || geometry.width() <= 0 || geometry.height() <= 0)
        {
            geometry = this->geometry();
        }
        if (!geometry.isValid() || geometry.width() <= 0 || geometry.height() <= 0)
        {
            return;
        }

        z7::platform::qt::PortableSettings settings;
        QByteArray payload;
        payload.reserve(kPositionPayloadSize);
        append_u32_le(&payload, u32_from_signed_i32(geometry.x()));
        append_u32_le(&payload, u32_from_signed_i32(geometry.y()));
        append_u32_le(&payload, u32_from_signed_i32(geometry.x() + geometry.width()));
        append_u32_le(&payload, u32_from_signed_i32(geometry.y() + geometry.height()));
        append_u32_le(&payload, isMaximized() ? 1u : 0u);
        settings.setValue(QString::fromLatin1(kSettingsFmPosition), payload);
    }

    QVector<int> MainWindow::default_details_column_widths()
    {
        return {
            160, // Name
            100, // Size
            100, // Packed
            150, // Modified
            150, // Created
            150, // Accessed
            90,  // Attributes
            60,  // Encrypted
            120, // Comment
            100, // CRC
            110, // Method
            100, // Characts
            110, // Host OS
            80,  // Version
            80,  // Volume Index
            100, // Offset
            80,  // Folders
            80   // Files
        };
    }

    void MainWindow::load_details_column_state()
    {
        z7::platform::qt::PortableSettings settings;
        QVector<int> const defaults = default_details_column_widths();
        std::array<char const*, 2> const keys = {
            kSettingsFmColumnsPanel0,
            kSettingsFmColumnsPanel1};

        for (int i = 0; i < 2; ++i)
        {
            if (panels_[i].ui.details_view == nullptr)
            {
                continue;
            }
            auto* header = panels_[i].ui.details_view->horizontalHeader();
            if (header == nullptr)
            {
                continue;
            }

            QVector<int> widths;
            QVariant const encoded =
                settings.value(QString::fromLatin1(keys[static_cast<size_t>(i)]), QVariant());
            DetailsColumnState state;
            if (decode_details_column_state(encoded.toByteArray(),
                                            DirectoryListModel::kColumnCount,
                                            defaults,
                                            &state))
            {
                apply_details_column_state(panels_[i].ui.details_view,
                                           state,
                                           DirectoryListModel::kColumnCount,
                                           &panels_[i].runtime.active_sort_action);
            }
            else
            {
                widths = defaults;
                column_width_persistence::apply_widths(header, widths);
            }
            apply_archive_preview_columns_visibility_for_panel(i);
        }
    }

    void MainWindow::save_details_column_state() const
    {
        z7::platform::qt::PortableSettings settings;
        std::array<char const*, 2> const keys = {
            kSettingsFmColumnsPanel0,
            kSettingsFmColumnsPanel1};

        for (int i = 0; i < 2; ++i)
        {
            DragAwareStructuredListView const* details_view = panels_[i].ui.details_view;
            if (details_view == nullptr || details_view->horizontalHeader() == nullptr)
            {
                continue;
            }
            QByteArray const encoded = encode_details_column_state(
                details_view->horizontalHeader(),
                DirectoryListModel::kColumnCount);
            if (encoded.isEmpty())
            {
                continue;
            }
            settings.setValue(QString::fromLatin1(keys[static_cast<size_t>(i)]),
                              encoded);
        }
    }

    void MainWindow::restore_panel_ui_state_from_settings()
    {
        z7::platform::qt::PortableSettings settings;

        if (std::optional<int> const list_mode =
                read_int_setting(settings, kSettingsFmListMode);
            list_mode.has_value())
        {
            for (int i = 0; i < 2; ++i)
            {
                int const view_mode = (*list_mode >> (i * 8)) & 0xFF;
                apply_view_mode_to_panel(i, view_mode);
            }
        }

        std::array<char const*, 2> const flat_keys = {
            kSettingsFmFlatViewArc0,
            kSettingsFmFlatViewArc1};
        for (int i = 0; i < 2; ++i)
        {
            if (panels_[i].model == nullptr)
            {
                continue;
            }
            QString const key = QString::fromLatin1(flat_keys[static_cast<size_t>(i)]);
            if (settings.contains(key))
            {
                panels_[i].model->set_flat_view(settings.value(key, false).toBool());
            }
        }

        FmPanelsState const panels_state = read_fm_panels_state(settings);
        if (two_panels_visible_ && panels_splitter_ != nullptr)
        {
            int const splitter_pos = panels_state.splitter_pos;
            if (splitter_pos > 0)
            {
                int const total_width =
                    panels_splitter_->width() > splitter_pos
                        ? panels_splitter_->width()
                        : kDefaultMainWindowWidth;
                panels_splitter_->setSizes(
                    {splitter_pos, qMax(1, total_width - splitter_pos)});
            }
        }

        set_active_panel(panels_state.present ? panels_state.active_panel : 0);
        update_view_menu_checks();
        update_status();
    }

    void MainWindow::save_panel_ui_state() const
    {
        z7::platform::qt::PortableSettings settings;

        int list_mode = 0;
        for (int i = 0; i < 2; ++i)
        {
            int const mode = static_cast<int>(panels_[i].view_mode);
            list_mode |= (mode & 0xFF) << (i * 8);
        }
        settings.setValue(QString::fromLatin1(kSettingsFmListMode), list_mode);
        write_fm_panels_state(&settings,
                              two_panels_visible_,
                              active_panel_index_,
                              current_fm_splitter_pos(panels_splitter_));

        std::array<char const*, 2> const flat_keys = {
            kSettingsFmFlatViewArc0,
            kSettingsFmFlatViewArc1};
        for (int i = 0; i < 2; ++i)
        {
            if (panels_[i].model == nullptr)
            {
                continue;
            }
            settings.setValue(QString::fromLatin1(flat_keys[static_cast<size_t>(i)]),
                              panels_[i].model->flat_view());
        }

        settings.sync();
    }

    QVector<z7::app::ArchiveSessionToken> MainWindow::run_shutdown_cleanup_once()
    {
        if (shutdown_cleanup_started_)
        {
            return {};
        }
        shutdown_cleanup_started_ = true;

        save_panel_ui_state();
        save_panel_paths();
        save_folder_history();
        save_main_window_geometry();
        save_details_column_state();
        QVector<QSharedPointer<ArchiveTempSession>> const sessions = archive_temp_sessions_;
        for (QSharedPointer<ArchiveTempSession> const& session : sessions)
        {
            finalize_archive_temp_session(session);
        }

        QVector<z7::app::ArchiveSessionToken> tokens_to_close;
        for (int i = 0; i < 2; ++i)
        {
            panels_[i].clear_archive_view_state([this](QSharedPointer<ArchiveTempSession> const& session)
                                                { release_archive_temp_session(session); },
                                                [&tokens_to_close](z7::app::ArchiveSessionToken token)
                                                {
                                                    if (token.is_valid())
                                                    {
                                                        tokens_to_close.push_back(token);
                                                    }
                                                });
        }
        return dedupe_archive_session_tokens(std::move(tokens_to_close));
    }

    void MainWindow::dispatch_detached_archive_session_close(
        QVector<z7::app::ArchiveSessionToken> tokens)
    {
        DetachedArchiveSessionCloser::dispatch(std::move(tokens));
    }

    void MainWindow::closeEvent(QCloseEvent* event)
    {
        dispatch_detached_archive_session_close(run_shutdown_cleanup_once());
        QMainWindow::closeEvent(event);
    }

    MainWindow::PanelController& MainWindow::panel_controller(int panel_index)
    {
        if (panel_index <= 0)
        {
            return panels_[0];
        }
        return panels_[1];
    }

    MainWindow::PanelController const& MainWindow::panel_controller(int panel_index) const
    {
        if (panel_index <= 0)
        {
            return panels_[0];
        }
        return panels_[1];
    }

    MainWindow::PanelController& MainWindow::active_panel_controller()
    {
        return panel_controller(active_panel_index_);
    }

    MainWindow::PanelController const& MainWindow::active_panel_controller() const
    {
        return panel_controller(active_panel_index_);
    }

    QAbstractItemView* MainWindow::active_item_view() const
    {
        return active_panel_controller().current_item_view();
    }

    int MainWindow::panel_index_for_view(QObject const* view_object) const
    {
        if (view_object == nullptr)
        {
            return -1;
        }
        for (int i = 0; i < 2; ++i)
        {
            PanelController const& panel = panels_[i];
            if (view_object == panel.ui.details_view
                || view_object == panel.ui.icon_list_view
                || (panel.ui.details_view != nullptr && view_object == panel.ui.details_view->viewport())
                || (panel.ui.icon_list_view != nullptr && view_object == panel.ui.icon_list_view->viewport()))
            {
                return i;
            }
        }
        return -1;
    }

} // namespace z7::ui::filemanager
