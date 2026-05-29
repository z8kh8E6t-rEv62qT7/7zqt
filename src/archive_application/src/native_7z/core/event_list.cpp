// src/archive_application/src/native_7z/core/event_list.cpp
// Role: Archive listing helpers for native backend operations.

#include "core/internal.h"

namespace z7::app
{
    namespace
    {

        bool cancel_requested_now(std::atomic<bool> const* cancel_requested)
        {
            return cancel_requested != nullptr && cancel_requested->load(std::memory_order_relaxed);
        }

        HRESULT abort_if_canceled(std::atomic<bool> const* cancel_requested)
        {
            return cancel_requested_now(cancel_requested) ? E_ABORT : S_OK;
        }

        std::string join_relative_path(std::string const& base, std::string const& child)
        {
            if (base.empty())
            {
                return normalize_archive_virtual_directory(child);
            }
            if (child.empty())
            {
                return normalize_archive_virtual_directory(base);
            }
            return normalize_archive_virtual_directory(base + "/" + child);
        }

    } // namespace

    int list_archive_entries_from_arc(CArc const* arc,
                                      std::string const& directory,
                                      bool recursive_dirs,
                                      bool include_detailed_props,
                                      std::atomic<bool>* cancel_requested,
                                      std::vector<ArchiveListEntry>& out_entries,
                                      size_t batch_size,
                                      std::function<bool(std::vector<ArchiveListEntry>&&)> const& batch_callback)
    {
        out_entries.clear();
        if (arc == nullptr || arc->Archive == nullptr)
        {
            return E_FAIL;
        }

        bool const streaming = batch_callback && batch_size > 0;
        std::vector<ArchiveListEntry> pending_batch;
        if (streaming)
        {
            pending_batch.reserve(batch_size);
        }

        // Deliver one entry: buffer it for batch delivery or append directly.
        // Returns E_ABORT when the delegate requests early termination.
        auto deliver = [&](ArchiveListEntry&& entry) -> HRESULT
        {
            if (!streaming)
            {
                out_entries.push_back(std::move(entry));
                return S_OK;
            }
            pending_batch.push_back(std::move(entry));
            if (pending_batch.size() >= batch_size)
            {
                if (!batch_callback(std::move(pending_batch)))
                {
                    return E_ABORT;
                }
                pending_batch.clear();
                pending_batch.reserve(batch_size);
            }
            return S_OK;
        };

        auto flush_pending_batch = [&]() -> HRESULT
        {
            if (streaming && !pending_batch.empty())
            {
                if (!batch_callback(std::move(pending_batch)))
                {
                    return E_ABORT;
                }
            }
            return S_OK;
        };

        bool const use_proxy2 = (arc->GetRawProps && arc->IsTree);
        if (use_proxy2)
        {
            CProxyArc2 proxy;
            const HRESULT proxy_res = proxy.Load(*arc, nullptr);
            if (proxy_res != S_OK)
            {
                return proxy_res;
            }

            if (proxy.Dirs.Size() <= k_Proxy2_RootDirIndex)
            {
                return E_FAIL;
            }

            unsigned target_dir_index = k_Proxy2_RootDirIndex;
            std::vector<std::string> const path_parts =
                split_archive_virtual_directory(normalize_archive_virtual_directory(directory));
            for (std::string const& part : path_parts)
            {
                if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK)
                {
                    return canceled;
                }
                UString const part_u = utf8_to_ustring(part);
                int const pos = proxy.FindItem(target_dir_index, part_u.Ptr(), true);
                if (pos < 0)
                {
                    return S_OK;
                }

                CProxyDir2 const& current_dir = proxy.Dirs[target_dir_index];
                if (static_cast<unsigned>(pos) >= current_dir.Items.Size())
                {
                    return S_OK;
                }

                UInt32 const arc_index = current_dir.Items[static_cast<unsigned>(pos)];
                CProxyFile2 const& file = proxy.Files[arc_index];
                if (!file.IsDir() || file.DirIndex < 0 || static_cast<unsigned>(file.DirIndex) >= proxy.Dirs.Size())
                {
                    return S_OK;
                }
                target_dir_index = static_cast<unsigned>(file.DirIndex);
            }

            auto emit_proxy2_dir = [&](unsigned dir_index,
                                       std::string const& relative_base,
                                       auto&& self) -> HRESULT
            {
                CProxyDir2 const& dir = proxy.Dirs[dir_index];
                for (unsigned i = 0; i < dir.Items.Size(); ++i)
                {
                    if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK)
                    {
                        return canceled;
                    }
                    UInt32 const arc_index = dir.Items[i];
                    CProxyFile2 const& file = proxy.Files[arc_index];
                    if (file.Ignore)
                    {
                        continue;
                    }

                    std::string const name = z7::common::trim_ascii_space_copy(
                        ustring_to_utf8(UString(file.Name)));
                    if (name.empty())
                    {
                        continue;
                    }

                    std::string const child_relative_path = join_relative_path(relative_base, name);
                    ArchiveListEntry entry;
                    entry.path = child_relative_path;
                    entry.is_dir = file.IsDir();
                    entry.size = 0;

                    bool const can_recurse =
                        entry.is_dir &&
                        file.DirIndex >= 0 &&
                        static_cast<unsigned>(file.DirIndex) < proxy.Dirs.Size();
                    unsigned child_dir_index = 0;
                    if (can_recurse)
                    {
                        child_dir_index = static_cast<unsigned>(file.DirIndex);
                        CProxyDir2 const& child_dir = proxy.Dirs[child_dir_index];
                        entry.size = child_dir.Size;
                        if (include_detailed_props)
                        {
                            fill_proxy_dir2_stats(child_dir, entry);
                        }
                    }
                    else if (!entry.is_dir)
                    {
                        UInt64 item_size = 0;
                        bool size_defined = false;
                        if (arc->GetItem_Size(arc_index, item_size, size_defined) == S_OK && size_defined)
                        {
                            entry.size = item_size;
                        }
                    }

                    if (include_detailed_props)
                    {
                        fill_archive_list_entry_details(arc->Archive, arc_index, entry);
                    }

                    if (entry.path.empty())
                    {
                        continue;
                    }
                    if (const HRESULT hr = deliver(std::move(entry)); hr != S_OK)
                    {
                        return hr;
                    }
                    if (recursive_dirs && can_recurse)
                    {
                        if (const HRESULT hr =
                                self(child_dir_index, child_relative_path, self);
                            hr != S_OK)
                        {
                            return hr;
                        }
                    }
                }
                return S_OK;
            };

