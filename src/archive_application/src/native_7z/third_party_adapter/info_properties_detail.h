// src/archive_application/src/native_7z/third_party_adapter/info_properties_detail.h
// Role: Private helper declarations for archive property extraction.

#pragma once

#include <optional>

#include "core/internal.h"
#include "third_party_adapter.h"

namespace z7::app::info_properties_detail {

    bool is_size_prop(PROPID prop_id);
    std::string format_grouped_uint64(uint64_t value);
    std::string property_name_utf8(PROPID prop_id, wchar_t const* property_name);

    void append_line(std::vector<ArchivePropertyLine>& out_lines,
                     PropertyLineKind kind,
                     ArchivePropertySection section,
                     ArchivePropertyDisplayGroup display_group,
                     std::optional<uint32_t> level = std::nullopt,
                     std::optional<uint32_t> const& prop_id = std::nullopt,
                     std::string name = {},
                     std::string value = {});
    void append_separator(std::vector<ArchivePropertyLine>& out_lines,
                          PropertyLineKind kind,
                          ArchivePropertySection section,
                          ArchivePropertyDisplayGroup display_group,
                          std::optional<uint32_t> level = std::nullopt);
    void append_property_variant_line(std::vector<ArchivePropertyLine>& out_lines,
                                      ArchivePropertySection section,
                                      ArchivePropertyDisplayGroup display_group,
                                      std::optional<uint32_t> level,
                                      PROPID prop_id,
                                      wchar_t const* name,
                                      NWindows::NCOM::CPropVariant const& prop);
    std::string raw_property_data_to_text(PROPID prop_id, void const* data, UInt32 data_size);

    struct SelectedPropertyItem {
        std::string path;
        std::string path_prefix;
        std::string name;
        bool is_dir = false;
        bool is_leaf_dir = false;
        bool has_arc_index = false;
        bool allow_archive_item_props = false;
        UInt32 arc_index = 0;
        UInt64 size = 0;
        UInt64 pack_size = 0;
        UInt32 num_sub_dirs = 0;
        UInt32 num_sub_files = 0;
        bool crc_defined = false;
        UInt32 crc = 0;
    };

    struct FolderPropertyContext {
        bool valid = false;
        std::string path_prefix;
        UInt64 size = 0;
        UInt64 pack_size = 0;
        UInt32 num_sub_dirs = 0;
        UInt32 num_sub_files = 0;
        bool crc_defined = false;
        UInt32 crc = 0;
    };

    HRESULT collect_property_context(CArc const& arc,
                                     std::string const& directory,
                                     std::vector<std::string> const& entries,
                                     std::vector<SelectedPropertyItem>& selected_items,
                                     FolderPropertyContext& folder_context,
                                     std::atomic<bool> const* cancel_requested = nullptr);
    void append_selected_item_properties(CArc const& arc,
                                         SelectedPropertyItem const& selected_item,
                                         bool flat_view,
                                         std::vector<ArchivePropertyLine>& out_lines,
                                         std::atomic<bool> const* cancel_requested = nullptr);
    void append_multi_selection_summary(CArc const& arc,
                                        std::vector<SelectedPropertyItem> const& selected_items,
                                        bool flat_view,
                                        std::vector<ArchivePropertyLine>& out_lines,
                                        std::atomic<bool> const* cancel_requested = nullptr);
    void append_folder_properties(FolderPropertyContext const& folder_context,
                                  std::vector<ArchivePropertyLine>& out_lines);
    void append_archive_link_properties_with_offset(CCodecs const& codecs,
                                                    CArchiveLink const& archive_link,
                                                    uint32_t level_offset,
                                                    bool include_non_open_error,
                                                    std::vector<ArchivePropertyLine>& out_lines,
                                                    std::atomic<bool> const* cancel_requested = nullptr);
    void append_archive_props2_for_parent_entry(CArc const& parent_arc,
                                                std::string const& child_entry_path,
                                                std::optional<uint32_t> level,
                                                std::vector<ArchivePropertyLine>& out_lines,
                                                std::atomic<bool> const* cancel_requested = nullptr);
    void append_archive_link_properties(CCodecs const& codecs,
                                        CArchiveLink const& archive_link,
                                        std::vector<ArchivePropertyLine>& out_lines,
                                        std::atomic<bool> const* cancel_requested = nullptr);

} // namespace z7::app::info_properties_detail
