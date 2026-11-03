#ifndef __CALENDARSYNC_H__
#define __CALENDARSYNC_H__

#include <PalmOS.h>

__attribute__((section("code3"))) Err CalendarSyncWithPC (void);
void CalendarSyncGetStats (UInt16 *received, UInt16 *applied, UInt16 *skipped,
		UInt16 *stage, UInt16 *eventIndex, UInt16 *lineLength);
__attribute__((section("code4"))) void CalendarSyncRecordModified (UInt32 palmID);

#endif
