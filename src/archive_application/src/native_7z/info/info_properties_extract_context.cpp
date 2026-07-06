// src/archive_application/src/native_7z/info/info_properties_extract_context.cpp
// Role: Proxy-based selected item and folder context extraction.

#include <unordered_set>

#include "third_party_adapter/info_properties_extract_internal.h"

namespace z7::app::info_properties_detail {

    namespace {

        bool starts_with(std::string const& value, std::string const& prefix) {
            return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
        }

        std::string normalize_entry_for_lookup(std::string const& raw_entry, std::string const& normalized_directory) {
            std::string normalized = normalize_archive_virtual_directory(raw_entry);
            if (normalized.empty() || normalized_directory.empty()) {
                return normalized;
            }

            std::string const prefix = normalized_directory + "/";
            if (normalized == normalized_directory || starts_with(normalized, prefix)) {
                return normalized;
            }
            return prefix + normalized;
        }

        std::string compute_selected_item_prefix(std::string const& normalized_directory,
                                                 std::string const& normalized_path) {
            std::string relative_path = normalized_path;
            if (!normalized_directory.empty()) {
                if (relative_path == normalized_directory) {
                    return {};
                }
                std::string const prefix = normalized_directory + "/";
                if (starts_with(relative_path, prefix)) {
                    relative_path.erase(0, prefix.size());
                }
            }

            size_t const slash = relative_path.rfind('/');
            if (slash == std::string::npos) {
                return {};
            }
            return relative_path.substr(0, slash + 1);
        }

        bool resolve_proxy_dir_index(CProxyArc const& proxy,
                                     std::string const& normalized_directory,
                                     unsigned& out_dir_index) {
            out_dir_index = k_Proxy_RootDirIndex;
            std::vector<std::string> const parts = split_archive_virtual_directory(normalized_directory);
            for (std::string const& part : parts) {
                UString const part_u = utf8_to_ustring(part);
                int const next = proxy.FindSubDir(out_dir_index, part_u.Ptr());
                if (next < 0) {
                    return false;
                }
                out_dir_index = static_cast<unsigned>(next);
            }
            return true;
        }

        bool resolve_proxy2_dir_index(CProxyArc2 const& proxy,
                                      std::string const& normalized_directory,
                                      unsigned& out_dir_index) {
            out_dir_index = k_Proxy2_RootDirIndex;
            std::vector<std::string> const parts = split_archive_virtual_directory(normalized_directory);
            for (std::string const& part : parts) {
                UString const part_u = utf8_to_ustring(part);
                int const pos = proxy.FindItem(out_dir_index, part_u.Ptr(), true);
                if (pos < 0) {
                    return false;
                }

                CProxyDir2 const& current_dir = proxy.Dirs[out_dir_index];
                if (static_cast<unsigned>(pos) >= current_dir.Items.Size()) {
                    return false;
                }

                UInt32 const arc_index = current_dir.Items[static_cast<unsigned>(pos)];
                CProxyFile2 const& file = proxy.Files[arc_index];
                if (!file.IsDir() || file.DirIndex < 0 || static_cast<unsigned>(file.DirIndex) >= proxy.Dirs.Size()) {
                    return false;
                }
                out_dir_index = static_cast<unsigned>(file.DirIndex);
            }
            return true;
        }

        void fill_folder_context_from_proxy(CProxyArc const& proxy,
                                            unsigned const dir_index,
                                            FolderPropertyContext& folder_context) {
            if (dir_index >= proxy.Dirs.Size()) {
                return;
            }
            CProxyDir const& dir = proxy.Dirs[dir_index];
            std::string const normalized_prefix =
                normalize_archive_virtual_directory(ustring_to_utf8(proxy.GetDirPath_as_Prefix(dir_index)));
            folder_context.valid = true;
            folder_context.path_prefix = normalized_prefix.empty() ? std::string() : (normalized_prefix + "/");
            folder_context.size = dir.Size;
            folder_context.pack_size = dir.PackSize;
            folder_context.num_sub_dirs = dir.NumSubDirs;
            folder_context.num_sub_files = dir.NumSubFiles;
            folder_context.crc_defined = dir.CrcIsDefined;
            folder_context.crc = dir.Crc;
        }

        void fill_folder_context_from_proxy2(CProxyArc2 const& proxy,
                                             unsigned const dir_index,
                                             FolderPropertyContext& folder_context) {
            if (dir_index >= proxy.Dirs.Size()) {
                return;
            }
            CProxyDir2 const& dir = proxy.Dirs[dir_index];
            bool is_alt_stream_dir = false;
            std::string const normalized_prefix = normalize_archive_virtual_directory(
                ustring_to_utf8(proxy.GetDirPath_as_Prefix(dir_index, is_alt_stream_dir)));
            folder_context.valid = true;
            folder_context.path_prefix = normalized_prefix.empty() ? std::string() : (normalized_prefix + "/");
            folder_context.size = dir.Size;
            folder_context.pack_size = dir.PackSize;
            folder_context.num_sub_dirs = dir.NumSubDirs;
            folder_context.num_sub_files = dir.NumSubFiles;
            folder_context.crc_defined = dir.CrcIsDefined;
            folder_context.crc = dir.Crc;
        }

