// src/archive_application/src/native_7z/third_party_adapter/info_properties_extract_internal.h
// Role: Private cross-TU declarations for info_properties_extract split units.

#pragma once

#include "info_properties_detail.h"

namespace z7::app::info_properties_detail {

inline constexpr UInt32 kInvalidArcIndex = static_cast<UInt32>(-1);

bool cancel_requested_now(const std::atomic<bool>* cancel_requested);
bool is_zero_error_flags_prop(PROPID prop_id,
                              const NWindows::NCOM::CPropVariant& prop);
bool append_property_variant_original(std::vector<ArchivePropertyLine>& out_lines,
                                      ArchivePropertySection section,
                                      ArchivePropertyDisplayGroup display_group,
                                      std::optional<uint32_t> level,
                                      PROPID prop_id,
                                      const wchar_t* name,
                                      const NWindows::NCOM::CPropVariant& prop);

uint64_t get_selected_item_unpacked_size(const CArc& arc,
                                         const SelectedPropertyItem& item,
                                         bool flat_view);
uint64_t get_selected_item_pack_size(const CArc& arc,
                                     const SelectedPropertyItem& item,
                                     bool flat_view);

}  // namespace z7::app::info_properties_detail
