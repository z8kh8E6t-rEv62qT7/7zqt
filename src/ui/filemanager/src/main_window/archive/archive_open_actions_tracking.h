// src/ui/filemanager/src/main_window/archive/archive_open_actions_tracking.h
// Role: Platform-specific helpers for launching external Open outside targets.

#pragma once

#include "main_window/deps.h"
#include "main_window/internal.h"

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace z7::ui::filemanager::archive_open_tracking {

#ifdef Q_OS_WIN
bool open_path_externally_with_process_handle(const QString& path,
                                              HANDLE* out_process);
#endif

}  // namespace z7::ui::filemanager::archive_open_tracking
