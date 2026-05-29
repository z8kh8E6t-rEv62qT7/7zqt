// src/archive_application/src/native_7z/core/event.cpp
// Role: Shared codec/open-type helpers and lifecycle utility functions.

#include "core/internal.h"

namespace z7::app
{

    int load_codecs_shared(CCodecs& codecs)
    {
        return codecs.Load();
    }

    int prepare_open_types_for_archive(std::string const& archive_type_hint,
                                       CCodecs& codecs,
                                       CObjectVector<COpenType>& types)
    {
        types.Clear();
        if (!archive_type_hint.empty())
        {
            UString const format = utf8_to_ustring(archive_type_hint);
            if (!ParseOpenTypes(codecs, format, types) || types.IsEmpty())
            {
                return E_INVALIDARG;
            }
            return S_OK;
        }

        types.Add(COpenType());
        types.Back().Recursive = false;
        return S_OK;
    }

    void emit_hash_progress(ArchiveBackendHooks const& hooks,
                            std::string const& line,
                            bool totals_known,
                            uint64_t total_bytes,
                            uint64_t completed_bytes,
                            uint64_t total_files,
                            uint64_t completed_files,
                            uint64_t error_count,
                            std::string const& current_path)
    {
        int percent = -1;
        if (totals_known && total_bytes != 0)
        {
            percent = static_cast<int>((completed_bytes * 100) / total_bytes);
        }
        else if (totals_known && total_files != 0)
        {
            percent = static_cast<int>((completed_files * 100) / total_files);
        }

        emit_progress_event(hooks,
                            OperationStage::kRunning,
                            percent,
                            totals_known,
                            total_bytes,
                            completed_bytes,
                            total_files,
                            completed_files,
                            error_count,
                            current_path,
                            line);
        if (!line.empty())
        {
            emit_log_event(hooks, OperationStage::kRunning, OutputChannel::kNone, line);
        }
    }

} // namespace z7::app
