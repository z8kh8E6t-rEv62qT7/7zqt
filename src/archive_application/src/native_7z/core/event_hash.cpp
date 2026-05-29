// src/archive_application/src/native_7z/core/event_hash.cpp
// Role: Hash input collection and hash summary conversion helpers.

#include "core/internal.h"

namespace z7::app
{
    namespace
    {

        inline constexpr size_t kHashDigestStringSize = k_HashCalc_DigestSize_Max * 2 + k_HashCalc_ExtraSize * 2 + 16;

        bool safe_is_directory(fs::path const& path)
        {
            std::error_code ec;
            return fs::is_directory(path, ec);
        }

        uint64_t safe_file_size(fs::path const& path)
        {
            std::error_code ec;
            uint64_t const size = fs::file_size(path, ec);
            return ec ? 0 : size;
        }

    } // namespace

    std::string path_leaf_name(fs::path const& path)
    {
        std::string name = path.filename().generic_string();
        if (!name.empty())
        {
            return name;
        }
        return path.generic_string();
    }

    void collect_hash_entries_for_path(fs::path const& selected_path,
                                       std::string const& display_name,
                                       bool recursive_dirs,
                                       std::vector<HashInputEntry>& entries,
                                       uint64_t& total_files,
                                       uint64_t& total_bytes)
    {
        bool const selected_is_dir = safe_is_directory(selected_path);
        if (selected_is_dir)
        {
            entries.push_back({selected_path, display_name, true, 0});
            if (!recursive_dirs)
            {
                return;
            }

            std::error_code it_ec;
            fs::recursive_directory_iterator it(selected_path, fs::directory_options::skip_permission_denied, it_ec);
            fs::recursive_directory_iterator end;

            while (!it_ec && it != end)
            {
                fs::path const child = it->path();
                std::error_code rel_ec;
                fs::path const rel = fs::relative(child, selected_path, rel_ec);
                std::string const rel_text = rel_ec ? child.filename().generic_string() : rel.generic_string();
                std::string const item_name = display_name + "/" + rel_text;

                if (it->is_directory(it_ec))
                {
                    entries.push_back({child, item_name, true, 0});
                }
                else if (it->is_regular_file(it_ec))
                {
                    uint64_t const size = safe_file_size(child);
                    entries.push_back({child, item_name, false, size});
                    ++total_files;
                    total_bytes += size;
                }

                std::error_code inc_ec;
                it.increment(inc_ec);
                it_ec = inc_ec;
            }
            return;
        }

        uint64_t const size = safe_file_size(selected_path);
        entries.push_back({selected_path, display_name, false, size});
        ++total_files;
        total_bytes += size;
    }

    HashSummary make_hash_summary(CHashBundle const& bundle)
    {
        HashSummary summary;
        summary.num_dirs = bundle.NumDirs;
        summary.num_files = bundle.NumFiles;
        summary.num_alt_streams = bundle.NumAltStreams;
        summary.files_size = bundle.FilesSize;
        summary.alt_streams_size = bundle.AltStreamsSize;
        summary.num_errors = bundle.NumErrors;
        summary.main_name = ustring_to_utf8(bundle.MainName);
        summary.first_file_name = ustring_to_utf8(bundle.FirstFileName);

        char digest[kHashDigestStringSize];
        for (unsigned i = 0; i < bundle.Hashers.Size(); ++i)
        {
            CHasherState const& hasher = bundle.Hashers[i];
            HashMethodDigest method;
            method.method_name = astring_to_std(hasher.Name);

            digest[0] = 0;
            hasher.WriteToString(k_HashCalc_Index_DataSum, digest);
            method.data_sum = digest;
            method.has_data_sum = !method.data_sum.empty();

            digest[0] = 0;
            hasher.WriteToString(k_HashCalc_Index_NamesSum, digest);
            method.names_sum = digest;
            method.has_names_sum = !method.names_sum.empty();

            digest[0] = 0;
            hasher.WriteToString(k_HashCalc_Index_StreamsSum, digest);
            method.streams_sum = digest;
            method.has_streams_sum = !method.streams_sum.empty();

            summary.methods.push_back(std::move(method));
        }

        return summary;
    }

} // namespace z7::app
