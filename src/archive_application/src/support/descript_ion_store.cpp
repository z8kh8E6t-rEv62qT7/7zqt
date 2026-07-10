#include "descript_ion_store.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

#include "archive_error.h"

namespace z7::app {
    namespace {

        constexpr char const* kDescriptIonFileName = "descript.ion";
        constexpr unsigned char kUtf8BomBytes[] = {0xEF, 0xBB, 0xBF};

        std::string trim_copy_ascii(std::string value) {
            auto not_space = [](unsigned char ch) {
                return !std::isspace(ch);
            };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
                            return not_space(static_cast<unsigned char>(ch));
                        }));
            value.erase(std::find_if(value.rbegin(),
                                     value.rend(),
                                     [&](char ch) { return not_space(static_cast<unsigned char>(ch)); })
                            .base(),
                        value.end());
            return value;
        }

        std::string lower_ascii_copy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        }

        void strip_carriage_returns(std::string* value) {
            if (value == nullptr) {
                return;
            }
            value->erase(std::remove(value->begin(), value->end(), '\r'), value->end());
        }

        std::string read_id_token(std::string const& text, size_t* cursor) {
            if (cursor == nullptr) {
                return {};
            }

            std::string result;
            bool quoted = false;
            while (*cursor < text.size()) {
                char const ch = text[*cursor];
                ++(*cursor);
                bool const is_separator = ch == ' ' || ch == '\t';
                if (ch == '\n' || (is_separator && !quoted) || (ch == '"' && quoted)) {
                    break;
                }
                if (ch == '"') {
                    quoted = true;
                    continue;
                }
                result.push_back(ch);
            }

            result = trim_copy_ascii(result);
            strip_carriage_returns(&result);
            return result;
        }

        std::string read_value_token(std::string const& text, size_t* cursor) {
            if (cursor == nullptr) {
                return {};
            }

            std::string result;
            while (*cursor < text.size()) {
                char const ch = text[*cursor];
                ++(*cursor);
                if (ch == '\n') {
                    break;
                }
                result.push_back(ch);
            }

            result = trim_copy_ascii(result);
            strip_carriage_returns(&result);
            return result;
        }

        std::string file_path_for_directory(std::string const& directory_path) {
            std::filesystem::path path(directory_path);
            path /= kDescriptIonFileName;
            return path.string();
        }

        bool contains_non_ascii(std::string const& value) {
            return std::any_of(value.begin(), value.end(), [](unsigned char ch) { return ch > 0x7F; });
        }

        auto find_case_insensitive_entry(DescriptIonDocument* document, std::string const& item_id) {
            std::string const lowered = lower_ascii_copy(item_id);
            return std::find_if(document->entries.begin(), document->entries.end(), [&](auto const& entry) {
                return lower_ascii_copy(entry.first) == lowered;
            });
        }

        auto find_case_insensitive_entry(DescriptIonDocument const& document, std::string const& item_id) {
            std::string const lowered = lower_ascii_copy(item_id);
            return std::find_if(document.entries.begin(), document.entries.end(), [&](auto const& entry) {
                return lower_ascii_copy(entry.first) == lowered;
            });
        }

    } // namespace

    bool parse_descript_ion_text(std::string const& text, DescriptIonDocument* out_document) {
        if (out_document == nullptr) {
            return false;
        }

        out_document->entries.clear();
        out_document->had_utf8_bom = text.size() >= 3
                                  && static_cast<unsigned char>(text[0]) == kUtf8BomBytes[0]
                                  && static_cast<unsigned char>(text[1]) == kUtf8BomBytes[1]
                                  && static_cast<unsigned char>(text[2]) == kUtf8BomBytes[2];
        out_document->use_utf8_bom_on_save = out_document->had_utf8_bom;

        size_t cursor = out_document->had_utf8_bom ? 3 : 0;
        while (cursor < text.size()) {
            std::string const id = read_id_token(text, &cursor);
            if (id.empty()) {
                continue;
            }
            std::string const value = read_value_token(text, &cursor);
            out_document->entries[id] = value;
        }
        return true;
    }

    std::string serialize_descript_ion_text(DescriptIonDocument const& document) {
        std::string out;
        if (document.use_utf8_bom_on_save) {
            out.append(reinterpret_cast<char const*>(kUtf8BomBytes), 3);
            out.append("\r\n");
        }

        for (auto const& [id, value] : document.entries) {
            bool const multi_word_id = id.find(' ') != std::string::npos;
            if (multi_word_id) {
                out.push_back('"');
            }
            out += id;
            if (multi_word_id) {
                out.push_back('"');
            }
            out.push_back(' ');
            out += value;
            out.append("\r\n");
        }
        return out;
    }

    bool load_descript_ion_document(std::string const& directory_path,
                                    DescriptIonDocument* out_document,
                                    std::string* error_message) {
        if (out_document == nullptr) {
            if (error_message != nullptr) {
                *error_message = "Descript.ion output document is null";
            }
            return false;
        }

        out_document->entries.clear();
        out_document->had_utf8_bom = false;
        out_document->use_utf8_bom_on_save = false;

        std::ifstream file(file_path_for_directory(directory_path), std::ios::binary);
        if (!file.is_open()) {
            return true;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        if (!file.good() && !file.eof()) {
            if (error_message != nullptr) {
                *error_message = "Failed to read descript.ion";
            }
            return false;
        }
        return parse_descript_ion_text(buffer.str(), out_document);
    }

    bool save_descript_ion_document(std::string const& directory_path,
                                    DescriptIonDocument const& document,
                                    std::string* error_message) {
        std::filesystem::path const file_path(file_path_for_directory(directory_path));
        std::error_code ec;
        std::filesystem::create_directories(file_path.parent_path(), ec);
        if (ec) {
            if (error_message != nullptr) {
                *error_message = ec.message();
            }
            return false;
        }

        std::ofstream file(file_path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            if (error_message != nullptr) {
                *error_message = "Failed to open descript.ion for writing";
            }
            return false;
        }

        std::string const serialized = serialize_descript_ion_text(document);
        file.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        if (!file.good()) {
            if (error_message != nullptr) {
                *error_message = "Failed to write descript.ion";
            }
            return false;
        }
        return true;
    }

    std::string normalize_descript_ion_value_for_display(std::string const& value) {
        size_t const separator_pos = value.find('\x04');
        return trim_copy_ascii(separator_pos == std::string::npos ? value : value.substr(0, separator_pos));
    }

    bool update_descript_ion_entry(DescriptIonDocument* document,
                                   std::string const& item_id,
                                   std::string const& raw_comment) {
        if (document == nullptr) {
            return false;
        }

        std::string const normalized_id = trim_copy_ascii(item_id);
        if (normalized_id.empty()) {
            return false;
        }

        std::string normalized_value = raw_comment;
        strip_carriage_returns(&normalized_value);
        normalized_value = trim_copy_ascii(normalized_value);
        auto const existing = find_case_insensitive_entry(document, normalized_id);
        if (normalized_value.empty()) {
            if (existing != document->entries.end()) {
                document->entries.erase(existing);
            }
            return true;
        }

        if (existing != document->entries.end()) {
            existing->second = normalized_value;
        } else {
            document->entries[normalized_id] = normalized_value;
        }
        if (contains_non_ascii(normalized_id) || contains_non_ascii(normalized_value)) {
            document->use_utf8_bom_on_save = true;
        }
        return true;
    }

    std::optional<std::string> read_descript_ion_comment_for_display(DescriptIonDocument const& document,
                                                                     std::string const& item_id) {
        auto const it = find_case_insensitive_entry(document, item_id);
        if (it == document.entries.end()) {
            return std::nullopt;
        }
        return normalize_descript_ion_value_for_display(it->second);
    }

} // namespace z7::app
