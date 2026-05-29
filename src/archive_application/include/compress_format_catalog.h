#pragma once

#include <string>
#include <vector>

namespace z7::app {

struct CompressFormatCatalogEntry {
  std::string type_id;
  std::string display_name;
  bool keep_name = false;
};

std::vector<CompressFormatCatalogEntry> list_update_archive_formats();

}  // namespace z7::app
