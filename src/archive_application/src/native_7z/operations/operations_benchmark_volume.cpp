// src/archive_application/src/native_7z/operations/operations_benchmark_volume.cpp
// Role: Native benchmark and volume operations (benchmark/split/combine).

#include "core/internal.h"
#include "third_party_adapter/callbacks_update.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {
    namespace {

        struct ExactCopyStatus {
            uint64_t copied_bytes = 0;
            bool canceled = false;
            std::string io_error_message;
        };

        template <typename OnChunkCopied>
        ExactCopyStatus copy_exact_bytes_with_progress(std::istream& in,
                                                       std::ostream& out,
                                                       uint64_t bytes_to_copy,
                                                       std::vector<char>& buffer,
                                                       std::atomic<bool>& cancel_requested,
                                                       OnChunkCopied&& on_chunk_copied) {
            ExactCopyStatus status;
            while (status.copied_bytes < bytes_to_copy) {
                if (cancel_requested.load()) {
                    status.canceled = true;
                    return status;
                }

                uint64_t const remaining = bytes_to_copy - status.copied_bytes;
                uint64_t const want = std::min<uint64_t>(remaining, static_cast<uint64_t>(buffer.size()));
                in.read(buffer.data(), static_cast<std::streamsize>(want));
                std::streamsize const got = in.gcount();
                if (got <= 0) {
                    status.io_error_message = in.bad() ? "File read error" : "Unexpected end of source file";
                    return status;
                }

                out.write(buffer.data(), got);
                if (!out) {
                    status.io_error_message = "File write error";
                    return status;
                }

                uint64_t const wrote = static_cast<uint64_t>(got);
                status.copied_bytes += wrote;
                on_chunk_copied(wrote);
            }
            return status;
        }

        struct CombinePartInfo {
            fs::path path;
            uint64_t size = 0;
        };

        std::string summary_line_from_typed(std::optional<BenchmarkTypedSummary> const& summary) {
            if (!summary.has_value()) {
                return {};
            }
            if (summary->has_total_rating) {
                return "Tot: "
                     + std::to_string(summary->total_cpu_usage_percent)
                     + " "
                     + std::to_string(summary->total_rpu_mips)
                     + " "
                     + std::to_string(summary->total_rating_mips);
            }
            if (summary->has_average_metrics) {
                BenchmarkTypedMetrics const& m = summary->average_metrics;
                return "Avr: "
                     + std::to_string(m.compress_speed_kb_per_sec)
                     + " "
                     + std::to_string(m.compress_cpu_usage_percent)
                     + " "
                     + std::to_string(m.compress_rpu_mips)
                     + " "
                     + std::to_string(m.compress_rating_mips)
                     + " | "
                     + std::to_string(m.decompress_speed_kb_per_sec)
                     + " "
                     + std::to_string(m.decompress_cpu_usage_percent)
                     + " "
                     + std::to_string(m.decompress_rpu_mips)
                     + " "
                     + std::to_string(m.decompress_rating_mips);
            }
            return {};
        }

    } // namespace

    BenchmarkResult NativeArchiveBackend::benchmark(BenchmarkRequest const& request, ArchiveBackendHooks const& hooks) {
        CObjectVector<CProperty> props;
        if (!is_auto_value(request.thread_count)) {
            CProperty property;
            property.Name = "mt";
            property.Value = utf8_to_ustring(z7::common::trim_ascii_space_copy(request.thread_count));
            props.Add(property);
        }

        if (!is_auto_value(request.dictionary_size)) {
            CProperty property;
            property.Name = "d";
            property.Value = utf8_to_ustring(z7::common::trim_ascii_space_copy(request.dictionary_size));
            props.Add(property);
        }

        if (request.total_mode) {
            CProperty property;
            property.Name = "m";
            property.Value = "*";
            props.Add(property);
        } else if (!z7::common::trim_ascii_space_copy(request.method_value).empty()) {
            CProperty property;
            property.Name = "m";
            property.Value = utf8_to_ustring(z7::common::trim_ascii_space_copy(request.method_value));
            props.Add(property);
        }

        BenchmarkResult result;
        std::optional<BenchmarkTypedSummary> typed_summary;
        UInt32 const iterations =
            request.iterations == 0 ? static_cast<UInt32>(kDefaultBenchIterations) : request.iterations;
        HRESULT bench_res = E_FAIL;
        if (request.total_mode) {
            BenchmarkTypedParser parser;
            NativeBenchmarkPrintCallback print_callback(
                hooks, &cancel_requested_, [this]() { return this->wait_while_paused(); }, &parser);
            bench_res = Bench(EXTERNAL_CODECS_VARS_L & print_callback, nullptr, props, iterations, true, nullptr);
            print_callback.flush_pending();
            typed_summary = parser.summary();
            result.summary_line = parser.summary_line();
        } else {
            uint64_t const dictionary_size = parse_size_to_bytes_or_default(request.dictionary_size, 32ULL << 20);
            NativeBenchStructuredCallback structured_callback(
                hooks, &cancel_requested_, [this]() { return this->wait_while_paused(); }, dictionary_size);
            NativeBenchFreqCallback freq_callback(
                hooks, &cancel_requested_, [this]() { return this->wait_while_paused(); });
            bench_res =
                Bench(EXTERNAL_CODECS_VARS_L nullptr, &structured_callback, props, iterations, false, &freq_callback);
            typed_summary = structured_callback.summary();
            result.summary_line = summary_line_from_typed(typed_summary);
        }

        return finalize_hresult_operation_result<BenchmarkResult>(
            cancel_requested_,
            bench_res,
            result.summary_line,
            [&](BenchmarkResult& failure) { failure.typed_summary = typed_summary; },
            [&](BenchmarkResult& success) {
                success.summary_line = result.summary_line;
                success.typed_summary = typed_summary;
            });
    }

    // Split/combine operations share the same cancel/progress semantics.

    SplitResult NativeArchiveBackend::split(SplitRequest const& request, ArchiveBackendHooks const& hooks) {
        std::vector<uint64_t> volume_sizes;
        if (!parse_volume_sizes_spec(request.volume_size_spec, volume_sizes) || volume_sizes.empty()) {
            return from_base_result<SplitResult>(invalid_request("Invalid split volume size specification"));
        }

        std::error_code ec;
        fs::path const source_path(request.source_file_path);
        if (!fs::exists(source_path, ec) || !fs::is_regular_file(source_path, ec)) {
            return from_base_result<SplitResult>(invalid_request("Split source must be an existing file"));
        }

        uint64_t const source_size = fs::file_size(source_path, ec);
        if (ec) {
            return make_operation_failure<SplitResult>(ArchiveErrorDomain::kIo, ec.message(), 2);
        }

        if (source_size <= volume_sizes.front()) {
            return from_base_result<SplitResult>(
                invalid_request("Split volume size must be smaller than source file size"));
        }

        uint64_t const num_volumes = get_number_of_volumes(source_size, volume_sizes);
        if (num_volumes == std::numeric_limits<uint64_t>::max()) {
            return from_base_result<SplitResult>(invalid_request("Invalid split volume size"));
        }

        fs::path const output_dir(request.output_dir);
        fs::create_directories(output_dir, ec);
        if (ec) {
            return make_operation_failure<SplitResult>(ArchiveErrorDomain::kIo, ec.message(), 2);
        }

        std::ifstream in(source_path, std::ios::binary);
        if (!in) {
            return make_operation_failure<SplitResult>(ArchiveErrorDomain::kIo, "Failed to open source file", 2);
        }

        SplitResult result;
        result.output_path = (output_dir / source_path.filename()).string();
        result.total_bytes = source_size;

        size_t const digits = volume_number_digits(num_volumes);
        std::vector<char> buffer(1 << 20);
        uint64_t completed = 0;
        uint64_t volume_index = 0;

        while (completed < source_size) {
            if (cancel_requested_.load()) {
                return make_operation_canceled<SplitResult>();
            }

            uint64_t const volume_size = (volume_index < volume_sizes.size())
                                           ? volume_sizes[static_cast<size_t>(volume_index)]
                                           : volume_sizes.back();
            if (volume_size == 0) {
                return from_base_result<SplitResult>(invalid_request("Split volume size must not be zero"));
            }

            std::string const part_suffix = volume_sequence_name(volume_index + 1, digits);
            fs::path const part_path = output_dir / (source_path.filename().string() + "." + part_suffix);
            if (fs::exists(part_path, ec)) {
                return make_operation_failure<SplitResult>(
                    ArchiveErrorDomain::kIo, "Split output already exists: " + part_path.string(), 2);
            }

            std::ofstream out(part_path, std::ios::binary | std::ios::trunc);
            if (!out) {
                return make_operation_failure<SplitResult>(
                    ArchiveErrorDomain::kIo, "Failed to create split part: " + part_path.string(), 2);
            }

            uint64_t const bytes_to_write = std::min<uint64_t>(volume_size, source_size - completed);
            ExactCopyStatus const copy_status =
                copy_exact_bytes_with_progress(in, out, bytes_to_write, buffer, cancel_requested_, [&](uint64_t wrote) {
                    completed += wrote;
                    emit_progress_event(hooks,
                                        OperationStage::kRunning,
                                        source_size == 0 ? -1 : static_cast<int>((completed * 100) / source_size),
                                        true,
                                        source_size,
                                        completed,
                                        0,
                                        volume_index,
                                        0,
                                        part_path.string(),
                                        {});
                });
            if (copy_status.canceled) {
                return make_operation_canceled<SplitResult>();
            }
            if (!copy_status.io_error_message.empty()) {
                return make_operation_failure<SplitResult>(ArchiveErrorDomain::kIo, copy_status.io_error_message, 2);
            }

            out.close();
            if (!out) {
                return make_operation_failure<SplitResult>(ArchiveErrorDomain::kIo, "File write error", 2);
            }
            result.generated_volume_paths.push_back(part_path.string());
            ++volume_index;
        }

        std::vector<std::string> generated_volume_paths = std::move(result.generated_volume_paths);
        result = make_operation_success<SplitResult>("Split completed");
        result.output_path = (output_dir / source_path.filename()).string();
        result.total_bytes = source_size;
        result.generated_volume_paths = std::move(generated_volume_paths);
        result.volume_count = static_cast<uint64_t>(result.generated_volume_paths.size());
        return result;
    }

    // Combine implementation stays in the same translation unit as split/benchmark.

    CombineResult NativeArchiveBackend::combine(CombineRequest const& request, ArchiveBackendHooks const& hooks) {
        fs::path const source_part(request.source_part_path);
        std::error_code ec;
        if (!fs::exists(source_part, ec) || !fs::is_regular_file(source_part, ec)) {
            return from_base_result<CombineResult>(invalid_request("Combine source must be an existing file"));
        }

        VolumeSequenceState seq;
        std::string const file_name = source_part.filename().string();
        if (!seq.parse_name(file_name)) {
            return from_base_result<CombineResult>(
                invalid_request("Cannot detect split file sequence from selected file"));
        }

        fs::path const input_dir = source_part.parent_path();
        std::vector<CombinePartInfo> parts;
        uint64_t total_size = 0;
        std::string next_name = file_name;
        for (;;) {
            fs::path const candidate = input_dir / next_name;
            if (!fs::exists(candidate, ec) || fs::is_directory(candidate, ec)) {
                break;
            }
            uint64_t const part_size = fs::file_size(candidate, ec);
            if (ec) {
                return make_operation_failure<CombineResult>(ArchiveErrorDomain::kIo, ec.message(), 2);
            }
            parts.push_back(CombinePartInfo{candidate, part_size});
            total_size += part_size;
            next_name = seq.next_name();
        }

        if (parts.size() <= 1) {
            return from_base_result<CombineResult>(invalid_request("Cannot find more than one split part"));
        }
        if (total_size == 0) {
            return from_base_result<CombineResult>(invalid_request("No data"));
        }

        fs::create_directories(fs::path(request.output_dir), ec);
        if (ec) {
            return make_operation_failure<CombineResult>(ArchiveErrorDomain::kIo, ec.message(), 2);
        }

        std::string output_name = seq.unchanged_part;
        while (!output_name.empty() && output_name.back() == '.') {
            output_name.pop_back();
        }
        if (output_name.empty()) {
            output_name = "file";
        }

        fs::path const output_path = fs::path(request.output_dir) / output_name;
        if (fs::exists(output_path, ec)) {
            return make_operation_failure<CombineResult>(
                ArchiveErrorDomain::kIo, "Destination file already exists: " + output_path.string(), 2);
        }

        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return make_operation_failure<CombineResult>(
                ArchiveErrorDomain::kIo, "Failed to create destination file", 2);
        }

        std::vector<char> buffer(1 << 20);
        uint64_t completed = 0;
        CombineResult result;
        result.output_path = output_path.string();
        result.total_bytes = total_size;
        for (CombinePartInfo const& part : parts) {
            if (cancel_requested_.load()) {
                return make_operation_canceled<CombineResult>();
            }

            std::ifstream in(part.path, std::ios::binary);
            if (!in) {
                return make_operation_failure<CombineResult>(
                    ArchiveErrorDomain::kIo, "Failed to open split part: " + part.path.string(), 2);
            }

            ExactCopyStatus const copy_status =
                copy_exact_bytes_with_progress(in, out, part.size, buffer, cancel_requested_, [&](uint64_t wrote) {
                    completed += wrote;
                    emit_progress_event(hooks,
                                        OperationStage::kRunning,
                                        total_size == 0 ? -1 : static_cast<int>((completed * 100) / total_size),
                                        true,
                                        total_size,
                                        completed,
                                        static_cast<uint64_t>(parts.size()),
                                        result.input_volume_paths.size(),
                                        0,
                                        part.path.string(),
                                        {});
                });
            if (copy_status.canceled) {
                return make_operation_canceled<CombineResult>();
            }
            if (!copy_status.io_error_message.empty()) {
                return make_operation_failure<CombineResult>(ArchiveErrorDomain::kIo, copy_status.io_error_message, 2);
            }

            result.input_volume_paths.push_back(part.path.string());
        }

        out.close();
        if (!out) {
            return make_operation_failure<CombineResult>(ArchiveErrorDomain::kIo, "File write error", 2);
        }
        std::vector<std::string> input_volume_paths = std::move(result.input_volume_paths);
        result = make_operation_success<CombineResult>("Combine completed");
        result.output_path = output_path.string();
        result.total_bytes = total_size;
        result.input_volume_paths = std::move(input_volume_paths);
        result.volume_count = static_cast<uint64_t>(result.input_volume_paths.size());
        return result;
    }

} // namespace z7::app
