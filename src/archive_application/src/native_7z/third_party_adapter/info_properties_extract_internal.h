// src/archive_application/src/native_7z/third_party_adapter/info_properties_extract_internal.h
// Role: Private cross-TU declarations for info_properties_extract split units.

#pragma once

#include <utility>

#include "info_properties_detail.h"

namespace z7::app::info_properties_detail {

    inline constexpr UInt32 kInvalidArcIndex = static_cast<UInt32>(-1);

    template <typename Callable>
    HRESULT safe_property_hresult(Callable&& callable) noexcept {
        try {
            return std::forward<Callable>(callable)();
        } catch (...) {
            return E_FAIL;
        }
    }

    bool cancel_requested_now(std::atomic<bool> const* cancel_requested);
    bool is_zero_error_flags_prop(PROPID prop_id, NWindows::NCOM::CPropVariant const& prop);
    bool append_property_variant_original(std::vector<ArchivePropertyLine>& out_lines,
                                          ArchivePropertySection section,
                                          ArchivePropertyDisplayGroup display_group,
                                          std::optional<uint32_t> level,
                                          PROPID prop_id,
                                          wchar_t const* name,
                                          NWindows::NCOM::CPropVariant const& prop);

    uint64_t get_selected_item_unpacked_size(CArc const& arc, SelectedPropertyItem const& item, bool flat_view);
    uint64_t get_selected_item_pack_size(CArc const& arc, SelectedPropertyItem const& item, bool flat_view);

} // namespace z7::app::info_properties_detail
