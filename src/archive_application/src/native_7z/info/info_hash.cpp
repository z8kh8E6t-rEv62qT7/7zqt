// src/archive_application/src/native_7z/info/info_hash.cpp
// Role: Hash execution internals and backend factory.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

HashResult NativeArchiveBackend::run_hash_entries(
    const HashRequest& request,
    const ArchiveBackendHooks& hooks,
    const std::vector<HashInputEntry>& entries,
    const std::string& main_name) {
  uint64_t total_files = 0;
  uint64_t total_bytes = 0;
  for (const HashInputEntry& entry : entries) {
    if (entry.is_dir) {
      continue;
    }
    ++total_files;
    total_bytes += entry.file_size;
  }

  uint64_t error_count = 0;
  emit_hash_progress(
      hooks, "Hashing", true, total_bytes, 0, total_files, 0, 0, {});

#ifdef Z7_EXTERNAL_CODECS
  const HRESULT load_codecs_res = ::LoadGlobalCodecs();
  if (load_codecs_res != S_OK) {
    return make_operation_failure<HashResult>(ArchiveErrorDomain::kBackendUnavailable,
                                              "Failed to load hash codecs",
                                              2);
  }
#endif

  CHashBundle bundle;
  if (!main_name.empty()) {
    bundle.MainName = utf8_to_ustring(main_name);
  }

  UStringVector methods;
  if (!request.hash_method.empty()) {
    methods.Add(utf8_to_ustring(request.hash_method));
  }
  const HRESULT set_methods_res = bundle.SetMethods(EXTERNAL_CODECS_VARS_G methods);
  if (set_methods_res != S_OK) {
    return make_operation_failure<HashResult>(ArchiveErrorDomain::kInvalidArguments,
                                              "Unsupported hash method",
                                              2);
  }

  uint64_t completed_bytes = 0;
  uint64_t completed_files = 0;
  bool first_file_set = false;
  std::vector<char> buffer(kHashReadChunkSize);

  for (const HashInputEntry& entry : entries) {
    if (cancel_requested_.load() || !wait_while_paused()) {
      return make_operation_canceled<HashResult>();
    }

    bundle.InitForNewFile();
    if (entry.is_dir) {
      bundle.Final(true, false, utf8_to_ustring(entry.relative_path));
      continue;
    }

    std::ifstream in(entry.absolute_path, std::ios::binary);
    if (!in) {
      ++bundle.NumErrors;
      ++error_count;
      continue;
    }

    if (!first_file_set) {
      first_file_set = true;
      bundle.FirstFileName = utf8_to_ustring(entry.relative_path);
    }

    emit_hash_progress(hooks,
                       entry.relative_path,
                       true,
                       total_bytes,
                       completed_bytes,
                       total_files,
                       completed_files,
                       error_count,
                       entry.relative_path);

    uint64_t file_progress_prev = 0;
    bool file_ok = true;
    while (in) {
      if (cancel_requested_.load() || !wait_while_paused()) {
        return make_operation_canceled<HashResult>();
      }

      in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      const std::streamsize got = in.gcount();
      if (got <= 0) {
        break;
      }

      bundle.Update(buffer.data(), static_cast<UInt32>(got));
      completed_bytes += static_cast<uint64_t>(got);
      if (bundle.CurSize - file_progress_prev >= kHashProgressStepBytes) {
        file_progress_prev = bundle.CurSize;
        emit_hash_progress(hooks,
                           entry.relative_path,
                           true,
                           total_bytes,
                           completed_bytes,
                           total_files,
                           completed_files,
                           error_count,
                           entry.relative_path);
      }
    }

    if (in.bad()) {
      ++bundle.NumErrors;
      ++error_count;
      file_ok = false;
    }

    if (!file_ok) {
      continue;
    }

    bundle.Final(false, false, utf8_to_ustring(entry.relative_path));
    ++completed_files;
    emit_hash_progress(hooks,
                       entry.relative_path,
                       true,
                       total_bytes,
                       completed_bytes,
                       total_files,
                       completed_files,
                       error_count,
                       entry.relative_path);
  }

  HashResult result = make_operation_success<HashResult>("Success");
  result.summary_data = make_hash_summary(bundle);
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    result.summary_data.num_archives = 1;
  }
  result.hash_summary = result.summary_data;
  return result;
}

