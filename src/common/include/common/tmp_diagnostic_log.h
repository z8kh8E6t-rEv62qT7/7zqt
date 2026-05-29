#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace z7::common {

// Internal utility for temporary single-line diagnostics written to /tmp.
// Production code may choose not to reference this helper at all; the helper
// remains available for future targeted investigations without Qt dependencies.
struct TmpDiagnosticLogContext final {
  std::optional<uint64_t> session_id;
  std::optional<uint32_t> generation;
};

struct TmpDiagnosticLogField final {
  std::string key;
  std::string value;
};

namespace tmp_diagnostic_log_detail {

inline constexpr std::string_view kTmpDiagnosticLogPath = "/tmp/7z.log";

inline int current_process_id() {
#ifdef _WIN32
  return _getpid();
#else
  return static_cast<int>(::getpid());
#endif
}

inline std::mutex& log_mutex() {
  static std::mutex mutex;
  return mutex;
}

inline std::string sanitize_single_line(std::string_view value) {
  std::string sanitized;
  sanitized.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '\r':
      case '\n':
      case '\t':
        sanitized.push_back(' ');
        break;
      default:
        sanitized.push_back(ch);
        break;
    }
  }
  return sanitized;
}

inline std::string current_timestamp_string() {
  using namespace std::chrono;
  const system_clock::time_point now = system_clock::now();
  const milliseconds millis = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t now_time = system_clock::to_time_t(now);
  std::tm tm_snapshot{};
#ifdef _WIN32
  if (localtime_s(&tm_snapshot, &now_time) != 0) {
    return "1970-01-01T00:00:00.000";
  }
#else
  if (localtime_r(&now_time, &tm_snapshot) == nullptr) {
    return "1970-01-01T00:00:00.000";
  }
#endif

  std::ostringstream out;
  out << std::put_time(&tm_snapshot, "%Y-%m-%dT%H:%M:%S")
      << '.'
      << std::setfill('0')
      << std::setw(3)
      << millis.count();
  return out.str();
}

inline void append_field_text(std::ostringstream& out,
                              std::string_view key,
                              std::string_view value) {
  const std::string sanitized_key = sanitize_single_line(key);
  if (sanitized_key.empty()) {
    return;
  }
  out << sanitized_key << '=' << sanitize_single_line(value);
}

inline TmpDiagnosticLogField make_field(std::string_view key,
                                        std::string_view value) {
  return TmpDiagnosticLogField{std::string(sanitize_single_line(key)),
                               std::string(sanitize_single_line(value))};
}

}  // namespace tmp_diagnostic_log_detail

inline std::string_view tmp_diagnostic_log_path() noexcept {
  return tmp_diagnostic_log_detail::kTmpDiagnosticLogPath;
}

inline TmpDiagnosticLogField make_tmp_diagnostic_log_field(
    std::string_view key,
    std::string_view value) {
  return tmp_diagnostic_log_detail::make_field(key, value);
}

inline TmpDiagnosticLogField make_tmp_diagnostic_log_field(
    std::string_view key,
    const std::string& value) {
  return tmp_diagnostic_log_detail::make_field(key, value);
}

inline TmpDiagnosticLogField make_tmp_diagnostic_log_field(
    std::string_view key,
    const char* value) {
  return tmp_diagnostic_log_detail::make_field(
      key, value != nullptr ? std::string_view(value) : std::string_view());
}

template <typename TValue>
inline TmpDiagnosticLogField make_tmp_diagnostic_log_field(
    std::string_view key,
    const TValue& value) {
  std::ostringstream out;
  out << value;
  return tmp_diagnostic_log_detail::make_field(key, out.str());
}

inline std::string format_tmp_diagnostic_log_fields(
    std::initializer_list<TmpDiagnosticLogField> fields) {
  std::ostringstream out;
  bool first = true;
  for (const TmpDiagnosticLogField& field : fields) {
    if (field.key.empty()) {
      continue;
    }
    if (!first) {
      out << ' ';
    }
    first = false;
    tmp_diagnostic_log_detail::append_field_text(out, field.key, field.value);
  }
  return out.str();
}

inline std::string format_tmp_diagnostic_log_fields(
    const std::vector<TmpDiagnosticLogField>& fields) {
  std::ostringstream out;
  bool first = true;
  for (const TmpDiagnosticLogField& field : fields) {
    if (field.key.empty()) {
      continue;
    }
    if (!first) {
      out << ' ';
    }
    first = false;
    tmp_diagnostic_log_detail::append_field_text(out, field.key, field.value);
  }
  return out.str();
}

inline TmpDiagnosticLogContext make_tmp_diagnostic_log_context(
    std::optional<uint64_t> session_id = std::nullopt,
    std::optional<uint32_t> generation = std::nullopt) {
  TmpDiagnosticLogContext context;
  context.session_id = session_id;
  context.generation = generation;
  return context;
}

inline void append_tmp_diagnostic_log(
    std::string_view stage,
    std::string_view detail = {},
    const TmpDiagnosticLogContext& context = {}) noexcept {
  try {
    std::ostringstream thread_stream;
    thread_stream << std::this_thread::get_id();

    const std::string sanitized_stage =
        tmp_diagnostic_log_detail::sanitize_single_line(stage);
    const std::string sanitized_detail =
        tmp_diagnostic_log_detail::sanitize_single_line(detail);

    std::lock_guard<std::mutex> lock(tmp_diagnostic_log_detail::log_mutex());
    std::ofstream output(
        std::string(tmp_diagnostic_log_detail::kTmpDiagnosticLogPath),
        std::ios::out | std::ios::app);
    if (!output.is_open()) {
      return;
    }

    output << tmp_diagnostic_log_detail::current_timestamp_string()
           << " pid=" << tmp_diagnostic_log_detail::current_process_id()
           << " tid=" << thread_stream.str();
    if (context.session_id.has_value()) {
      output << " session=" << *context.session_id;
    }
    if (context.generation.has_value()) {
      output << " generation=" << *context.generation;
    }
    output << " stage=" << sanitized_stage;
    if (!sanitized_detail.empty()) {
      output << ' ' << sanitized_detail;
    }
    output << '\n';
  } catch (...) {
  }
}

inline void append_tmp_diagnostic_log_fields(
    std::string_view stage,
    std::initializer_list<TmpDiagnosticLogField> fields,
    const TmpDiagnosticLogContext& context = {}) noexcept {
  append_tmp_diagnostic_log(stage, format_tmp_diagnostic_log_fields(fields), context);
}

inline void append_tmp_diagnostic_log_fields(
    std::string_view stage,
    const std::vector<TmpDiagnosticLogField>& fields,
    const TmpDiagnosticLogContext& context = {}) noexcept {
  append_tmp_diagnostic_log(stage, format_tmp_diagnostic_log_fields(fields), context);
}

}  // namespace z7::common
