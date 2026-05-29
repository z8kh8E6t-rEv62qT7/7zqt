// src/archive_application/src/native_7z/operations/extract_output.h
// Role: Output directory helper declarations for extract request handling.

#pragma once

#include <string>

namespace z7::app {

std::string resolve_multi_archive_output_dir(const std::string& output_template,
                                             const std::string& archive_path);
std::string output_tail_name(std::string output_dir);

}  // namespace z7::app