HashResult NativeArchiveBackend::run_hash_internal(const HashRequest& request,
                                                   const ArchiveBackendHooks& hooks) {
  emit_hash_progress(hooks, "Scanning", false, 0, 0, 0, 0, 0, {});

  std::vector<HashInputEntry> entries;
  entries.reserve(request.input_paths.size() * 2);

  for (const std::string& input : request.input_paths) {
    if (cancel_requested_.load()) {
      return make_operation_canceled<HashResult>();
    }

    uint64_t total_files_unused = 0;
    uint64_t total_bytes_unused = 0;
    const fs::path path(input);
    collect_hash_entries_for_path(
        path,
        path_leaf_name(path),
        request.recursive_dirs,
        entries,
        total_files_unused,
        total_bytes_unused);
  }

  const std::string main_name =
      request.input_paths.size() == 1
          ? path_leaf_name(fs::path(request.input_paths.front()))
          : std::string();
  return run_hash_entries(request, hooks, entries, main_name);
}

std::unique_ptr<INativeArchiveBackend> create_native_archive_backend() {
  return std::make_unique<NativeArchiveBackend>();
}

BenchmarkMemoryEstimate estimate_benchmark_memory_native(
    const BenchmarkRequest& request) {
  BenchmarkMemoryEstimate out;

  uint32_t cpu_threads = 1;
#ifndef Z7_ST
  cpu_threads = NWindows::NSystem::GetNumberOfProcessors();
#endif
  if (cpu_threads == 0) {
    cpu_threads = 1;
  }

  const uint32_t num_threads =
      std::max<uint32_t>(1, parse_thread_count_or_default(request.thread_count, cpu_threads));
  const uint64_t dictionary_size =
      parse_size_to_bytes_or_default(request.dictionary_size, 32ULL << 20);

  out.estimated_usage_bytes =
      GetBenchMemoryUsage(num_threads, -1, dictionary_size, request.total_mode);

  size_t ram_size = static_cast<size_t>(sizeof(size_t)) << 29;
  out.ram_defined = NWindows::NSystem::GetRamSize(ram_size);
  out.total_ram_bytes = static_cast<uint64_t>(ram_size);

  if (out.ram_defined) {
    out.safe_ram_limit_bytes = (out.total_ram_bytes / 16ULL) * 15ULL;
    constexpr uint64_t kSafetyMarginBytes = 1ULL << 20;
    out.within_limit =
        out.estimated_usage_bytes + kSafetyMarginBytes <= out.safe_ram_limit_bytes;
  } else {
    out.safe_ram_limit_bytes = 0;
    out.within_limit = true;
  }

  out.ok = true;
  return out;
}

BenchmarkSystemInfo query_benchmark_system_info_native() {
  BenchmarkSystemInfo out;

  UInt32 num_cpus = 1;
  UInt32 num_cpus_sys = 1;

  NWindows::NSystem::CProcessAffinity threads_info;
  threads_info.InitST();
  if (threads_info.Get()) {
    num_cpus = threads_info.GetNumProcessThreads();
    num_cpus_sys = threads_info.GetNumSystemThreads();
  }
  if (num_cpus == 0) {
    num_cpus = NWindows::NSystem::GetNumberOfProcessors();
  }
  if (num_cpus == 0) {
    num_cpus = 1;
  }
  if (num_cpus_sys < num_cpus) {
    num_cpus_sys = num_cpus;
  }
  out.process_threads = static_cast<uint32_t>(num_cpus);
  out.system_threads = static_cast<uint32_t>(num_cpus_sys);

  {
    AString s("/ ");
    s.Add_UInt32(num_cpus);
    s += GetProcessThreadsInfo(threads_info);
    out.hardware_threads = astring_to_std(s);
  }

  {
    AString s1;
    AString s2;
    GetSysInfo(s1, s2);
    out.sys1 = astring_to_std(s1);
    out.sys2 = astring_to_std(s2);
  }

  {
    AString s;
    AString registers;
    GetCpuName_MultiLine(s, registers);
    out.cpu = astring_to_std(s);
  }

  {
    AString s;
    ::GetOsInfoText(s);
    s += " : ";
    AddCpuFeatures(s);
    out.cpu_feature = astring_to_std(s);
  }

  out.version = std::string("7-Zip ") + MY_VERSION_CPU;
  return out;
}

}  // namespace z7::app
