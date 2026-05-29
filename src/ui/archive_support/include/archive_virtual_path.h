#pragma once

#include <QString>

namespace z7::ui::archive_support {

// Normalizes a virtual path inside an archive:
//   - converts native separators to '/'
//   - trims whitespace, leading/trailing slashes, and collapses consecutive
//     slashes
// Returns an empty string when the input collapses to the archive root.
QString normalize_virtual_dir(const QString& value);

// Joins two normalized virtual paths with '/'. Either side may be empty; the
// result is the non-empty side, or the two joined by a single '/'.
QString join_virtual_path(const QString& base, const QString& child);

// Builds the user-visible display path for a virtual directory inside an
// archive, combining the native archive display source and the virtual dir.
QString virtual_display_path(const QString& display_source,
                             const QString& virtual_dir);

}  // namespace z7::ui::archive_support
