#include "internal.h"

namespace z7::macos_integration::capi_internal
{

    QString fallback_archive_error_summary(z7::app::OperationOutcome const& outcome)
    {
        return fallback_archive_error_summary(
            outcome.error,
            z7::ui::archive_support::from_utf8_string(outcome.summary));
    }

    QString fallback_archive_error_summary(z7::app::ArchiveError const& error, QString const& summary)
    {
        QString const trimmed_summary = summary.trimmed();
        if (!trimmed_summary.isEmpty())
        {
            return trimmed_summary;
        }
        return z7::ui::archive_support::from_utf8_string(z7::app::describe_archive_error(error)).trimmed();
    }

} // namespace z7::macos_integration::capi_internal
