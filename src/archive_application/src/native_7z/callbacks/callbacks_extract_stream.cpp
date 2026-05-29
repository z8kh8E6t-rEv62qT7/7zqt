// src/archive_application/src/native_7z/callbacks/callbacks_extract_stream.cpp
// Role: Output stream callback implementation for archive extraction.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract_stream.h"

namespace z7::app {

NativeFileOutStream::NativeFileOutStream(fs::path path)
    : path_(std::move(path)) {}

HRESULT NativeFileOutStream::open() {
  file_.open(path_, std::ios::binary | std::ios::trunc);
  return file_.is_open() ? S_OK : E_FAIL;
}

HRESULT NativeFileOutStream::Close() {
  if (!file_.is_open()) {
    return S_OK;
  }
  file_.close();
  return file_.fail() ? E_FAIL : S_OK;
}

STDMETHODIMP NativeFileOutStream::QueryInterface(REFIID iid,
                                                 void** out_object) throw() {
  if (out_object == nullptr) {
    return E_INVALIDARG;
  }
  *out_object = nullptr;

  if (iid == IID_IUnknown || iid == IID_ISequentialOutStream) {
    *out_object = static_cast<ISequentialOutStream*>(this);
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) NativeFileOutStream::AddRef() throw() {
  return ++ref_count_;
}

STDMETHODIMP_(ULONG) NativeFileOutStream::Release() throw() {
  const ULONG next = --ref_count_;
  if (next == 0) {
    delete this;
  }
  return next;
}

STDMETHODIMP NativeFileOutStream::Write(const void* data,
                                        UInt32 size,
                                        UInt32* processed_size) throw() {
  if (processed_size != nullptr) {
    *processed_size = 0;
  }
  if (size == 0) {
    return S_OK;
  }
  if (!file_.is_open()) {
    return E_FAIL;
  }

  file_.write(reinterpret_cast<const char*>(data),
              static_cast<std::streamsize>(size));
  if (!file_) {
    return E_FAIL;
  }
  if (processed_size != nullptr) {
    *processed_size = size;
  }
  return S_OK;
}

NativeBufferOutStream::NativeBufferOutStream(std::vector<uint8_t>& sink,
                                             size_t max_size)
    : sink_(sink), max_size_(max_size) {}

STDMETHODIMP NativeBufferOutStream::QueryInterface(REFIID iid,
                                                   void** out_object) throw() {
  if (out_object == nullptr) {
    return E_INVALIDARG;
  }
  *out_object = nullptr;

  if (iid == IID_IUnknown || iid == IID_ISequentialOutStream) {
    *out_object = static_cast<ISequentialOutStream*>(this);
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) NativeBufferOutStream::AddRef() throw() {
  return ++ref_count_;
}

STDMETHODIMP_(ULONG) NativeBufferOutStream::Release() throw() {
  const ULONG next = --ref_count_;
  if (next == 0) {
    delete this;
  }
  return next;
}

STDMETHODIMP NativeBufferOutStream::Write(const void* data,
                                          UInt32 size,
                                          UInt32* processed_size) throw() {
  if (processed_size != nullptr) {
    *processed_size = 0;
  }
  if (size == 0) {
    return S_OK;
  }
  const size_t current = sink_.size();
  if (current > max_size_ || size > max_size_ - current) {
    return E_OUTOFMEMORY;
  }
  const auto* bytes = static_cast<const uint8_t*>(data);
  sink_.insert(sink_.end(), bytes, bytes + size);
  if (processed_size != nullptr) {
    *processed_size = size;
  }
  return S_OK;
}

}  // namespace z7::app
