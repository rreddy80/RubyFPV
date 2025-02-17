#pragma once
#include "../base/base.h"

// Globals

extern u32 g_TimeNow;
extern u32 g_TimeStart;
extern u32 g_TimeNowMicros;

extern u32 g_TimeLastVideoCameraChangeCommand;
extern u32 g_TimeStartPendingRadioFlagsChange;

// Central

extern u32 g_RouterIsReadyTimestamp;
extern u32 g_TimeLastVideoDataOverloadAlarm;
extern u32 g_TimeLastVideoTxOverloadAlarm;
extern u32 g_TimeLastSentCurrentActiveOSDLayout;

extern u32 g_uTimeLastRadioLinkOverloadAlarm;
