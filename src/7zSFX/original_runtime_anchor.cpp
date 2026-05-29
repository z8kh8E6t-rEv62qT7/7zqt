// Defines GUID and console stream globals required by the statically linked
// original 7-Zip objects. The SFX app does not use the console bridge, but some
// upstream utility objects still reference these globals when linked whole.

#include "Common/MyInitGuid.h"
#include "Common/StdOutStream.h"
#include "7zip/IProgress.h"
#include "7zip/IStream.h"
#include "7zip/ICoder.h"
#include "7zip/IPassword.h"
#include "7zip/Archive/IArchive.h"
#include "7zip/UI/Agent/Agent.h"
#include "7zip/UI/Agent/IFolderArchive.h"
#include "7zip/UI/Common/IFileExtractCallback.h"

CStdOutStream* g_StdStream = &g_StdOut;
CStdOutStream* g_ErrStream = &g_StdErr;
