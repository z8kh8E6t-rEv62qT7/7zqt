#pragma once

#include <map>
#include <optional>
#include <string>

namespace z7::app {

    struct DescriptIonEntry {
        std::string id;
        std::string value;
    };

    struct DescriptIonDocument {
        std::map<std::string, std::string> entries;
        bool had_utf8_bom = false;
        bool use_utf8_bom_on_save = false;
    };

    bool parse_descript_ion_text(std::string const& text, DescriptIonDocument* out_document);
    std::string serialize_descript_ion_text(DescriptIonDocument const& document);

    bool load_descript_ion_document(std::string const& directory_path,
                                    DescriptIonDocument* out_document,
                                    std::string* error_message = nullptr);
    bool save_descript_ion_document(std::string const& directory_path,
                                    DescriptIonDocument const& document,
                                    std::string* error_message = nullptr);

    std::string normalize_descript_ion_value_for_display(std::string const& value);
    bool update_descript_ion_entry(DescriptIonDocument* document,
                                   std::string const& item_id,
                                   std::string const& raw_comment);

    std::optional<std::string> read_descript_ion_comment_for_display(DescriptIonDocument const& document,
                                                                     std::string const& item_id);

} // namespace z7::app
