/******************************************************************************
 *
 * Copyright (c) 1995-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: DateAlarm.h
 *
 * Description:
 *	  This file defines the alarm functions.
 *
 * History:
 *		August 29, 1995	Created by Art Lamb
 *			Name		Date		Description
 *			----		----		-----------
 *			frigino	9/9/97	Added extern ref to PlayAlarmSound
 *
 *****************************************************************************/

extern void AlarmInit ();
extern void DisplayAlarm ();
extern void AlarmTriggered (SysAlarmTriggeredParamType * cmdPBP);
extern void RescheduleAlarms (DmOpenRef dbP);
extern void AlarmReset (Boolean newerOnly);
extern void PlayAlarmSound(UInt32 uniqueRecID);
extern UInt32 AlarmGetTrigger (UInt32* refP);
extern void AlarmSetTrigger (UInt32 alarmTime, UInt32 ref);

