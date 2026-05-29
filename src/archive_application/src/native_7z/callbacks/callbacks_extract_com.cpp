// src/archive_application/src/native_7z/callbacks/callbacks_extract_com.cpp
// Role: Extract callback COM identity and lifetime methods.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract_run.h"

namespace z7::app {

STDMETHODIMP NativeExtractCallback::QueryInterface(REFIID iid,
                                                   void** out_object) throw() {
  if (out_object == nullptr) {
    return E_INVALIDARG;
  }
  *out_object = nullptr;

  if (iid == IID_IUnknown ||
      iid == IID_IProgress ||
      iid == IID_IArchiveExtractCallback) {
    *out_object = static_cast<IArchiveExtractCallback*>(this);
  } else if (iid == IID_ICryptoGetTextPassword) {
    *out_object = static_cast<ICryptoGetTextPassword*>(this);
  } else if (iid == IID_ICompressProgressInfo) {
    *out_object = static_cast<ICompressProgressInfo*>(this);
  } else if (iid == IID_IArchiveRequestMemoryUseCallback) {
    *out_object = static_cast<IArchiveRequestMemoryUseCallback*>(this);
  } else {
    return E_NOINTERFACE;
  }

  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) NativeExtractCallback::AddRef() throw() {
  return ++ref_count_;
}

STDMETHODIMP_(ULONG) NativeExtractCallback::Release() throw() {
  const ULONG next = --ref_count_;
  if (next == 0) {
    delete this;
  }
  return next;
}

}  // namespace z7::app
