// src/archive_application/src/native_7z/third_party_adapter/callbacks_extract_stream.h
// Role: Output stream callback declarations for archive extraction.

#pragma once

namespace z7::app {

class NativeFileOutStream final : public ISequentialOutStream {
 public:
  explicit NativeFileOutStream(fs::path path);

  HRESULT open();
  HRESULT Close();

  STDMETHOD(QueryInterface)(REFIID iid, void** out_object) throw() override;
  STDMETHOD_(ULONG, AddRef)() throw() override;
  STDMETHOD_(ULONG, Release)() throw() override;

  STDMETHOD(Write)(const void* data,
                   UInt32 size,
                   UInt32* processed_size) throw() override;

 private:
  std::atomic<ULONG> ref_count_{1};
  fs::path path_;
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

  STDMETHOD(Write)(const void* data,
                   UInt32 size,
                   UInt32* processed_size) throw() override;

 private:
  std::atomic<ULONG> ref_count_{1};
  std::vector<uint8_t>& sink_;
  size_t max_size_;
};

}  // namespace z7::app
