// src/archive_application/src/native_7z/info/info_properties_format.cpp
// Role: Value formatting and line assembly for archive properties.

#include "third_party_adapter/info_properties_detail.h"

#include <array>

namespace z7::app::info_properties_detail {

namespace {

constexpr uint32_t kRawDataPrintMaxSize = 1u << 8;

}  // namespace

bool is_size_prop(const PROPID prop_id) {
  switch (prop_id) {
    case kpidSize:
    case kpidPackSize:
    case kpidNumSubDirs:
    case kpidNumSubFiles:
    case kpidOffset:
    case kpidLinks:
    case kpidNumBlocks:
    case kpidNumVolumes:
    case kpidPhySize:
    case kpidHeadersSize:
    case kpidTotalSize:
    case kpidFreeSpace:
    case kpidClusterSize:
    case kpidNumErrors:
    case kpidNumStreams:
    case kpidNumAltStreams:
    case kpidAltStreamsSize:
    case kpidVirtualSize:
    case kpidUnpackSize:
    case kpidTotalPhySize:
    case kpidTailSize:
    case kpidEmbeddedStubSize:
      return true;
    default:
      return false;
  }
}

std::string format_grouped_uint64(const uint64_t value) {
  const std::string digits = std::to_string(value);
  if (digits.size() <= 3) {
    return digits;
  }

  std::string out;
  out.reserve(digits.size() + digits.size() / 3);
  const size_t first_group = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
  out.append(digits, 0, first_group);
  for (size_t i = first_group; i < digits.size(); i += 3) {
    out.push_back(' ');
    out.append(digits, i, 3);
  }
  return out;
}

std::string property_name_utf8(const PROPID prop_id, const wchar_t* property_name) {
  if (property_name != nullptr && property_name[0] != 0) {
    return ustring_to_utf8(UString(property_name));
  }
  return std::to_string(static_cast<uint32_t>(prop_id));
}

void append_line(std::vector<ArchivePropertyLine>& out_lines,
                 const PropertyLineKind kind,
                 const ArchivePropertySection section,
                 const ArchivePropertyDisplayGroup display_group,
                 const std::optional<uint32_t> level,
                 const std::optional<uint32_t>& prop_id,
                 std::string name,
                 std::string value) {
  ArchivePropertyLine line;
  line.kind = kind;
  line.section = section;
  line.display_group = display_group;
  line.level = level;
  line.prop_id = prop_id;
  line.name = std::move(name);
  line.value = std::move(value);
  out_lines.push_back(std::move(line));
}

void append_separator(std::vector<ArchivePropertyLine>& out_lines,
                      const PropertyLineKind kind,
                      const ArchivePropertySection section,
                      const ArchivePropertyDisplayGroup display_group,
                      const std::optional<uint32_t> level) {
  append_line(out_lines, kind, section, display_group, level);
}

void append_property_variant_line(std::vector<ArchivePropertyLine>& out_lines,
                                  const ArchivePropertySection section,
                                  const ArchivePropertyDisplayGroup display_group,
                                  const std::optional<uint32_t> level,
                                  const PROPID prop_id,
                                  const wchar_t* name,
                                  const NWindows::NCOM::CPropVariant& prop) {
  if (prop.vt == VT_EMPTY) {
    return;
  }

  std::string value;
  UInt64 converted = 0;
  if (is_size_prop(prop_id) && ConvertPropVariantToUInt64(prop, converted)) {
    value = format_grouped_uint64(static_cast<uint64_t>(converted));
  } else {
    UString converted_text;
    ConvertPropertyToString2(converted_text, prop, prop_id, 9);
    value = z7::common::trim_ascii_space_copy(ustring_to_utf8(converted_text));
  }

  if (value.empty()) {
    return;
  }

  append_line(out_lines,
              PropertyLineKind::kPair,
              section,
              display_group,
              level,
              static_cast<uint32_t>(prop_id),
              property_name_utf8(prop_id, name),
              value);
}

std::string raw_property_data_to_text(const PROPID prop_id,
                                      const void* data,
                                      const UInt32 data_size) {
  if (data == nullptr || data_size == 0) {
    return {};
  }

  if (prop_id == kpidNtSecure) {
    AString sec;
    ConvertNtSecureToString(static_cast<const Byte*>(data), data_size, sec);
    return astring_to_std(sec);
  }

  if (data_size > kRawDataPrintMaxSize) {
    return std::string("data:") + std::to_string(data_size);
  }

  char temp[kRawDataPrintMaxSize * 2 + 2];
  if (data_size <= 8 && (prop_id == kpidCRC || prop_id == kpidChecksum)) {
    ConvertDataToHex_Upper(temp, static_cast<const Byte*>(data), data_size);
  } else {
    ConvertDataToHex_Lower(temp, static_cast<const Byte*>(data), data_size);
  }
  return temp;
}

}  // namespace z7::app::info_properties_detail
