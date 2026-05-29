// src/archive_application/src/native_7z/third_party_adapter/third_party_fwd.h
// Role: Forward declarations and primitive aliases for original 7-Zip types.

#pragma once

#include <cstdint>

using UInt32 = std::uint32_t;
using UInt64 = std::uint64_t;
using Int32 = std::int32_t;
using PROPID = std::uint32_t;

class AString;
class UString;
class CCodecs;
struct CArchiveLink;
struct CHashBundle;
struct CUpdateErrorInfo;

struct IInArchive;

struct CProxyDir;
struct CProxyDir2;
class CArc;

namespace NWindows::NCOM {
class CPropVariant;
}  // namespace NWindows::NCOM
