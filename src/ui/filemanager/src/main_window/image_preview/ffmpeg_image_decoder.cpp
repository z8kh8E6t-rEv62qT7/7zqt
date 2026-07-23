#include "main_window/image_preview/ffmpeg_image_decoder.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace z7::ui::filemanager {
    namespace {

        constexpr int kIoBufferSize = 32 * 1024;
        constexpr int kDefaultFrameDurationMs = 100;
        constexpr int kMinimumFrameDurationMs = 20;
        constexpr int kMaximumFrameDurationMs = 10'000;

        QString ffmpeg_error(QString const& prefix, int error_code) {
            std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
            av_strerror(error_code, buffer.data(), buffer.size());
            return QStringLiteral("%1: %2").arg(prefix, QString::fromUtf8(buffer.data()));
        }

        struct MemoryInput {
            std::shared_ptr<const std::vector<uint8_t>> bytes;
            size_t position = 0;
            std::atomic<bool> const* cancel_requested = nullptr;
            bool secondary_io_attempted = false;
        };

        bool is_canceled(MemoryInput const& input) {
            return input.cancel_requested != nullptr && input.cancel_requested->load();
        }

        int read_packet(void* opaque, uint8_t* destination, int destination_size) {
            auto* input = static_cast<MemoryInput*>(opaque);
            if (input == nullptr || destination == nullptr || destination_size <= 0) {
                return AVERROR(EINVAL);
            }
            if (is_canceled(*input)) {
                return AVERROR_EXIT;
            }
            size_t const remaining = input->bytes->size() - input->position;
            if (remaining == 0) {
                return AVERROR_EOF;
            }
            size_t const count = std::min(remaining, static_cast<size_t>(destination_size));
            std::memcpy(destination, input->bytes->data() + input->position, count);
            input->position += count;
            return static_cast<int>(count);
        }

        int64_t seek_input(void* opaque, int64_t offset, int whence) {
            auto* input = static_cast<MemoryInput*>(opaque);
            if (input == nullptr) {
                return AVERROR(EINVAL);
            }
            if (is_canceled(*input)) {
                return AVERROR_EXIT;
            }
            if ((whence & AVSEEK_SIZE) != 0) {
                if (input->bytes->size() > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
                    return AVERROR(EOVERFLOW);
                }
                return static_cast<int64_t>(input->bytes->size());
            }

            int64_t base = 0;
            switch (whence & ~AVSEEK_FORCE) {
                case SEEK_SET:
                    base = 0;
                    break;
                case SEEK_CUR:
                    base = static_cast<int64_t>(input->position);
                    break;
                case SEEK_END:
                    base = static_cast<int64_t>(input->bytes->size());
                    break;
                default:
                    return AVERROR(EINVAL);
            }
            if ((offset > 0 && base > std::numeric_limits<int64_t>::max() - offset)
                || (offset < 0 && base < std::numeric_limits<int64_t>::min() - offset)) {
                return AVERROR(EOVERFLOW);
            }
            int64_t const next = base + offset;
            if (next < 0 || static_cast<uint64_t>(next) > input->bytes->size()) {
                return AVERROR(EINVAL);
            }
            input->position = static_cast<size_t>(next);
            return next;
        }

        int reject_secondary_io(AVFormatContext* context,
                                AVIOContext** output,
                                char const*,
                                int,
                                AVDictionary**) {
            if (output != nullptr) {
                *output = nullptr;
            }
            if (context != nullptr && context->opaque != nullptr) {
                static_cast<MemoryInput*>(context->opaque)->secondary_io_attempted = true;
            }
            return AVERROR(EPERM);
        }

        FfmpegImageDecodeResult prohibited_io_result() {
            return {{}, QStringLiteral("FFmpeg image attempted prohibited file or network I/O"), false};
        }

        struct FormatContextDeleter {
            void operator()(AVFormatContext* context) const {
                if (context != nullptr) {
                    avformat_close_input(&context);
                }
            }
        };

        struct AvioContextDeleter {
            void operator()(AVIOContext* context) const {
                if (context != nullptr) {
                    avio_context_free(&context);
                }
            }
        };

        struct CodecContextDeleter {
            void operator()(AVCodecContext* context) const {
                if (context != nullptr) {
                    avcodec_free_context(&context);
                }
            }
        };

        struct FrameDeleter {
            void operator()(AVFrame* frame) const {
                if (frame != nullptr) {
                    av_frame_free(&frame);
                }
            }
        };

        struct PacketDeleter {
            void operator()(AVPacket* packet) const {
                if (packet != nullptr) {
                    av_packet_free(&packet);
                }
            }
        };

        struct SwsContextDeleter {
            void operator()(SwsContext* context) const { sws_freeContext(context); }
        };

        using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
        using AvioContextPtr = std::unique_ptr<AVIOContext, AvioContextDeleter>;
        using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
        using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
        using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
        using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

        struct StreamDecoder {
            unsigned int stream_index = 0;
            AVRational time_base{0, 1};
            CodecContextPtr codec;
            SwsContextPtr scaler;
            unsigned int tiff_page_count = 1;
            bool tiff_pages_expanded = false;
        };

        void observe_tiff_page_count(StreamDecoder& decoder, AVFrame const& frame) {
            if (decoder.codec->codec_id != AV_CODEC_ID_TIFF || frame.metadata == nullptr) {
                return;
            }
            AVDictionaryEntry const* page_number = av_dict_get(frame.metadata, "page_number", nullptr, 0);
            if (page_number == nullptr || page_number->value == nullptr) {
                return;
            }
            QString const value = QString::fromUtf8(page_number->value);
            qsizetype const separator = value.lastIndexOf(QLatin1Char('/'));
            if (separator < 0) {
                return;
            }
            bool ok = false;
            uint const page_count = value.sliced(separator + 1).trimmed().toUInt(&ok);
            if (ok && page_count > decoder.tiff_page_count && page_count <= std::numeric_limits<uint16_t>::max()) {
                decoder.tiff_page_count = page_count;
            }
        }

        int frame_duration_ms(AVFrame const& frame, AVRational time_base) {
            if (frame.duration <= 0 || time_base.num <= 0 || time_base.den <= 0) {
                return kDefaultFrameDurationMs;
            }
            int64_t const millis = av_rescale_q(frame.duration, time_base, AVRational{1, 1000});
            if (millis <= 0) {
                return kDefaultFrameDurationMs;
            }
            return static_cast<int>(std::clamp<int64_t>(millis,
                                                        kMinimumFrameDurationMs,
                                                        kMaximumFrameDurationMs));
        }

        FfmpegImageDecodeResult append_frame(StreamDecoder& decoder,
                                             AVFrame const& frame,
                                             std::shared_ptr<FfmpegDecodedImage> const& decoded,
                                             uint64_t maximum_frame_pixels,
                                             uint64_t maximum_decoded_bytes) {
            if (frame.width <= 0 || frame.height <= 0) {
                return {{}, QStringLiteral("FFmpeg decoded an invalid image size"), false};
            }
            uint64_t const width = static_cast<uint64_t>(frame.width);
            uint64_t const height = static_cast<uint64_t>(frame.height);
            if (width > maximum_frame_pixels / height) {
                return {{}, QStringLiteral("Image exceeds the 64,000,000-pixel limit"), false};
            }

            QImage image(frame.width, frame.height, QImage::Format_RGBA8888);
            if (image.isNull()) {
                return {{}, QStringLiteral("Unable to allocate the decoded image"), false};
            }
            decoder.scaler.reset(sws_getCachedContext(decoder.scaler.release(),
                                                       frame.width,
                                                       frame.height,
                                                       static_cast<AVPixelFormat>(frame.format),
                                                       frame.width,
                                                       frame.height,
                                                       AV_PIX_FMT_RGBA,
                                                       SWS_BILINEAR,
                                                       nullptr,
                                                       nullptr,
                                                       nullptr));
            if (!decoder.scaler) {
                return {{}, QStringLiteral("FFmpeg could not create an RGBA converter"), false};
            }

            uint8_t* output_data[4] = {image.bits(), nullptr, nullptr, nullptr};
            int output_linesize[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
            int const scaled = sws_scale(decoder.scaler.get(),
                                         frame.data,
                                         frame.linesize,
                                         0,
                                         frame.height,
                                         output_data,
                                         output_linesize);
            if (scaled != frame.height) {
                return {{}, QStringLiteral("FFmpeg could not convert the decoded frame to RGBA"), false};
            }

            uint64_t const frame_bytes = static_cast<uint64_t>(image.sizeInBytes());
            if (frame_bytes > maximum_decoded_bytes
                || decoded->byte_size > maximum_decoded_bytes - frame_bytes) {
                if (decoded->frames.empty()) {
                    return {{}, QStringLiteral("Decoded image exceeds the 256 MiB cache limit"), false};
                }
                decoded->frames.resize(1);
                decoded->byte_size = static_cast<uint64_t>(decoded->frames.front().image.sizeInBytes());
                decoded->animation_truncated = true;
                return {decoded, {}, false};
            }
            decoded->byte_size += frame_bytes;
            decoded->frames.push_back(FfmpegImageFrame{std::move(image), frame_duration_ms(frame, decoder.time_base)});
            return {decoded, {}, false};
        }

        FfmpegImageDecodeResult receive_frames(StreamDecoder& decoder,
                                               AVFrame* frame,
                                               std::shared_ptr<FfmpegDecodedImage> const& decoded,
                                               uint64_t maximum_frame_pixels,
                                               uint64_t maximum_decoded_bytes,
                                               std::atomic<bool> const* cancel_requested) {
            for (;;) {
                if (cancel_requested != nullptr && cancel_requested->load()) {
                    return {{}, {}, true};
                }
                int const receive_result = avcodec_receive_frame(decoder.codec.get(), frame);
                if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) {
                    return {decoded, {}, false};
                }
                if (receive_result < 0) {
                    return {{}, ffmpeg_error(QStringLiteral("FFmpeg image decode failed"), receive_result), false};
                }
                observe_tiff_page_count(decoder, *frame);
                FfmpegImageDecodeResult result = append_frame(
                    decoder, *frame, decoded, maximum_frame_pixels, maximum_decoded_bytes);
                av_frame_unref(frame);
                if (!result.ok() || decoded->animation_truncated) {
                    return result;
                }
            }
        }

        FfmpegImageDecodeResult send_packet_and_receive(
            StreamDecoder& decoder,
            AVPacket const* packet,
            AVFrame* frame,
            std::shared_ptr<FfmpegDecodedImage> const& decoded,
            uint64_t maximum_frame_pixels,
            uint64_t maximum_decoded_bytes,
            std::atomic<bool> const* cancel_requested) {
            for (;;) {
                if (cancel_requested != nullptr && cancel_requested->load()) {
                    return {{}, {}, true};
                }
                int const send_result = avcodec_send_packet(decoder.codec.get(), packet);
                if (send_result == AVERROR(EAGAIN)) {
                    FfmpegImageDecodeResult drained = receive_frames(decoder,
                                                                     frame,
                                                                     decoded,
                                                                     maximum_frame_pixels,
                                                                     maximum_decoded_bytes,
                                                                     cancel_requested);
                    if (!drained.error.isEmpty() || drained.canceled || decoded->animation_truncated) {
                        return drained;
                    }
                    continue;
                }
                if (send_result == AVERROR_EOF && packet == nullptr) {
                    return {decoded, {}, false};
                }
                if (send_result < 0) {
                    return {{},
                            ffmpeg_error(packet == nullptr
                                             ? QStringLiteral("FFmpeg could not flush the image decoder")
                                             : QStringLiteral("FFmpeg rejected an image packet"),
                                         send_result),
                            false};
                }
                return receive_frames(decoder,
                                      frame,
                                      decoded,
                                      maximum_frame_pixels,
                                      maximum_decoded_bytes,
                                      cancel_requested);
            }
        }

    } // namespace

    FfmpegImageDecodeResult FfmpegImageDecoder::decode(
        std::shared_ptr<const std::vector<uint8_t>> const& bytes,
        std::atomic<bool> const* cancel_requested) {
        return decode_with_limits(bytes, kMaximumFramePixels, kMaximumDecodedBytes, cancel_requested);
    }

    FfmpegImageDecodeResult FfmpegImageDecoder::decode_with_limits(
        std::shared_ptr<const std::vector<uint8_t>> const& bytes,
        uint64_t maximum_frame_pixels,
        uint64_t maximum_decoded_bytes,
        std::atomic<bool> const* cancel_requested) {
        if (!bytes || bytes->empty()) {
            return {{}, QStringLiteral("Image data is empty"), false};
        }

        MemoryInput input{bytes, 0, cancel_requested, false};
        auto* io_buffer = static_cast<unsigned char*>(av_malloc(kIoBufferSize));
        if (io_buffer == nullptr) {
            return {{}, QStringLiteral("Unable to allocate the FFmpeg input buffer"), false};
        }
        AvioContextPtr io(avio_alloc_context(
            io_buffer, kIoBufferSize, 0, &input, &read_packet, nullptr, &seek_input));
        if (!io) {
            av_free(io_buffer);
            return {{}, QStringLiteral("Unable to create the FFmpeg memory reader"), false};
        }

        FormatContextPtr format(avformat_alloc_context());
        if (!format) {
            return {{}, QStringLiteral("Unable to allocate the FFmpeg format context"), false};
        }
        format->pb = io.get();
        format->flags |= AVFMT_FLAG_CUSTOM_IO;
        format->opaque = &input;
        format->io_open = &reject_secondary_io;
        // A few nested demuxers allocate their own AVFormatContext instead of
        // using io_open. FFmpeg propagates this whitelist to those contexts,
        // so a deliberately nonexistent protocol closes that second route.
        format->protocol_whitelist = av_strdup("z7memory");
        if (format->protocol_whitelist == nullptr) {
            return {{}, QStringLiteral("Unable to create the FFmpeg I/O policy"), false};
        }

        AVDictionary* raw_options = nullptr;
        av_dict_set(&raw_options, "ignore_loop", "1", 0);
        AVFormatContext* raw_format = format.release();
        int const open_result = avformat_open_input(&raw_format, nullptr, nullptr, &raw_options);
        format.reset(raw_format);
        av_dict_free(&raw_options);
        if (input.secondary_io_attempted) {
            return prohibited_io_result();
        }
        if (open_result < 0) {
            if (open_result == AVERROR_EXIT || (cancel_requested != nullptr && cancel_requested->load())) {
                return {{}, {}, true};
            }
            return {{}, ffmpeg_error(QStringLiteral("FFmpeg could not recognize the image"), open_result), false};
        }
        // Ownership stays with this function because AVFMT_FLAG_CUSTOM_IO is set.
        format->pb = io.get();

        int const stream_info_result = avformat_find_stream_info(format.get(), nullptr);
        if (input.secondary_io_attempted) {
            return prohibited_io_result();
        }
        if (stream_info_result < 0) {
            if (stream_info_result == AVERROR_EXIT || (cancel_requested != nullptr && cancel_requested->load())) {
                return {{}, {}, true};
            }
            return {{}, ffmpeg_error(QStringLiteral("FFmpeg could not inspect the image"), stream_info_result), false};
        }

        std::vector<StreamDecoder> decoders;
        for (unsigned int i = 0; i < format->nb_streams; ++i) {
            AVStream* stream = format->streams[i];
            if (stream == nullptr || stream->codecpar == nullptr
                || stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
                continue;
            }
            if (stream->codecpar->width > 0 && stream->codecpar->height > 0) {
                uint64_t const width = static_cast<uint64_t>(stream->codecpar->width);
                uint64_t const height = static_cast<uint64_t>(stream->codecpar->height);
                if (width > maximum_frame_pixels / height) {
                    return {{}, QStringLiteral("Image exceeds the 64,000,000-pixel limit"), false};
                }
            }
            AVCodec const* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (codec == nullptr) {
                continue;
            }
            CodecContextPtr context(avcodec_alloc_context3(codec));
            if (!context) {
                return {{}, QStringLiteral("Unable to allocate the FFmpeg decoder"), false};
            }
            int result = avcodec_parameters_to_context(context.get(), stream->codecpar);
            if (result < 0) {
                return {{}, ffmpeg_error(QStringLiteral("FFmpeg could not configure the decoder"), result), false};
            }
            context->pkt_timebase = stream->time_base;
            result = avcodec_open2(context.get(), codec, nullptr);
            if (result < 0) {
                continue;
            }
            StreamDecoder decoder;
            decoder.stream_index = i;
            decoder.time_base = stream->time_base;
            decoder.codec = std::move(context);
            decoders.push_back(std::move(decoder));
        }
        if (decoders.empty()) {
            return {{}, QStringLiteral("FFmpeg found no decodable image stream"), false};
        }

        auto decoded = std::make_shared<FfmpegDecodedImage>();
        PacketPtr packet(av_packet_alloc());
        FramePtr frame(av_frame_alloc());
        if (!packet || !frame) {
            return {{}, QStringLiteral("Unable to allocate FFmpeg decode buffers"), false};
        }

        for (;;) {
            if (cancel_requested != nullptr && cancel_requested->load()) {
                return {{}, {}, true};
            }
            int const read_result = av_read_frame(format.get(), packet.get());
            if (input.secondary_io_attempted) {
                return prohibited_io_result();
            }
            if (read_result == AVERROR_EOF) {
                break;
            }
            if (read_result < 0) {
                if (read_result == AVERROR_EXIT || (cancel_requested != nullptr && cancel_requested->load())) {
                    return {{}, {}, true};
                }
                return {{}, ffmpeg_error(QStringLiteral("FFmpeg could not read the image"), read_result), false};
            }

            auto decoder_it = std::find_if(decoders.begin(), decoders.end(), [&](StreamDecoder const& decoder) {
                return decoder.stream_index == static_cast<unsigned int>(packet->stream_index);
            });
            if (decoder_it != decoders.end()) {
                FfmpegImageDecodeResult result = send_packet_and_receive(*decoder_it,
                                                                         packet.get(),
                                                                         frame.get(),
                                                                         decoded,
                                                                         maximum_frame_pixels,
                                                                         maximum_decoded_bytes,
                                                                         cancel_requested);
                if (!result.error.isEmpty() || result.canceled || decoded->animation_truncated) {
                    av_packet_unref(packet.get());
                    return result;
                }
                if (decoder_it->codec->codec_id == AV_CODEC_ID_TIFF && !decoder_it->tiff_pages_expanded
                    && decoder_it->tiff_page_count > 1) {
                    decoder_it->tiff_pages_expanded = true;
                    for (unsigned int page = 2; page <= decoder_it->tiff_page_count; ++page) {
                        avcodec_flush_buffers(decoder_it->codec.get());
                        int const option_result =
                            av_opt_set_int(decoder_it->codec->priv_data, "page", static_cast<int64_t>(page), 0);
                        if (option_result < 0) {
                            av_packet_unref(packet.get());
                            return {{},
                                    ffmpeg_error(QStringLiteral("FFmpeg could not select a TIFF page"), option_result),
                                    false};
                        }
                        result = send_packet_and_receive(*decoder_it,
                                                         packet.get(),
                                                         frame.get(),
                                                         decoded,
                                                         maximum_frame_pixels,
                                                         maximum_decoded_bytes,
                                                         cancel_requested);
                        if (!result.error.isEmpty() || result.canceled || decoded->animation_truncated) {
                            av_packet_unref(packet.get());
                            return result;
                        }
                    }
                }
            }
            av_packet_unref(packet.get());
        }

        for (StreamDecoder& decoder : decoders) {
            FfmpegImageDecodeResult result = send_packet_and_receive(decoder,
                                                                     nullptr,
                                                                     frame.get(),
                                                                     decoded,
                                                                     maximum_frame_pixels,
                                                                     maximum_decoded_bytes,
                                                                     cancel_requested);
            if (!result.error.isEmpty() || result.canceled || decoded->animation_truncated) {
                return result;
            }
        }

        if (decoded->frames.empty()) {
            return {{}, QStringLiteral("FFmpeg decoded no image frames"), false};
        }
        return {std::move(decoded), {}, false};
    }

    bool FfmpegImageDecoder::dimensions_within_limit(int width, int height) {
        if (width <= 0 || height <= 0) {
            return false;
        }
        uint64_t const unsigned_width = static_cast<uint64_t>(width);
        uint64_t const unsigned_height = static_cast<uint64_t>(height);
        return unsigned_width <= kMaximumFramePixels / unsigned_height;
    }

} // namespace z7::ui::filemanager
