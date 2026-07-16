// Role: Shared archive filename-code-page validation and conversion scope.

#include "filename_code_page.h"

#include <mutex>

#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {
    namespace {

        std::once_flag locale_once;

    } // namespace

    void initialize_archive_filename_locale() {
        std::call_once(locale_once, []() {
#ifdef ENV_HAVE_LOCALE
            MY_SetLocale();
#endif
        });
    }

    bool is_filename_code_page_supported(uint32_t code_page) {
        if (code_page < 2 || code_page > 65535) {
            return false;
        }
        initialize_archive_filename_locale();
        return IsArchiveFilenameCodePageSupported(static_cast<UINT>(code_page));
    }

    ScopedFilenameCodePage::ScopedFilenameCodePage(FilenameCodePage code_page) {
        initialize_archive_filename_locale();
        previous_ = SetThreadArchiveFilenameCodePage(code_page.value_or(0));
    }

    ScopedFilenameCodePage::~ScopedFilenameCodePage() {
        SetThreadArchiveFilenameCodePage(previous_);
    }

} // namespace z7::app
