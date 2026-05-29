// src/archive_application/src/native_7z/third_party_adapter/third_party_adapter.h
// Role: Single include boundary for upstream 7-Zip headers used by backend.

#pragma once

#include "Common/MyCom.h"
#include "Common/IntToString.h"
#include "Common/StringConvert.h"
#include "Common/UTFConvert.h"
#include "7zip/UI/Common/HashCalc.h"
#include "7zip/UI/Common/OpenArchive.h"
#include "7zip/UI/Common/Bench.h"
#include "7zip/UI/Common/PropIDUtils.h"
#include "7zip/UI/Common/Update.h"
#include "7zip/UI/Common/UpdateCallback.h"
#include "7zip/UI/Common/UpdateProduce.h"
#include "Windows/PropVariantConv.h"
#include "7zip/UI/Agent/AgentProxy.h"
#include "7zip/Common/FileStreams.h"
#include "7zip/Common/LimitedStreams.h"
#include "7zip/Common/StreamObjects.h"
#include "7zip/Compress/CopyCoder.h"
#include "7zip/MyVersion.h"
#include "Windows/SystemInfo.h"
#include "Windows/TimeUtils.h"
