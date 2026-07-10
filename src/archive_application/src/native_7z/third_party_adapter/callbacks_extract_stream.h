// src/archive_application/src/native_7z/third_party_adapter/callbacks_extract_stream.h
// Role: Output stream callback declarations for archive extraction.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

    class NativeFileOutStream final : public ISequentialOutStream {
    public:
        explicit NativeFileOutStream(std::filesystem::path path);

        HRESULT open();
        HRESULT Close();

        STDMETHOD(QueryInterface)(REFIID iid, void** out_object) throw() override;
        STDMETHOD_(ULONG, AddRef)() throw() override;
        STDMETHOD_(ULONG, Release)() throw() override;

        STDMETHOD(Write)(void const* data, UInt32 size, UInt32* processed_size) throw() override;

    private:
        std::atomic<ULONG> ref_count_{1};
        std::filesystem::path path_;
        std::ofstream file_;
    };

    // Writes extracted data into a caller-owned buffer up to max_size.
    // Returns E_OUTOFMEMORY once the limit is exceeded, which signals the
    // caller to abandon the in-memory strategy and fall back to a temp file.
    class NativeBufferOutStream final : public ISequentialOutStream {
    public:
        NativeBufferOutStream(std::vector<uint8_t>& sink, size_t max_size);

        STDMETHOD(QueryInterface)(REFIID iid, void** out_object) throw() override;
        STDMETHOD_(ULONG, AddRef)() throw() override;
        STDMETHOD_(ULONG, Release)() throw() override;

        STDMETHOD(Write)(void const* data, UInt32 size, UInt32* processed_size) throw() override;

    private:
        std::atomic<ULONG> ref_count_{1};
        std::vector<uint8_t>& sink_;
        size_t max_size_;
    };

} // namespace z7::app
