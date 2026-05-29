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

bool parse_descript_ion_text(const std::string& text, DescriptIonDocument* out_document);
std::string serialize_descript_ion_text(const DescriptIonDocument& document);

bool load_descript_ion_document(const std::string& directory_path,
                                DescriptIonDocument* out_document,
                                std::string* error_message = nullptr);
bool save_descript_ion_document(const std::string& directory_path,
                                const DescriptIonDocument& document,
                                std::string* error_message = nullptr);

std::string normalize_descript_ion_value_for_display(const std::string& value);
bool update_descript_ion_entry(DescriptIonDocument* document,
                               const std::string& item_id,
                               const std::string& raw_comment);

std::optional<std::string> read_descript_ion_comment_for_display(
    const DescriptIonDocument& document,
    const std::string& item_id);

}  // namespace z7::app
