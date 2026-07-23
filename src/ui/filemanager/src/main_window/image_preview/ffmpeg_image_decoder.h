#pragma once

#include <QImage>
#include <QString>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace z7::ui::filemanager {

    struct FfmpegImageFrame {
        QImage image;
        int duration_ms = 100;
    };

    struct FfmpegDecodedImage {
        std::vector<FfmpegImageFrame> frames;
        uint64_t byte_size = 0;
        bool animation_truncated = false;
    };

    struct FfmpegImageDecodeResult {
        std::shared_ptr<const FfmpegDecodedImage> image;
        QString error;
        bool canceled = false;

        bool ok() const { return image != nullptr && !image->frames.empty(); }
    };

    class FfmpegImageDecoder final {
    public:
        static constexpr uint64_t kMaximumFramePixels = 64'000'000;
        static constexpr uint64_t kMaximumDecodedBytes = 256ull * 1024ull * 1024ull;

        static bool dimensions_within_limit(int width, int height);

        static FfmpegImageDecodeResult decode(std::shared_ptr<const std::vector<uint8_t>> const& bytes,
                                              std::atomic<bool> const* cancel_requested = nullptr);

    private:
        static FfmpegImageDecodeResult decode_with_limits(
            std::shared_ptr<const std::vector<uint8_t>> const& bytes,
            uint64_t maximum_frame_pixels,
            uint64_t maximum_decoded_bytes,
            std::atomic<bool> const* cancel_requested = nullptr);
    };

} // namespace z7::ui::filemanager
