// src/ui/filemanager/src/main_window/drag_drop/drop_effect_feedback.h
// Role: Apply drag/drop performed-effect feedback compatible with Windows OLE semantics.

#pragma once

#include <QByteArray>
#include <Qt>

class QDropEvent;

namespace z7::ui::filemanager {

QByteArray encode_windows_drop_effect_dword(quint32 value);
void apply_windows_drop_effect_feedback(const QDropEvent* event,
                                        bool operation_succeeded,
                                        Qt::DropAction reported_action,
                                        bool trusted_internal_source);

}  // namespace z7::ui::filemanager