        bool
        resolve_proxy_item(CProxyArc const& proxy, std::string const& normalized_path, SelectedPropertyItem& out_item) {
            std::vector<std::string> const parts = split_archive_virtual_directory(normalized_path);
            if (parts.empty()) {
                return false;
            }

            unsigned parent_dir_index = k_Proxy_RootDirIndex;
            for (size_t i = 0; i + 1 < parts.size(); ++i) {
                UString const part_u = utf8_to_ustring(parts[i]);
                int const next = proxy.FindSubDir(parent_dir_index, part_u.Ptr());
                if (next < 0) {
                    return false;
                }
                parent_dir_index = static_cast<unsigned>(next);
            }

            UString const leaf_u = utf8_to_ustring(parts.back());
            int const sub_dir = proxy.FindSubDir(parent_dir_index, leaf_u.Ptr());
            if (sub_dir >= 0) {
                CProxyDir const& dir = proxy.Dirs[static_cast<unsigned>(sub_dir)];
                out_item.path = normalized_path;
                out_item.name = z7::common::trim_ascii_space_copy(ustring_to_utf8(UString(dir.Name)));
                if (out_item.name.empty()) {
                    out_item.name = parts.back();
                }
                out_item.is_dir = true;
                out_item.is_leaf_dir = dir.ArcIndex >= 0;
                out_item.has_arc_index = dir.ArcIndex >= 0;
                out_item.allow_archive_item_props = out_item.has_arc_index;
                out_item.arc_index = out_item.has_arc_index ? static_cast<UInt32>(dir.ArcIndex) : kInvalidArcIndex;
                out_item.size = dir.Size;
                out_item.pack_size = dir.PackSize;
                out_item.num_sub_dirs = dir.NumSubDirs;
                out_item.num_sub_files = dir.NumSubFiles;
                out_item.crc_defined = dir.CrcIsDefined;
                out_item.crc = dir.Crc;
                return true;
            }

            CProxyDir const& parent_dir = proxy.Dirs[parent_dir_index];
            for (unsigned i = 0; i < parent_dir.SubFiles.Size(); ++i) {
                UInt32 const arc_index = parent_dir.SubFiles[i];
                CProxyFile const& file = proxy.Files[arc_index];
                if (!UString(file.Name).IsEqualTo_NoCase(leaf_u)) {
                    continue;
                }

                out_item.path = normalized_path;
                out_item.name = z7::common::trim_ascii_space_copy(ustring_to_utf8(UString(file.Name)));
                if (out_item.name.empty()) {
                    out_item.name = parts.back();
                }
                out_item.is_dir = false;
                out_item.has_arc_index = true;
                out_item.allow_archive_item_props = true;
                out_item.arc_index = arc_index;
                return true;
            }

            return false;
        }

        bool resolve_proxy2_item(CProxyArc2 const& proxy,
                                 std::string const& normalized_path,
                                 SelectedPropertyItem& out_item) {
            std::vector<std::string> const parts = split_archive_virtual_directory(normalized_path);
            if (parts.empty()) {
                return false;
            }

            unsigned parent_dir_index = k_Proxy2_RootDirIndex;
            for (size_t i = 0; i + 1 < parts.size(); ++i) {
                UString const part_u = utf8_to_ustring(parts[i]);
                int const pos = proxy.FindItem(parent_dir_index, part_u.Ptr(), true);
                if (pos < 0) {
                    return false;
                }

                CProxyDir2 const& current_dir = proxy.Dirs[parent_dir_index];
                if (static_cast<unsigned>(pos) >= current_dir.Items.Size()) {
                    return false;
                }

                UInt32 const arc_index = current_dir.Items[static_cast<unsigned>(pos)];
                CProxyFile2 const& file = proxy.Files[arc_index];
                if (!file.IsDir() || file.DirIndex < 0 || static_cast<unsigned>(file.DirIndex) >= proxy.Dirs.Size()) {
                    return false;
                }
                parent_dir_index = static_cast<unsigned>(file.DirIndex);
            }

            UString const leaf_u = utf8_to_ustring(parts.back());
            int const pos = proxy.FindItem(parent_dir_index, leaf_u.Ptr(), false);
            if (pos < 0) {
                return false;
            }
            CProxyDir2 const& dir = proxy.Dirs[parent_dir_index];
            if (static_cast<unsigned>(pos) >= dir.Items.Size()) {
                return false;
            }

            UInt32 const arc_index = dir.Items[static_cast<unsigned>(pos)];
            CProxyFile2 const& file = proxy.Files[arc_index];
            if (file.Ignore) {
                return false;
            }

            out_item.path = normalized_path;
            out_item.name = z7::common::trim_ascii_space_copy(ustring_to_utf8(UString(file.Name)));
            if (out_item.name.empty()) {
                out_item.name = parts.back();
            }
            out_item.has_arc_index = true;
            out_item.arc_index = arc_index;
            out_item.allow_archive_item_props = !file.Ignore;
            out_item.is_dir = file.IsDir();
            out_item.is_leaf_dir = file.IsDir() && !file.Ignore;
            if (file.IsDir() && file.DirIndex >= 0 && static_cast<unsigned>(file.DirIndex) < proxy.Dirs.Size()) {
                CProxyDir2 const& child_dir = proxy.Dirs[static_cast<unsigned>(file.DirIndex)];
                out_item.size = child_dir.Size;
                out_item.pack_size = child_dir.PackSize;
                out_item.num_sub_dirs = child_dir.NumSubDirs;
                out_item.num_sub_files = child_dir.NumSubFiles;
                out_item.crc_defined = child_dir.CrcIsDefined;
                out_item.crc = child_dir.Crc;
            }
            return true;
        }

    } // namespace