            if (const HRESULT hr = emit_proxy2_dir(target_dir_index, std::string(), emit_proxy2_dir);
                hr != S_OK)
            {
                return hr;
            }
            return flush_pending_batch();
        }

        CProxyArc proxy;
        const HRESULT proxy_res = proxy.Load(*arc, nullptr);
        if (proxy_res != S_OK)
        {
            return proxy_res;
        }
        if (proxy.Dirs.Size() <= k_Proxy_RootDirIndex)
        {
            return E_FAIL;
        }

        unsigned target_dir_index = k_Proxy_RootDirIndex;
        std::vector<std::string> const path_parts =
            split_archive_virtual_directory(normalize_archive_virtual_directory(directory));
        for (std::string const& part : path_parts)
        {
            if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK)
            {
                return canceled;
            }
            UString const part_u = utf8_to_ustring(part);
            int const next = proxy.FindSubDir(target_dir_index, part_u.Ptr());
            if (next < 0)
            {
                return S_OK;
            }
            target_dir_index = static_cast<unsigned>(next);
        }

        auto emit_proxy_dir = [&](unsigned dir_index,
                                  std::string const& relative_base,
                                  auto&& self) -> HRESULT
        {
            CProxyDir const& dir = proxy.Dirs[dir_index];

            for (unsigned i = 0; i < dir.SubDirs.Size(); ++i)
            {
                if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK)
                {
                    return canceled;
                }
                unsigned const child_dir_index = dir.SubDirs[i];
                CProxyDir const& dir_entry = proxy.Dirs[child_dir_index];
                std::string const name = z7::common::trim_ascii_space_copy(
                    ustring_to_utf8(UString(dir_entry.Name)));
                if (!name.empty())
                {
                    std::string const child_relative_path = join_relative_path(relative_base, name);
                    ArchiveListEntry entry;
                    entry.path = child_relative_path;
                    entry.is_dir = true;
                    entry.size = dir_entry.Size;
                    if (include_detailed_props)
                    {
                        if (dir_entry.ArcIndex >= 0)
                        {
                            fill_archive_list_entry_details(
                                arc->Archive, static_cast<UInt32>(dir_entry.ArcIndex), entry);
                        }
                        fill_proxy_dir_stats(dir_entry, entry);
                    }
                    if (const HRESULT hr = deliver(std::move(entry)); hr != S_OK)
                    {
                        return hr;
                    }
                    if (recursive_dirs)
                    {
                        if (const HRESULT hr =
                                self(child_dir_index, child_relative_path, self);
                            hr != S_OK)
                        {
                            return hr;
                        }
                    }
                }
            }

            for (unsigned i = 0; i < dir.SubFiles.Size(); ++i)
            {
                if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK)
                {
                    return canceled;
                }
                UInt32 const arc_index = dir.SubFiles[i];
                CProxyFile const& file = proxy.Files[arc_index];
                std::string const name = z7::common::trim_ascii_space_copy(
                    ustring_to_utf8(UString(file.Name)));
                if (name.empty())
                {
                    continue;
                }

                UInt64 item_size = 0;
                bool size_defined = false;
                std::string const child_relative_path = join_relative_path(relative_base, name);
                ArchiveListEntry entry;
                entry.path = child_relative_path;
                entry.is_dir = false;
                entry.size =
                    (arc->GetItem_Size(arc_index, item_size, size_defined) == S_OK && size_defined)
                        ? item_size
                        : 0;
                if (include_detailed_props)
                {
                    fill_archive_list_entry_details(arc->Archive, arc_index, entry);
                }
                if (const HRESULT hr = deliver(std::move(entry)); hr != S_OK)
                {
                    return hr;
                }
            }
            return S_OK;
        };

        if (const HRESULT hr = emit_proxy_dir(target_dir_index, std::string(), emit_proxy_dir);
            hr != S_OK)
        {
            return hr;
        }
        return flush_pending_batch();
    }

    int
    list_archive_entries_via_original_api(std::string const& archive_path,
                                          std::string const& directory,
                                          std::string const& archive_type_hint,
                                          bool recursive_dirs,
                                          bool include_detailed_props,
                                          ArchiveBackendHooks const& hooks,
                                          std::atomic<bool>* cancel_requested,
                                          std::function<bool()> wait_while_paused,
                                          CCodecs* preloaded_codecs,
                                          std::vector<ArchiveListEntry>& out_entries,
                                          size_t batch_size,
                                          std::function<bool(std::vector<ArchiveListEntry>&&)> const& batch_callback)
    {
        out_entries.clear();
        return run_with_open_archive_read_hresult(archive_path,
                                                  archive_type_hint,
                                                  hooks,
                                                  cancel_requested,
                                                  std::move(wait_while_paused),
                                                  true,
                                                  preloaded_codecs,
                                                  [&](OpenArchiveReadState const& open_state, UInt32) -> int
                                                  {
                                                      return list_archive_entries_from_arc(open_state.arc,
                                                                                           directory,
                                                                                           recursive_dirs,
                                                                                           include_detailed_props,
                                                                                           cancel_requested,
                                                                                           out_entries,
                                                                                           batch_size,
                                                                                           batch_callback);
                                                  });
    }

} // namespace z7::app
