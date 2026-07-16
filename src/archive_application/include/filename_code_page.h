// Role: Shared archive filename-code-page validation and scoped conversion.

#pragma once

#include <cstdint>
#include <optional>

namespace z7::app {

    using FilenameCodePage = std::optional<uint32_t>;

    bool is_filename_code_page_supported(uint32_t code_page);
    void initialize_archive_filename_locale();

    class ScopedFilenameCodePage final {
    public:
        explicit ScopedFilenameCodePage(FilenameCodePage code_page);
        ~ScopedFilenameCodePage();

        ScopedFilenameCodePage(ScopedFilenameCodePage const&) = delete;
        ScopedFilenameCodePage& operator=(ScopedFilenameCodePage const&) = delete;

    private:
        uint32_t previous_ = 0;
    };

} // namespace z7::app
