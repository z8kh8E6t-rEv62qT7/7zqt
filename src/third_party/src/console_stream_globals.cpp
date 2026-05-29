// src/third_party/src/console_stream_globals.cpp
// Role: Runtime console stream globals for original 7-Zip console bridge.

#include "Common/StdOutStream.h"

CStdOutStream* g_StdStream = &g_StdOut;
CStdOutStream* g_ErrStream = &g_StdErr;
