// src/archive_application/src/native_7z/info/info_properties.cpp
// Role: Public entry point for structured archive property collection.

#include "third_party_adapter/info_properties_detail.h"

namespace z7::app {

namespace {

bool cancel_requested_now(const std::atomic<bool>* cancel_requested) {
  return cancel_requested != nullptr &&
         cancel_requested->load(std::memory_order_relaxed);
}

HRESULT abort_if_canceled(const std::atomic<bool>* cancel_requested) {
  return cancel_requested_now(cancel_requested) ? E_ABORT : S_OK;
}

}  // namespace

int collect_archive_properties_from_open_state(const ArchivePropertiesRequest& request,
                                               const CArc& arc,
                                               CCodecs& codecs,
                                               const CArchiveLink& archive_link,
                                               std::atomic<bool>* cancel_requested,
                                               std::vector<ArchivePropertyLine>& out_lines) {
  out_lines.clear();
  if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK) {
    return canceled;
  }

  std::vector<info_properties_detail::SelectedPropertyItem> selected_items;
  info_properties_detail::FolderPropertyContext folder_context;
  const HRESULT context_res = info_properties_detail::collect_property_context(
      arc,
      request.directory,
      request.entries,
      selected_items,
      folder_context,
      cancel_requested);
  if (context_res != S_OK) {
    return context_res;
  }
  if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK) {
    return canceled;
  }

  if (selected_items.size() == 1) {
    info_properties_detail::append_selected_item_properties(
        arc,
        selected_items.front(),
        request.flat_view,
        out_lines,
        cancel_requested);
    if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK) {
      return canceled;
    }
    info_properties_detail::append_separator(out_lines,
                                             PropertyLineKind::kSeparator,
                                             ArchivePropertySection::kCurrentFolder,
                                             ArchivePropertyDisplayGroup::kCurrentFolderPath);
  } else if (!selected_items.empty()) {
    info_properties_detail::append_multi_selection_summary(
        arc, selected_items, request.flat_view, out_lines, cancel_requested);
    if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK) {
      return canceled;
    }
    info_properties_detail::append_separator(out_lines,
                                             PropertyLineKind::kSeparator,
                                             ArchivePropertySection::kCurrentFolder,
                                             ArchivePropertyDisplayGroup::kCurrentFolderPath);
  }
  info_properties_detail::append_folder_properties(folder_context, out_lines);
  if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK) {
    return canceled;
  }
  info_properties_detail::append_archive_link_properties(
      codecs, archive_link, out_lines, cancel_requested);
  if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK) {
    return canceled;
  }
  return S_OK;
}

int collect_archive_properties_via_original_api(const ArchivePropertiesRequest& request,
                                                const ArchiveBackendHooks& hooks,
                                                std::atomic<bool>* cancel_requested,
                                                std::function<bool()> wait_while_paused,
                                                CCodecs* preloaded_codecs,
                                                std::vector<ArchivePropertyLine>& out_lines) {
  out_lines.clear();
  if (request.archive_path.empty()) {
    return E_INVALIDARG;
  }
  if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK) {
    return canceled;
  }
  return run_with_open_archive_read_hresult(
      request.archive_path,
      request.archive_type_hint,
      hooks,
      cancel_requested,
      std::move(wait_while_paused),
      true,
      preloaded_codecs,
      [&](OpenArchiveReadState& open_state, UInt32) -> int {
        const CArc* arc = open_state.arc;
        CCodecs& codecs = preloaded_codecs != nullptr ? *preloaded_codecs : open_state.codecs;
        return collect_archive_properties_from_open_state(request,
                                                          *arc,
                                                          codecs,
                                                          open_state.archive_link,
                                                          cancel_requested,
                                                          out_lines);
      });
}

}  // namespace z7::app
