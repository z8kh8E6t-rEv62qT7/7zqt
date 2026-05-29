#include <QByteArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "internal.h"

namespace z7::macos_integration::capi_internal
{
    namespace
    {

        constexpr char kSyntheticQtArgv0[] = "z7_macos_integration_capi";

        struct QtCoreAppHolder
        {
            int argc = 1;
            char* argv[2] = {const_cast<char*>(kSyntheticQtArgv0), nullptr};
            std::unique_ptr<QCoreApplication> app;
        };

    } // namespace

    void ensure_qt_core_app()
    {
        if (QCoreApplication::instance() != nullptr)
        {
            return;
        }
        static QtCoreAppHolder holder;
        if (!holder.app)
        {
            holder.app = std::make_unique<QCoreApplication>(holder.argc, holder.argv);
        }
    }

    QString to_qstring(char const* value)
    {
        if (value == nullptr)
        {
            return {};
        }
        return QString::fromUtf8(value).trimmed();
    }

    char* duplicate_c_string(QString const& value)
    {
        QByteArray const utf8 = value.toUtf8();
        size_t const bytes = static_cast<size_t>(utf8.size());
        char* out = static_cast<char*>(std::malloc(bytes + 1));
        if (out == nullptr)
        {
            return nullptr;
        }
        if (bytes > 0)
        {
            std::memcpy(out, utf8.constData(), bytes);
        }
        out[bytes] = '\0';
        return out;
    }

    void free_c_string(char const*& value)
    {
        if (value != nullptr)
        {
            std::free(const_cast<char*>(value));
            value = nullptr;
        }
    }

    QStringList string_list_from_utf8(char const* const* values, size_t count)
    {
        QStringList out;
        out.reserve(static_cast<int>(count));
        if (values == nullptr)
        {
            return out;
        }
        for (size_t i = 0; i < count; ++i)
        {
            char const* value = values[i];
            if (value == nullptr)
            {
                continue;
            }
            QString const text = QString::fromUtf8(value).trimmed();
            if (!text.isEmpty())
            {
                out.push_back(QFileInfo(text).absoluteFilePath());
            }
        }
        return out;
    }

    void init_menu_plan_error(z7_mi_menu_plan_t* out_plan, z7_mi_status_t status, QString const& error)
    {
        if (out_plan == nullptr)
        {
            return;
        }
        std::memset(out_plan, 0, sizeof(*out_plan));
        out_plan->ok = false;
        out_plan->status = status;
        out_plan->error_message = duplicate_c_string(error);
    }

    void init_action_result_error(z7_mi_action_result_t* out_result,
                                  z7_mi_status_t status,
                                  QString const& error,
                                  QString const& action_id)
    {
        if (out_result == nullptr)
        {
            return;
        }
        std::memset(out_result, 0, sizeof(*out_result));
        out_result->ok = false;
        out_result->status = status;
        out_result->error_message = duplicate_c_string(error);
        out_result->action_id = duplicate_c_string(action_id);
    }

    void
    init_quicklook_list_error(z7_mi_quicklook_list_result_t* out_result, z7_mi_status_t status, QString const& error)
    {
        if (out_result == nullptr)
        {
            return;
        }
        std::memset(out_result, 0, sizeof(*out_result));
        out_result->ok = false;
        out_result->status = status;
        out_result->error_message = duplicate_c_string(error);
    }

    void init_quicklook_batch_export_error(
        z7_mi_quicklook_batch_export_result_t* out_result,
        z7_mi_status_t status,
        QString const& error,
        size_t completed_item_count,
        size_t total_item_count,
        int64_t failed_item_index,
        QString const& failed_entry_path,
        QString const& failed_destination_path)
    {
        if (out_result == nullptr)
        {
            return;
        }
        std::memset(out_result, 0, sizeof(*out_result));
        out_result->ok = false;
        out_result->status = status;
        out_result->error_message = duplicate_c_string(error);
        out_result->completed_item_count = completed_item_count;
        out_result->total_item_count = total_item_count;
        out_result->failed_item_index = failed_item_index;
        out_result->failed_entry_path = duplicate_c_string(failed_entry_path);
        out_result->failed_destination_path = duplicate_c_string(failed_destination_path);
    }

} // namespace z7::macos_integration::capi_internal
