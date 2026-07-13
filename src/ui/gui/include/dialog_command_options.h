#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace z7::ui::gui {

    struct AddTaskOpaqueState {
        std::string raw_update_switch;
        std::vector<std::string> raw_update_switches;
    };

    struct CompressCommandOptions {
        std::string archive_path;
        std::string archive_type = "7z";
        bool keep_archive_name_extension = false;
        bool single_file_input = false;
        std::string single_file_name;
        std::string update_mode = "add";
        std::string path_mode = "relative";
        bool create_sfx = false;
        bool share_for_write = false;
        bool delete_after_compressing = false;
        std::string compression_level;
        std::string method;
        std::string dictionary_size;
        std::string word_size;
        std::string solid_block_size;
        std::string thread_count;
        std::string volume_size;
        std::vector<std::string> extra_parameters;
        std::string password;
        std::string encryption_method = "AES-256";
        bool encrypt_headers = false;
        bool symbolic_links_defined = false;
        bool symbolic_links = false;
        bool hard_links_defined = false;
        bool hard_links = false;
        bool alternate_streams_defined = false;
        bool alternate_streams = false;
        bool file_security_defined = false;
        bool file_security = false;
        bool preserve_access_time = false;
        bool write_mtime_defined = false;
        bool write_mtime = false;
        bool write_ctime_defined = false;
        bool write_ctime = false;
        bool write_atime_defined = false;
        bool write_atime = false;
        bool set_archive_mtime = false;
        AddTaskOpaqueState opaque_add_task;
    };

    struct ExtractCommandOptions {
        std::string output_dir;
        std::string overwrite_switch;
        std::string path_mode = "full";
        bool eliminate_root_duplication = false;
        std::string password;
        bool show_password = false;
        bool restore_file_security = false;
        bool split_dest_enabled = false;
        std::string split_dest_name;
        std::string archive_name;
    };

    struct BenchmarkCommandOptions {
        uint32_t iterations = 10;
        std::string thread_count = "auto";
        std::string dictionary_size = "32m";
        std::string method_value;
        bool total_mode = false;
    };

} // namespace z7::ui::gui