    HRESULT collect_property_context(CArc const& arc,
                                     std::string const& directory,
                                     std::vector<std::string> const& entries,
                                     std::vector<SelectedPropertyItem>& selected_items,
                                     FolderPropertyContext& folder_context,
                                     std::atomic<bool> const* cancel_requested) {
        selected_items.clear();
        folder_context = FolderPropertyContext{};
        if (arc.Archive == nullptr) {
            return E_FAIL;
        }
        if (cancel_requested_now(cancel_requested)) {
            return E_ABORT;
        }

        std::string const normalized_directory = normalize_archive_virtual_directory(directory);
        std::unordered_set<std::string> seen_paths;
        seen_paths.reserve(entries.size());

        bool const use_proxy2 = (arc.GetRawProps && arc.IsTree);
        if (use_proxy2) {
            CProxyArc2 proxy;
            const HRESULT load_res = safe_property_hresult([&]() -> HRESULT { return proxy.Load(arc, nullptr); });
            if (load_res == E_ABORT) {
                return E_ABORT;
            }
            if (load_res != S_OK) {
                return S_OK;
            }

            unsigned folder_dir_index = k_Proxy2_RootDirIndex;
            if (!resolve_proxy2_dir_index(proxy, normalized_directory, folder_dir_index)) {
                folder_dir_index = k_Proxy2_RootDirIndex;
            }
            fill_folder_context_from_proxy2(proxy, folder_dir_index, folder_context);

            for (std::string const& raw_entry : entries) {
                if (cancel_requested_now(cancel_requested)) {
                    return E_ABORT;
                }
                std::string const normalized = normalize_entry_for_lookup(raw_entry, normalized_directory);
                if (normalized.empty() || !seen_paths.insert(normalized).second) {
                    continue;
                }

                SelectedPropertyItem selected_item;
                if (!resolve_proxy2_item(proxy, normalized, selected_item)) {
                    continue;
                }
                selected_item.path_prefix = compute_selected_item_prefix(normalized_directory, selected_item.path);
                selected_items.push_back(std::move(selected_item));
            }
            return S_OK;
        }

        CProxyArc proxy;
        const HRESULT load_res = safe_property_hresult([&]() -> HRESULT { return proxy.Load(arc, nullptr); });
        if (load_res == E_ABORT) {
            return E_ABORT;
        }
        if (load_res != S_OK) {
            return S_OK;
        }
        unsigned folder_dir_index = k_Proxy_RootDirIndex;
        if (!resolve_proxy_dir_index(proxy, normalized_directory, folder_dir_index)) {
            folder_dir_index = k_Proxy_RootDirIndex;
        }
        fill_folder_context_from_proxy(proxy, folder_dir_index, folder_context);

        for (std::string const& raw_entry : entries) {
            if (cancel_requested_now(cancel_requested)) {
                return E_ABORT;
            }
            std::string const normalized = normalize_entry_for_lookup(raw_entry, normalized_directory);
            if (normalized.empty() || !seen_paths.insert(normalized).second) {
                continue;
            }

            SelectedPropertyItem selected_item;
            if (!resolve_proxy_item(proxy, normalized, selected_item)) {
                continue;
            }
            selected_item.path_prefix = compute_selected_item_prefix(normalized_directory, selected_item.path);
            selected_items.push_back(std::move(selected_item));
        }
        return S_OK;
    }

} // namespace z7::app::info_properties_detail
