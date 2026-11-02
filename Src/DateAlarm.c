/******************************************************************************
 *
 * Copyright (c) 1995-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: DateAlarm.c
 *
 * Description:
 *	  This module contains the routines that handle alarms.
 *
 * History:
 *		August 29, 1995	Created by Art Lamb
 *			Name		Date			Description
 *			----		----			-----------
 *			rbb		4/20/99		Added snooze feature
 *
 *****************************************************************************/

#include <PalmOS.h>
#include <AlarmMgr.h>
#include <FeatureMgr.h>

#include "Datebook.h"


/***********************************************************************
 *
 *	Global variables
 *
 ***********************************************************************/
extern UInt16 alarmSoundRepeatCount;
extern UInt16 alarmSoundRepeatInterval;
extern UInt16 alarmSound;


/***********************************************************************
 *
 *	Internal Constants
 *
 ***********************************************************************/
#define alarmDescDrawX						45
#define alarmDescDrawY						18
#define alarmDescSeparatorY				9		// whitespace between title and description
#define alarmDescRightMargin				10
#define alarmDescMaxLine					4
#define alarmDescMaxWidth					100

// The title settings were gleaned from Form.c
#define titleFont								boldFont
#define titleHeight 							15
#define titleMarginX 						3	// Don't forget the blank column after the last char
#define titleMarginY 						2


/***********************************************************************
 *
 *	Internal Structures
 *
 ***********************************************************************/

typedef struct {
	ApptDBRecordPtr	appointmentP;
	UInt32					alarmTime;
} ApptInstance;


/***********************************************************************
 *
 *	Internal Storage
 *
 ***********************************************************************/

static PendingAlarmQueueType	PendingAlarmQueue;


/***********************************************************************
 *
 *	Local prototypes
 *
 ***********************************************************************/

void DBDisplayAlarm(void);
static Err	AddPendingAlarm (
	PendingAlarmQueueType*	inAlarmInternalsP,
	UInt16						recordNum,
	UInt32						alarmTime,
	UInt16						inBaseIndex );

static UInt16 DoAlarmDialog (
	PendingAlarmType *		inPendingInfoP );

void ReserveAlarmInternals (
	PendingAlarmQueueType *	outAlarmInternalsP );

void ReleaseAlarmInternals (
	PendingAlarmQueueType *	inAlarmInternalsP,
	Boolean						inModified );

static UInt16 GetPendingAlarmCount (
	PendingAlarmQueueType*	inAlarmInternalsP );

static void SetPendingAlarmCount (
	PendingAlarmQueueType*	inAlarmInternalsP, 
	UInt16						inCount );

static void GetPendingAlarm (
	PendingAlarmQueueType*	inAlarmInternalsP, 
	UInt16						inAlarmIndex,
	PendingAlarmType *		outPendingInfo );

static void SetPendingAlarm (
	PendingAlarmQueueType*	inAlarmInternalsP, 
	UInt16						inAlarmIndex,
	PendingAlarmType *		inPendingInfo );

static void InsertPendingAlarm (
	PendingAlarmQueueType*	inAlarmInternalsP, 
	UInt16						inAlarmIndex,
	PendingAlarmType *		inPendingInfo );

static void RemovePendingAlarm (
	PendingAlarmQueueType*	inAlarmInternalsP, 
	UInt16						inAlarmIndex );

static void ClearPendingAlarms (
	PendingAlarmQueueType*	inAlarmInternalsP );

UInt16
GetDismissedAlarmCount (
	PendingAlarmQueueType*	inAlarmInternalsP );

UInt32 *
GetDismissedAlarmList (
	PendingAlarmQueueType*	inAlarmInternalsP );

static void
AppendDismissedAlarm (
	PendingAlarmQueueType*	inAlarmInternalsP, 
	UInt32						inUniqueID );

static void
ClearDismissedAlarms (
	PendingAlarmQueueType*	inAlarmInternalsP );


/***********************************************************************
 *
 * FUNCTION:    AlarmGetTrigger
 *
 * DESCRIPTION: This routine gets the time of the next scheduled
 *              alarm from the Alarm Manager.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    the time of the next scheduled alarm, or zero if 
 *              no alarm is scheduled.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	9/1/95	Initial Revision
 *			rbb	4/21/99	Was GetTimeOfNextAlarm. Renamed to avoid
 *								confusion with ApptGetTimeOfNextAlarm.
 *
 ***********************************************************************/
UInt32 AlarmGetTrigger (UInt32* refP)
{
	UInt16 		cardNo;
	LocalID 		dbID;
	UInt32		alarmTime = 0;	
	DmSearchStateType searchInfo;

#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	char							debugStr[80];
	char							numStr[16];
#endif

	DmGetNextDatabaseByTypeCreator (true, &searchInfo, 
		sysFileTApplication, sysFileCDatebook, true, &cardNo, &dbID);

	alarmTime = AlmGetAlarm (cardNo, dbID, refP);

#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	StrCopy (debugStr, "    AlarmGetTrigger (");
	StrIToA (numStr, *refP);
	StrCat (debugStr, numStr);
	StrCat (debugStr, ") returns ");
	StrIToH (numStr, alarmTime);
	StrCat (debugStr, numStr);
	StrCat (debugStr, "\n");
	DbgSrcMessage (debugStr);
#endif

	return (alarmTime);
}


/***********************************************************************
 *
 * FUNCTION:    AlarmSetTrigger
 *
 * DESCRIPTION: This routine set the time of the next scheduled
 *              alarm. 
 *
 * PARAMETERS:  the time of the next scheduled alarm, or zero if 
 *              no alarm is scheduled.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	9/1/95	Initial Revision
 *			rbb	4/21/99	Renamed from SetTimeOfNextAlarm, in parallel
 *								with rename of GetTimeOfNextAlarm
 *
 ***********************************************************************/
void AlarmSetTrigger (UInt32 alarmTime, UInt32 ref)
{
	UInt16 cardNo;
	LocalID dbID;
	DmSearchStateType searchInfo;

#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	char							debugStr[80];
	char							numStr[16];

	StrCopy (debugStr, "    AlarmSetTrigger (");
	StrIToH (numStr, alarmTime);
	StrCat (debugStr, numStr);
	StrCat (debugStr, ", ");
	StrIToA (numStr, ref);
	StrCat (debugStr, numStr);
	StrCat (debugStr, ")\n");
	DbgSrcMessage (debugStr);
#endif

	DmGetNextDatabaseByTypeCreator (true, &searchInfo, 
		sysFileTApplication, sysFileCDatebook, true, &cardNo, &dbID);

	AlmSetAlarm (cardNo, dbID, ref, alarmTime, true);
}


/***********************************************************************
 *
 * FUNCTION:    GetSnoozeAnchorTime
 *
 * DESCRIPTION: Returns the time that the snooze bar was pressed. Used
 *					 when resuming from snooze to determine when alarm processing
 *					 was suspended.
 *
 * PARAMETERS:  none
 *
 * RETURNED:    time that snooze bar was pressed
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/21/99	Initial Revision
 *
 ***********************************************************************/
static UInt32
GetSnoozeAnchorTime (
	PendingAlarmQueueType*	inAlarmInternalsP )
{
	return inAlarmInternalsP->snoozeAnchorTime;
}


/***********************************************************************
 *
 * FUNCTION:    GetSnoozeDuration
 *
 * DESCRIPTION: Returns the duration to be applied when the snooze bar is pressed.
 *
 * PARAMETERS:  none
 *
 * RETURNED:    snooze duration
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/21/99	Initial Revision
 *
 ***********************************************************************/
static UInt32
GetSnoozeDuration (
	PendingAlarmQueueType*	inAlarmInternalsP )
{
	return inAlarmInternalsP->snoozeDuration;
}


/***********************************************************************
 *
 * FUNCTION:    SetSnoozeDuration
 *
 * DESCRIPTION: Sets the duration to be applied when the snooze bar is pressed.
 *
 * PARAMETERS:  none
 *
 * RETURNED:    snooze duration
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/21/99	Initial Revision
 *
 ***********************************************************************/
static void
SetSnoozeDuration (
	PendingAlarmQueueType*	inAlarmInternalsP,
	UInt32						inDuration )
{
	inAlarmInternalsP->snoozeDuration = inDuration;
}


/***********************************************************************
 *
 * FUNCTION:    GetSnoozeAnchorUniqueID
 *
 * DESCRIPTION: Returns the uniqueID of the record shown when the
 *					 snooze bar was pressed.
 *
 * PARAMETERS:  none
 *
 * RETURNED:    uniqueID
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/4/99	Initial Revision
 *
 ***********************************************************************/
static UInt32
GetSnoozeAnchorUniqueID (
	PendingAlarmQueueType*	inAlarmInternalsP )
{
	return inAlarmInternalsP->snoozeAnchorUniqueID;
}


/***********************************************************************
 *
 * FUNCTION:    SetSnoozeAnchor
 *
 * DESCRIPTION: Stores the time that the snooze bar was pressed. Used
 *					 when resuming from snooze to determine when alarm processing
 *					 was suspended.
 *
 * PARAMETERS:  time that snooze bar was pressed
 *
 * RETURNED:    none
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/21/99	Initial Revision
 *
 ***********************************************************************/
static void
SetSnoozeAnchor (
	PendingAlarmQueueType*	inAlarmInternalsP,
	UInt32						inUniqueID,
	UInt32						inTimeInSeconds )
{
	inAlarmInternalsP->snoozeAnchorUniqueID = inUniqueID;
	inAlarmInternalsP->snoozeAnchorTime = inTimeInSeconds;
}


/***********************************************************************
 *
 * FUNCTION:    AlarmInit
 *
 * DESCRIPTION: Initialize DateAlarm internals for use at interrupt time.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	8/17/99	Initial Revision
 *
 ***********************************************************************/
void AlarmInit ()
{
	DatebookPreferenceType		prefs;
	UInt16							prefsSize;
	Int16								prefsVersion = noPreferenceFound;
	UInt16							snooze;
	PendingAlarmQueueType		alarmInternals;
	
	// Read the preferences / saved-state information.  Fix-up if no prefs or older/newer version
	prefsSize = sizeof (DatebookPreferenceType);
	prefsVersion = PrefGetAppPreferences (sysFileCDatebook, datebookPrefID, &prefs, &prefsSize, true);
	
	snooze = (prefsVersion == datebookPrefsVersionNum) ? prefs.alarmSnooze : defaultAlarmSnooze;
	
	ReserveAlarmInternals (&alarmInternals);
	
	SetSnoozeDuration (&alarmInternals, snooze);
	
	ReleaseAlarmInternals (&alarmInternals, true);
}


/***********************************************************************
 *
 * FUNCTION:    DrawAlarm
 *
 * DESCRIPTION: This routine draws the description, time, duration, and 
 *              date of an event.
 *
 * PARAMETERS:  pendingP - record index and alarm time of the event
 *
 * RETURNED:    nothing
 *
 *	HISTORY:
 *		10/14/95	art	Created by Art Lamb
 *		08/04/99	kwk	Use templates & DateTemplateToAscii to make it localizable.
 *		10/12/99	gap	Need to use min return value of FntCharsInWidth and 
 *							FldWordWrap in order to display the alarm text properly 
 *							with respect to word boundaries as well as linefeeds, etc.
 *
 ***********************************************************************/
static void DrawAlarm (PendingAlarmType * pendingP)
 {
 	Char							chr;
	Char							timeStr [timeStringLength];
	Char 							dateStr[longDateStrLength];
	Char							dowNameStr[dowDateStringLength];
 	UInt16						lineCount = 0;
	UInt16 						duration;
 	UInt16						length;
 	Int16							descLen;
	Int16							descFitWith;
	Int16							descFitLen;
	UInt16						maxWidth;
	Int32 						adjust;
	UInt32 						eventTime;
 	Int16							x, y;
 	Int16							extentX, extentY;
	FontID						curFont;
	MemHandle					resH;
	MemHandle					recordH;
	Char*							ptr;
	Char*							tempStr;
	DmOpenRef 					dbP;
	DateTimeType 				dateTime;
	ApptDBRecordType 			apptRec;
	SystemPreferencesType 	sysPrefs;	
	Boolean						fit;

	// Open the appointment database.
	dbP = DmOpenDatabaseByTypeCreator (datebookDBType, sysFileCDatebook, 
		dmModeReadOnly);
	if (!dbP) return;

	ApptGetRecord (dbP, pendingP->recordNum, &apptRec, &recordH);

	curFont = FntSetFont (largeBoldFont);

	// Get the date and time formats.
	PrefGetPreferences (&sysPrefs);


	// Compute the maximum width of a line of the description.
	WinGetWindowExtent (&extentX, &extentY);
	maxWidth = extentX - alarmDescDrawX - alarmDescRightMargin;  
	
	// Calculate the event's date and time from the alarm time and the alarm
	// advance.  The date and time stored in the record will not be
	// the correct values to display, if the event is repeating.
	adjust = apptRec.alarm->advance;
	switch (apptRec.alarm->advanceUnit)
		{
		case aauMinutes:
			adjust *= minutesInSeconds;
			break;
		case aauHours:
			adjust *= hoursInSeconds;
			break;
		case aauDays:
			adjust *= daysInSeconds;
			break;
		}	
	eventTime = pendingP->alarmTime + adjust;
	
	
	y = alarmDescDrawY;
	// Draw the date - for English this will be "Monday, <date>".
	TimSecondsToDateTime (eventTime, &dateTime);

	// Get the day-of-week name and the system formatted date
	DateTemplateToAscii("^1l", dateTime.month, dateTime.day, dateTime.year, dowNameStr, sizeof(dowNameStr));
	DateToAscii(dateTime.month, dateTime.day, dateTime.year, sysPrefs.dateFormat, dateStr);
	
	resH = DmGetResource (strRsc, drawAlarmDateTemplateStrID);
	ptr = MemHandleLock (resH);
	tempStr = DateParamString(ptr, dowNameStr, dateStr, NULL, NULL);
	MemPtrUnlock(ptr);
	
	// DOLATER kwk - what should happen if the string needs to be truncated? Go
	// onto a following line (wrap it)?
	WinDrawChars(tempStr, StrLen(tempStr), alarmDescDrawX, y);
	MemPtrFree((MemPtr)tempStr);


	// Display the time of the event if the event has a time.
	if (TimeToInt (apptRec.when->startTime) != apptNoTime)
		{
		// Calculate the duration of the event.
		duration = (apptRec.when->endTime.hours * hoursInMinutes + 
						apptRec.when->endTime.minutes) -
					  (apptRec.when->startTime.hours * hoursInMinutes + 
						apptRec.when->startTime.minutes);


		// Draw the event's time and duration.
		TimSecondsToDateTime (eventTime, &dateTime);
		TimeToAscii (dateTime.hour, dateTime.minute, sysPrefs.timeFormat, timeStr);
	
		x = alarmDescDrawX;
		y += FntLineHeight();
		WinDrawChars (timeStr, StrLen (timeStr), x, y);
		
		x += FntCharsWidth (timeStr, StrLen (timeStr)) + FntCharWidth (spaceChr);
		chr = '-';
		WinDrawChars (&chr, 1, x, y);
		
		TimSecondsToDateTime (eventTime + (duration * minutesInSeconds), &dateTime);
		TimeToAscii (dateTime.hour, dateTime.minute, sysPrefs.timeFormat, timeStr);
		x += FntCharWidth (chr) + FntCharWidth (spaceChr);
		WinDrawChars (timeStr, StrLen (timeStr), x, y);
		}
	
	
	// Draw the event's description.
	y += alarmDescSeparatorY;
	ptr = apptRec.description;
	descLen = StrLen(ptr);

	while(descLen)
	{
		descFitWith = maxWidth;
		descFitLen = descLen;
		
		// Calculate how many characters will fit in the window bounds
		FntCharsInWidth (ptr, &descFitWith, &descFitLen, &fit);
		if (!descFitLen)
			break;
			
		// Calculate the number of characters in full words that will fit in the bounds
		length = FldWordWrap  (ptr, maxWidth);
		
		// Need to display the minimum of the two as FldWordWrap includes carriage returns, tabs, etc.
		descFitLen = min(descFitLen, length);

		y += FntLineHeight();


		if (++lineCount >= alarmDescMaxLine) {
			if (descLen != descFitLen)
				descFitLen = descLen;
				
			// DOLATER еее - make sure chrEllipsis is international OK
			if (descFitWith < maxWidth)
				descFitWith += FntCharWidth(chrEllipsis);
			
			descFitWith = min(descFitWith, maxWidth);
			
			WinDrawTruncChars(ptr, descFitLen, alarmDescDrawX, y, descFitWith);
			break;
			}
		else 
			WinDrawTruncChars(ptr, descFitLen, alarmDescDrawX, y, maxWidth);
			
		descLen -= length;
		ptr += length;
	}

	FntSetFont (curFont);
	
	MemHandleUnlock (recordH);
	
	DmCloseDatabase (dbP);
 }


/***********************************************************************
 *
 * FUNCTION:    DrawAlarmTitle
 *
 * DESCRIPTION: Show the current time along with the title, "Reminder"
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	8/19/99	Initial Revision
 *			jmp	10/6/99	Replace WinInvertChars() with WinDrawInvertedChars(), and set
 *								set the fore/back colors to be those of a dialog so that the
 *								title colors are drawn correctly.
 *
 ***********************************************************************/
static void DrawAlarmTitle ()
{
	DateTimeType 				dateTime;
	Char							timeStr [timeStringLength];
	MemHandle					titleH;
	Char*							titleP;
	UInt16						titleLen;
	Int16							windowWidth;
	Int16							windowHeight;
	RectangleType				r;
	Coord							x;
	FontID						currFont;
	IndexedColorType 			currForeColor;
	IndexedColorType 			currBackColor;
	IndexedColorType			currTextColor;

	TimSecondsToDateTime (TimGetSeconds(), &dateTime);
	TimeToAscii (dateTime.hour, dateTime.minute, (TimeFormatType) PrefGetPreference (prefTimeFormat),
					timeStr);
	
	titleH = DmGetResource (strRsc, alarmTitleStrID);
	titleP = MemHandleLock (titleH);
	titleLen = StrLen (titleP);

	WinGetWindowExtent (&windowWidth, &windowHeight);
	
	currFont = FntSetFont (titleFont);
 	currForeColor = WinSetForeColor (UIColorGetTableEntryIndex(UIDialogFrame));
 	currBackColor = WinSetBackColor (UIColorGetTableEntryIndex(UIDialogFill));
 	currTextColor = WinSetTextColor (UIColorGetTableEntryIndex(UIDialogFrame));
	
	r = (RectangleType) { {0, 1}, {windowWidth, FntLineHeight ()} };
	x  = (windowWidth - FntCharsWidth (titleP, titleLen) + 1) >> 1;

	WinDrawLine (1, 0, r.extent.x-2, 0);
	WinDrawRectangle (&r, 0);
	WinDrawInvertedChars (timeStr, StrLen(timeStr), titleMarginX, 0);
	WinDrawInvertedChars (titleP, titleLen, x, 0);
	
   WinSetForeColor (currForeColor);
   WinSetBackColor (currBackColor);
   WinSetTextColor (currTextColor);
	FntSetFont (currFont);
	MemPtrUnlock(titleP);
}


/***********************************************************************
 *
 * FUNCTION:    ShowAlarmHandleEvent
 *
 * DESCRIPTION: This routine is the event handler for the alarm dialog
 *              of the Datebook application. 
 *
 * PARAMETERS:  event  - a pointer to an EventType structure
 *
 * RETURNED:    true if the event has handle and should not be passed
 *              to a higher level handler.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	10/10/95	Initial Revision
 *			CSS	06/22/99	Standardized keyDownEvent handling
 *								(TxtCharIsHardKey, commandKeyMask, etc.)
 *			rbb	11/4/99	Clock display is now updated on the minute
 *								Allow simulator to quit while alarm is still shown
 *
 ***********************************************************************/
static Boolean ShowAlarmHandleEvent (EventType * event)
{
	UInt16 index;
	FormPtr frm;
	PendingAlarmType * pendingP;

	// We don't want t allow an other application to be launched 
	// while an alarm is displayed, so we intercept appStop events
	// (except when running under simulation). All other events are
	// handled by FrmDoDialog.
	if (event->eType == appStopEvent)
		{
#if	EMULATION_LEVEL == EMULATION_NONE
		return true;
#endif
		}
	else if (event->eType == keyDownEvent)
		{
		if (	!TxtCharIsHardKey(event->data.keyDown.modifiers,
										event->data.keyDown.chr)
			&&	EvtKeydownIsVirtual(event))
			{
			if (event->data.keyDown.chr == vchrLateWakeup)
				{
				// refresh time on power up
				DrawAlarmTitle ();
				} 
			else if (event->data.keyDown.chr == vchrPageUp)
				{
				// disable 'up' scroll arrow from ticking while our alarm dialog is up.
				return true;
				}
			}
		}
	else if (event->eType == frmUpdateEvent)
		{
		frm = FrmGetActiveForm ();
		FrmDrawForm (frm);
		DrawAlarmTitle ();
		
		index = FrmGetObjectIndex (frm, AlarmDescGadget);
		pendingP = FrmGetGadgetData (frm, index);
		DrawAlarm (pendingP);
		}
	
	else if (event->eType == nilEvent)
		{
		// refresh time
		DrawAlarmTitle ();
		}
	
	{
	UInt32 ticks = TimGetTicks ();
	UInt32 seconds = TimGetSeconds ();
	UInt32 minutes = seconds / 60;
	UInt32 targetSeconds = (minutes + 1) * 60;
	UInt32 delayInSeconds = targetSeconds - seconds;
	UInt32 targetTicks = ticks + delayInSeconds * sysTicksPerSecond;
	
	EvtSetNullEventTick (targetTicks);
	}

	return false;
}


/***********************************************************************
 *
 * FUNCTION:    PlayAlarmSound
 *
 * DESCRIPTION:	Play a MIDI sound given a unique record ID of the MIDI record in the System
 *						MIDI database.  If the sound is not found, then the default alarm sound will
 *						be played.
 *
 * PARAMETERS:  uniqueRecID	-- unique record ID of the MIDI record in the System
 *										   MIDI database.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			vmk	8/10/97	Initial version
 *			trev	08/14/97	Ported to dateBook App
 *			vmk	12/11/97	Prevent call to DmFindRecordByID if DmOpenDatabase failed
 *
 ***********************************************************************/
void PlayAlarmSound(UInt32 uniqueRecID)
{
	Err			err;
	MemHandle	midiH;							// handle of MIDI record
	SndMidiRecHdrType*	midiHdrP;			// pointer to MIDI record header
	UInt8*		midiStreamP;					// pointer to MIDI stream beginning with the 'MThd'
														// SMF header chunk
	UInt16		cardNo;							// card number of System MIDI database
	LocalID		dbID;								// Local ID of System MIDI database
	DmOpenRef	dbP = NULL;						// reference to open database
	UInt16		recIndex;						// record index of the MIDI record to play
	SndSmfOptionsType	smfOpt;					// SMF play options
	DmSearchStateType	searchState;			// search state for finding the System MIDI database
	Boolean		bError = false;				// set to true if we couldn't find the MIDI record
	
		
	// Find the system MIDI database
	err = DmGetNextDatabaseByTypeCreator(true, &searchState,
			 		sysFileTMidi, sysFileCSystem, false, 
			 		&cardNo, &dbID);
	if ( err )
		bError = true;														// DB not found
	
	// Open the MIDI database in read-only mode
	if ( !bError )
		{
		dbP = DmOpenDatabase (cardNo, dbID, dmModeReadOnly);
		if ( !dbP )
			bError = true;													// couldn't open
		}
	
	// Find the MIDI track record
	if ( !bError )
		{
		err = DmFindRecordByID (dbP, uniqueRecID, &recIndex);
		if ( err )
			bError = true;														// record not found
		}
		
	// Lock the record and play the sound
	if ( !bError )
		{
		// Find the record handle and lock the record
		midiH = DmQueryRecord(dbP, recIndex);
		midiHdrP = MemHandleLock(midiH);
		
		// Get a pointer to the SMF stream
		midiStreamP = (UInt8*)midiHdrP + midiHdrP->bDataOffset;
		
		// Play the sound (ignore the error code)
		// The sound can be interrupted by a key/digitizer event
		smfOpt.dwStartMilliSec = 0;
		smfOpt.dwEndMilliSec = sndSmfPlayAllMilliSec;
		smfOpt.amplitude = (UInt16)PrefGetPreference(prefAlarmSoundVolume);
		smfOpt.interruptible = true;
		smfOpt.reserved = 0;
		err = SndPlaySmf (NULL, sndSmfCmdPlay, midiStreamP, &smfOpt, NULL, NULL, false);
		
		// Unlock the record
		MemPtrUnlock (midiHdrP);
		}
	
	// Close the MIDI database
	if ( dbP )
		DmCloseDatabase (dbP);
		
	
	// If there was an error, play the built-in alarm sound
	if ( bError )
		SndPlaySystemSound(sndAlarm);
}


/***********************************************************************
 *
 * FUNCTION:    DoAlarmDialog
 *
 * DESCRIPTION: Handles the dialog for a single alarm
 *
 * PARAMETERS:  inAppointmentP		Appointment containing the alarm
 *
 * RETURNED:    Button pressed by user
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/27/99	Initial Revision
 *
 ***********************************************************************/
UInt16
DoAlarmDialog (
	PendingAlarmType *		inPendingInfoP )
{
	FormPtr						frm;
	FormPtr						curForm;
	UInt16						buttonHit;

	frm = FrmInitForm (AlarmDialog);

	curForm = FrmGetActiveForm ();
	if (curForm)
		FrmSetActiveForm (frm);

	// Set the event handler for the alarm dialog.
	FrmSetEventHandler (frm, ShowAlarmHandleEvent);

	// Store the infomation necessary to draw the alarm's description
	// in a gadget object.  This allows us to redraw the 
	// description on a frmUpdate event.
	FrmSetGadgetData (frm, FrmGetObjectIndex (frm, AlarmDescGadget), inPendingInfoP);

	FrmDrawForm (frm);
	DrawAlarmTitle ();
	DrawAlarm (inPendingInfoP);
	
	// Display the alarm dialog.
	buttonHit = FrmDoDialog (frm);

 	FrmDeleteForm (frm);
	FrmSetActiveForm (curForm);
	
	return buttonHit;
}


/***********************************************************************
 *
 * FUNCTION:    DisplayAlarm
 *
 * DESCRIPTION: This routine displays all pending alarms.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	8/30/95	Initial Revision
 *			rbb	4/22/99	Added snooze feature
 *
 ***********************************************************************/
void DBDisplayAlarm (void)
{
	UInt32	ref;
	UInt32 currentAlarmTime;
	UInt32 nextAlarmTime;
	UInt32 previousAlarmTime = 0;
	DmOpenRef dbP = 0;
	PendingAlarmType pendingInfo;
	UInt16 buttonHit = AlarmOKButton;
	UInt32 uniqueID;
	ApptDBRecordType appointment;
	MemHandle recordH;
	UInt16 dismissedCount;
	UInt32 * dismissedAlarms;
	PendingAlarmQueueType		alarmInternals;
	
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	char							debugStr[80];
	char							numStr[16];

	DbgSrcMessage ("\nDisplay alarm\n");
#endif
	
	dbP = DmOpenDatabaseByTypeCreator (datebookDBType, sysFileCDatebook, dmModeReadOnly);
	if (!dbP)
		{
		return;
		}

	ReserveAlarmInternals (&alarmInternals);
	
	// Get the list of pending alarms, if we don't have a list of pending 
	// alarms exit.
	if (GetPendingAlarmCount (&alarmInternals) == 0)
		{
		goto Exit;
		}
	
	// Get a pointer to the dismissed alarm list. If there's no list, we must be in an
	// extremely low memory condition, so exit.
	dismissedCount = GetDismissedAlarmCount (&alarmInternals);
	dismissedAlarms = GetDismissedAlarmList (&alarmInternals);
	if (dismissedAlarms == NULL)
		{
		ErrNonFatalDisplay ("Couldn't get dismissed alarms");
		goto Exit;
		}
	
	// Clear the snooze settings. This must be done before presenting the alarm dialog;
	// otherwise, AlarmTrigger will still think that it's returning from a snooze
	// and will foul up the pending alarms list.
	SetSnoozeAnchor (&alarmInternals, 0, apptNoAlarm);

	// Display alarms until there are no more pending or the user hits snooze
	while (true)
		{
		if (GetPendingAlarmCount (&alarmInternals) == 0)
			{
			ClearPendingAlarms (&alarmInternals);
			break;
			}

		// Get the record number and alarm time, of the next alarm, from 
		// the pending alarms list.
		GetPendingAlarm (&alarmInternals, 0, &pendingInfo);
		
		// Get the appointment record
		ApptGetRecord (dbP, pendingInfo.recordNum, &appointment, &recordH);
		DmRecordInfo (dbP, pendingInfo.recordNum, NULL, &uniqueID, NULL);
		currentAlarmTime = ApptGetAlarmTime (&appointment, pendingInfo.alarmTime);
		
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
		StrCopy (debugStr, "    description = \"");
		StrCat (debugStr, appointment.description);
		StrCat (debugStr, "\"\n");
		DbgSrcMessage (debugStr);
		StrCopy (debugStr, "    currentAlarmTime = ");
		StrIToH (numStr, currentAlarmTime);
		StrCat (debugStr, numStr);
		StrCat (debugStr, "\n\n");
		DbgSrcMessage (debugStr);
#endif
		
		
		// Show the alarm dialog
		if (currentAlarmTime != apptNoAlarm)
			{
			// Because we are working with a local copy of the alarm internals, we must
			// be careful to release them before presenting the alarm dialog. If we fail
			// to do so, any subsequent alarms that are received by AlarmTriggered(),
			// while the dialog is still up, will not appear in our copy of the internals.
			ReleaseAlarmInternals (&alarmInternals, true);
			
			buttonHit = DoAlarmDialog (&pendingInfo);
			
			ReserveAlarmInternals (&alarmInternals);
			}

		MemHandleUnlock (recordH);
		
		// If the snooze button was hit, keep the alarm in the pending queue
		// to be retriggered after the snooze
		if (buttonHit == AlarmSnoozeButton)
			{
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
			DbgSrcMessage ("    buttonHit == AlarmSnoozeButton\n");
#endif
			
			SetSnoozeAnchor (&alarmInternals, uniqueID, currentAlarmTime);
			AlarmSetTrigger (TimGetSeconds() + GetSnoozeDuration(&alarmInternals), 0);

			// If the user has dismissed alarms that triggered at the same time as the one just
			// snoozed, archive their uniqueIDs so that AlarmTriggered can filter them out
			// when resuming from the snooze.
			if (pendingInfo.alarmTime != previousAlarmTime)
				{
				ClearDismissedAlarms (&alarmInternals);
				}
			
			break;
			}
		else
			{
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
			DbgSrcMessage ("    buttonHit == AlarmOKButton\n");
#endif
			
			// Suspend alarm notification before fiddling with the pending alarms list
			// to avoid collisions with AlarmTriggered(), which also fiddles with the list
			nextAlarmTime = AlarmGetTrigger (&ref);
			AlarmSetTrigger (0, 0);
			
			// Remove the alarm from the pending list
			RemovePendingAlarm (&alarmInternals, 0);
			
			if (currentAlarmTime != previousAlarmTime)
				{
				ClearDismissedAlarms (&alarmInternals);
				}
				
			AppendDismissedAlarm (&alarmInternals, uniqueID);
			
			previousAlarmTime = currentAlarmTime;

			// Cancel any scheduled repeating alarm sounds and schedule the next
			// alarm
			if (nextAlarmTime && (ref > 0))
				{
				nextAlarmTime = ApptGetTimeOfNextAlarm (dbP, nextAlarmTime + minutesInSeconds);
				ref = 0;
				}
			
			// Restore alarm notification
			AlarmSetTrigger (nextAlarmTime, ref);
			}
		}
	
	// If not snoozing, clear the list of dismissed siblings
	if (buttonHit != AlarmSnoozeButton)
		{
		ClearDismissedAlarms (&alarmInternals);
		}
	
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	StrCopy (debugStr, "    GetDismissedAlarmCount() = ");
	StrIToA (numStr, GetDismissedAlarmCount (false));
	StrCat (debugStr, numStr);
	StrCat (debugStr, "\n");
	DbgSrcMessage (debugStr);
#endif
	
Exit:
	ReleaseAlarmInternals (&alarmInternals, true);

#if	EMULATION_LEVEL != EMULATION_NONE
	ECApptDBValidate (dbP);
#endif

	DmCloseDatabase (dbP);
}


/***********************************************************************
 *
 * FUNCTION:	AlarmTriggered
 *
 * DESCRIPTION:	This routine is called when the alarm manager informs the 
 *						datebook application that an alarm has triggered.
 *						THIS IS CALLED AT INTERRUPT LEVEL! DONT USE GLOBALS!!
 *
 * PARAMETERS:	time of the alarm.
 *
 * RETURNED:	time of the next alarm
 *
 * REVISION HISTORY:
 *			Name		Date		Description
 *			----		----		-----------
 *			art		9/20/95	Initial Revision
 *			frigino	9/9/97	Switched use of globals to use of prefs
 *									values instead.
 *			vmk		12/9/97	Call DatebookLoadPrefs() to load/fixup our app prefs
 *			rbb		4/22/99	Added snooze feature
 *
 ***********************************************************************/
void AlarmTriggered (SysAlarmTriggeredParamType * cmdPBP)
{
	DatebookPreferenceType		prefs;
	MemHandle						alarmListH;
	DatebookAlarmType*			alarmListP;
	DmOpenRef						dbP;
	UInt32							alarmRangeBegin;
	UInt32							alarmRangeEnd;
	UInt16							i;
	UInt16							alarmCount;
	Boolean							audible;
	UInt32							alarmTime;
	Boolean							playSound = false;
	Boolean							wasSnoozed;
	Boolean							newAlarm;
	UInt32							ref;
	UInt32							repeatSoundTime;
	UInt16							baseIndex = 0;
	Err								err = 0;
	PendingAlarmQueueType		alarmInternals;
	
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	char								debugStr[80];
	char								numStr[16];
	
	DbgSrcMessage ("\nAlarm triggered\n");
#endif
	
	// Open the appointment database.
	dbP = DmOpenDatabaseByTypeCreator (datebookDBType, sysFileCDatebook, 
		dmModeReadOnly);
	if (!dbP)
		return;

	// Read the preferences / saved-state information (will fix up incompatible versions)
	DatebookLoadPrefs (&prefs);
	ReserveAlarmInternals (&alarmInternals);

	// Establish the times for which alarms need to be retrieved.
	alarmRangeEnd = cmdPBP->alarmSeconds;
	alarmRangeBegin = GetSnoozeAnchorTime (&alarmInternals);
	
	wasSnoozed = (alarmRangeBegin != apptNoAlarm);
	newAlarm = (cmdPBP->ref == 0);
	
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	StrCopy(debugStr, "    ");
	StrIToH(numStr, alarmRangeBegin);
	StrCat(debugStr, numStr);
	StrCat(debugStr, " - ");
	StrIToH(numStr, alarmRangeEnd);
	StrCat(debugStr, numStr);
	StrCat(debugStr, "\n");
	DbgSrcMessage (debugStr);
#endif
	
	// If alarms were not snoozed, only the present alarm time needs to be handled
	if (!wasSnoozed)
		{
		alarmRangeBegin = alarmRangeEnd;
		}
	
	// Upon return from snooze, rebuild list of pending alarms, as record IDs may
	// have changed. At first glance, this may seem like a waste of time, throwing
	// out a list of possibly valid IDs, but, since we need to take a full pass through
	// the database to find newly triggered alarms anyway, we actually save time by
	// avoiding the need to take a second pass through the database for those times
	// when the record IDs actually do change.
	if (wasSnoozed)
		{
		ClearPendingAlarms (&alarmInternals);
		}
	
	// DBDisplayAlarm(), as currently implemented, displays all pending alarms (including
	// any alarms triggered while in DBDisplayAlarm, itself) prior to returning. If the
	// pending count is not zero, we can assume that DBDisplayAlarm is already up (or is
	// about to receive an earlier sysAppLaunchCmdDisplayAlarm event), so we can suppress
	// the unnecessary launch event.
	if (GetPendingAlarmCount (&alarmInternals) != 0)
		{
		cmdPBP->purgeAlarm = true;
		
		// Make sure not to interfere with the currently displayed alarm
		baseIndex = 1;
		}
	
	// If this is the first notification of an alarm, add it to the list of pending alarms.
	if (wasSnoozed || newAlarm)
		{
		// Get a list of all the appointments that have an alarm scheduled 
		// at the passed time.
		
		// Because we are working with a local copy of the alarm internals, we must
		// be careful to release them before calling ApptGetAlarmsList(), which also
		// to do so, any subsequent alarms that are received by AlarmTriggered(),
		// while the dialog is still up, will not appear in our copy of the internals.
		ReleaseAlarmInternals (&alarmInternals, true);
		alarmListH =  ApptGetAlarmsList (dbP, alarmRangeBegin, alarmRangeEnd, &alarmCount, &audible);
		ReserveAlarmInternals (&alarmInternals);

		if (alarmCount != 0)
			{
			UInt16 anchor = (UInt16) -1;
			
			alarmListP = (DatebookAlarmType *) MemHandleLock (alarmListH);
			
			// If returning from a snooze, confirm that the snooze alarm still exists and
			// hasn't been rescheduled, then place it at the beginning of the pending
			// alarms list.
			if (wasSnoozed)
				{
				for (i = 0; i < alarmCount; i++)
					{
					// For optimization, check for the correct alarm time before checking
					// for the correct unique ID
					if (alarmListP[i].alarmTime == alarmRangeBegin)
						{
						UInt32	uniqueID;
						DmRecordInfo (dbP, alarmListP[i].recordNum, NULL, &uniqueID, NULL);
						
						// If the time and ID both match, put the alarm at the beginning of
						// the pending alarm list and secure its position by setting baseIndex
						if (uniqueID == GetSnoozeAnchorUniqueID (&alarmInternals))
							{
							AddPendingAlarm (&alarmInternals, alarmListP[i].recordNum,
													alarmListP[i].alarmTime, 0);
							if (audible)
								{
								playSound = true;
								}
							
							anchor = i;
							baseIndex = 1;
							}
						}
					}
				}
			
			// Add the alarms that just trigggered to the pending alarms list.
			for (i = 0; i < alarmCount; i++)
				{
				// Skip over the snoozed alarm, if found above
				if (i == anchor)
					{
					continue;
					}
					
				err = AddPendingAlarm (&alarmInternals, alarmListP[i].recordNum,
												alarmListP[i].alarmTime, baseIndex);
				if (audible && (err != datebookErrAlarmListFull))
					{
					// Play the alarm sound unless the pending alarms list has become full. This
					// should save a little bit of battery life when the device is left unattended.
					playSound = true;
					}
				}
				
			// Free alarm data
			MemHandleFree (alarmListH);
			}
		else
			{
			// If our alarms disappeared, clear the snooze and don't bother calling
			// the display code
			SetSnoozeAnchor (&alarmInternals, 0, apptNoAlarm);
			cmdPBP->purgeAlarm = true;
			}
		}
	else
		{
		// If this it not a new alarm, then the alarm notification dialog is currently on
		// display, so we set the purgeAlarm flag to block the sysAppLaunchCmdDisplayAlarm
		// launch command.
		cmdPBP->purgeAlarm = true;
		audible = true;
		playSound = true;
		
		// Send the form an update command to refresh the clock display
//		FrmUpdateForm (AlarmDialog, frmRedrawUpdateCode);
		DrawAlarmTitle ();
		}

	// Play the alarm sound.
	if (playSound)
		PlayAlarmSound (prefs.alarmSoundUniqueRecID);

	// Get the time of the next alarm on any event.
	alarmTime = ApptGetTimeOfNextAlarm (dbP, alarmRangeEnd + minutesInSeconds);
	ref = 0;

	// If the time of the next event's alarm is after the time of the next 
	// repeat  sound, schedule an alarm for the repeat sound.
	if (audible && cmdPBP->ref < prefs.alarmSoundRepeatCount)
		{
		repeatSoundTime = cmdPBP->alarmSeconds + prefs.alarmSoundRepeatInterval;

		if (((alarmTime == 0) || (repeatSoundTime < alarmTime)) && 
			 (repeatSoundTime >= TimGetSeconds ()))
			{
			alarmTime = repeatSoundTime;
			ref = cmdPBP->ref + 1;
			}
		}

	AlarmSetTrigger (alarmTime, ref);

	#if	EMULATION_LEVEL != EMULATION_NONE
		ECApptDBValidate (dbP);
	#endif
	
	ReleaseAlarmInternals (&alarmInternals, true);

	DmCloseDatabase (dbP);
}


/***********************************************************************
 *
 * FUNCTION:    RescheduleAlarms
 *
 * DESCRIPTION: This routine computes the time of the next alarm and 
 *              compares it to the time of the alarm scheduled with
 *              Alarm Manager,  if they are different it reschedules
 *              the next alarm with the Alarm Manager.
 *
 * PARAMETERS:  dbP - the appointment database
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	9/5/95	Initial Revision
 *			rbb	4/22/99	Snoozing now disables other rescheduling of alarms
 *
 ***********************************************************************/
void RescheduleAlarms (DmOpenRef dbP)
{
	UInt32 ref;
	UInt32 timeInSeconds;
	UInt32 nextAlarmTime;
	UInt32 scheduledAlarmTime;
	PendingAlarmQueueType alarmInternals;
	
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	DbgSrcMessage ("RescheduleAlarms()\n");
#endif
	
	ReserveAlarmInternals (&alarmInternals);
	
	// The pending alarm queue used to be empty, as a rule, any time that
	// RescheduleAlarms could possibly be called. With the addition of the
	// snooze feature, the pending alarm queue may not be empty and, if this
	// is the case, could be invalidated by the changes that prompted the
	// call to RescheduleAlarms. We clear them here and will rebuild the
	// queue in AlarmTriggered().
	ClearPendingAlarms (&alarmInternals);
		
	// only reschedule if we're not snoozing
	if (GetSnoozeAnchorTime (&alarmInternals) == apptNoAlarm)
		{
		scheduledAlarmTime = AlarmGetTrigger (&ref);
		timeInSeconds = TimGetSeconds ();
		if ((timeInSeconds < scheduledAlarmTime) ||
		    (scheduledAlarmTime == 0) ||
		    (ref > 0))
			scheduledAlarmTime = timeInSeconds;
		
		nextAlarmTime = ApptGetTimeOfNextAlarm (dbP, scheduledAlarmTime);
		
		// If the scheduled time of the next alarm is not equal to the
		// calculate time of the next alarm,  reschedule the alarm with 
		// the alarm manager.
		if (scheduledAlarmTime != nextAlarmTime)
			{
			AlarmSetTrigger (nextAlarmTime, 0);
			}
		}
	else
		{
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
		DbgSrcMessage ("    exiting due to snooze\n");
#endif
		}
	
	ReleaseAlarmInternals (&alarmInternals, true);
}


/***********************************************************************
 *
 * FUNCTION:    AlarmReset
 *
 * DESCRIPTION: This routine is called when the system time is changed
 *              by the Preference application, or when the device is reset.
 *                
 * PARAMETERS:  newerOnly - If true, we will not reset the alarm if the 
 *              time of the next (as calculated) is greater then the 
 *              currently scheduled alarm.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	9/21/95	Initial Revision
 *       art	11/18.96	Reseting time will nolong trigger alarms.
 *			rbb	4/22/99	Reset snooze and any pending alarms
 *
 ***********************************************************************/
void AlarmReset (Boolean newerOnly)
{
	UInt32 ref;
	UInt32 currentTime;
	UInt32 alarmTime;
	DmOpenRef dbP;
	PendingAlarmQueueType alarmInternals;
		
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	DbgSrcMessage ("AlarmReset()\n");
#endif

	if (newerOnly)
		{
		alarmTime = AlarmGetTrigger (&ref);
		currentTime = TimGetSeconds ();
		if ( alarmTime && (alarmTime <= currentTime))
			return;
		}

	// Clear any pending alarms.
	AlarmSetTrigger (0, 0);
	
	ReserveAlarmInternals (&alarmInternals);
	SetSnoozeAnchor (&alarmInternals, 0, apptNoAlarm);
	ReleaseAlarmInternals (&alarmInternals, true);

	// Open the appointment database.
	dbP = DmOpenDatabaseByTypeCreator (datebookDBType, sysFileCDatebook, 
		dmModeReadOnly);
	if (!dbP) return;

	RescheduleAlarms (dbP);

	#if	EMULATION_LEVEL != EMULATION_NONE
		ECApptDBValidate (dbP);
	#endif

	DmCloseDatabase (dbP);
}


/***********************************************************************
 *
 * FUNCTION:    DisplayAlarm
 *
 * DESCRIPTION: This is a branch island for the DisplayAlarm function.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			scl	1/28/98	Initial Revision
 *
 ***********************************************************************/
// Note: We need to create a branch island to DBDisplayAlarm in order to successfully
//  link this application for the device.
void DisplayAlarm(void)
{
	DBDisplayAlarm();
}


#pragma mark -
/***********************************************************************
 *
 * FUNCTION:    ReserveAlarmInternals
 *
 * DESCRIPTION: Returns the internal struct which stores pending alarms,
 *					 etc. for use by AlarmTrigger and DBDisplayAlarm, et al.
 *					 THIS MAY BE CALLED AT INTERRUPT LEVEL (inInterrupt == true)!
 *					 IF SO, DONT USE GLOBALS!!
 *
 * PARAMETERS:  inInterrupt		If true, use FeatureMgr to retrieve
 *											application globals.
 *
 * RETURNED:    Pointer to the internal struct
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/29/99	Initial Revision
 *
 ***********************************************************************/
void
ReserveAlarmInternals (
	PendingAlarmQueueType*		outAlarmInternalsP )
{
/*
#pragma unused (inInterrupt)
	PendingAlarmQueueType**		alarmInternalsH;
	PendingAlarmQueueType*		alarmInternalsP;
	Err								err;
	
	err = FtrGet (sysFileCDatebook, alarmPendingListFeature, (UInt32*) &alarmInternalsH);
	if (err == ftrErrNoSuchFeature)
		{
		alarmInternalsH = (PendingAlarmQueueType **) MemHandleNew (sizeof (PendingAlarmQueueType));
		if (!alarmInternalsH)
			{
			ErrFatalDisplay ("Out of memory");
			return NULL;
			}
			
		MemHandleSetOwner ((MemHandle) alarmInternalsH, dmDynOwnerID);
		FtrSet (sysFileCDatebook, alarmPendingListFeature, (UInt32) alarmInternalsH);
		}
	
	alarmInternalsP = (PendingAlarmQueueType *) MemHandleLock ((MemHandle) alarmInternalsH);
	
	if (err == ftrErrNoSuchFeature)
		{
		MemSet (alarmInternalsP, sizeof (PendingAlarmQueueType), 0);
		alarmInternalsP->snoozeAnchorTime = apptNoAlarm;
		}
	
	return alarmInternalsP;
*/
	
	
//	static PendingAlarmQueueType	alarmInternals;
	UInt16								prefsSize = sizeof (PendingAlarmQueueType);
	Int16									prefsVersion;
	
	prefsVersion = PrefGetAppPreferences (sysFileCDatebook, datebookUnsavedPrefID,
														outAlarmInternalsP, &prefsSize, false);
	
	if (prefsVersion == noPreferenceFound)
		{
		MemSet (outAlarmInternalsP, sizeof (PendingAlarmQueueType), 0);
		outAlarmInternalsP->snoozeAnchorTime = apptNoAlarm;
		
		PrefSetAppPreferences (sysFileCDatebook, datebookUnsavedPrefID,
										datebookUnsavedPrefsVersionNum, outAlarmInternalsP,
										sizeof (PendingAlarmQueueType), false);
		}
}


/***********************************************************************
 *
 * FUNCTION:    ReleaseAlarmInternals
 *
 * DESCRIPTION: Releases the internal struct which stores pending alarms,
 *					 etc. for use by AlarmTrigger and DBDisplayAlarm, et al.
 *					 THIS MAY BE CALLED AT INTERRUPT LEVEL, SO DONT USE GLOBALS!!
 *
 * PARAMETERS:  inAlarmInternalsP	Pointer to the internal struct
 *					 inModified				If true, use FeatureMgr to retrieve
 *												application globals.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/29/99	Initial Revision
 *
 ***********************************************************************/
void
ReleaseAlarmInternals (
	PendingAlarmQueueType *	inAlarmInternalsP,
	Boolean						inModified )
{
//#pragma unused (inInterrupt)
//	MemPtrUnlock (inAlarmInternalsP);

#if ERROR_CHECK_LEVEL == ERROR_CHECK_FULL
	PendingAlarmQueueType	prefs;
	UInt16						prefsSize = sizeof (PendingAlarmQueueType);
	Int16							prefsVersion;
	
	prefsVersion = PrefGetAppPreferences (sysFileCDatebook, datebookUnsavedPrefID,
														&prefs, &prefsSize, false);
	
	if (!inModified && (MemCmp (&prefs, &inAlarmInternalsP, prefsSize) != 0))
		{
		ErrNonFatalDisplay ("ReleaseAlarmInternals detects changes, but inModified==false");
		}
#endif
	
	
	if (inModified)
		{
		PrefSetAppPreferences (sysFileCDatebook, datebookUnsavedPrefID,
										datebookUnsavedPrefsVersionNum, inAlarmInternalsP,
										sizeof (PendingAlarmQueueType), false);
		}
	
#if ERROR_CHECK_LEVEL != ERROR_CHECK_NONE
		MemSet (inAlarmInternalsP, sizeof (PendingAlarmQueueType), 0x55);
#endif
}


#pragma mark -
/***********************************************************************
 *
 * FUNCTION:    GetPendingAlarmCount
 *
 * DESCRIPTION: Returns size of pending alarms queue.
 *					 THIS MAY BE CALLED AT INTERRUPT LEVEL (inInterrupt == true)!
 *					 IF SO, DONT USE GLOBALS!!
 *
 * PARAMETERS:  inInterrupt		If true, use FeatureMgr to retrieve
 *											application globals.
 *
 * RETURNED:    Number of items in pending alarms queue
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/29/99	Initial Revision
 *
 ***********************************************************************/
static UInt16
GetPendingAlarmCount (
	PendingAlarmQueueType*	inAlarmInternalsP )
{
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	char								debugStr[80];
	char								numStr[16];
	
	StrCopy (debugStr, "GetPendingAlarmCount() returns ");
	StrIToH(numStr, inAlarmInternalsP->pendingCount);
	StrCat(debugStr, numStr);
	StrCat(debugStr, "\n");
	DbgSrcMessage (debugStr);
#endif

	return inAlarmInternalsP->pendingCount;
}


/***********************************************************************
 *
 * FUNCTION:    SetPendingAlarmCount
 *
 * DESCRIPTION: Set size of pending alarms queue. Assumes count is
 *					 within range.
 *					 THIS MAY BE CALLED AT INTERRUPT LEVEL (inInterrupt == true)!
 *					 IF SO, DONT USE GLOBALS!!
 *
 * PARAMETERS:  inCount				Number of items in pending alarms queue
 *					 inInterrupt		If true, use FeatureMgr to retrieve
 *											application globals.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/29/99	Initial Revision
 *
 ***********************************************************************/
static void
SetPendingAlarmCount (
	PendingAlarmQueueType*	inAlarmInternalsP,
	UInt16						inCount)
{
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	char								debugStr[80];
	char								numStr[16];
	
	StrCopy (debugStr, "SetPendingAlarmCount (");
	StrIToH(numStr, inCount);
	StrCat(debugStr, numStr);
	StrCat(debugStr, ")\n");
	DbgSrcMessage (debugStr);
#endif

	inAlarmInternalsP->pendingCount = inCount;
}


/***********************************************************************
 *
 * FUNCTION:    GetPendingAlarm
 *
 * DESCRIPTION: Retrieves an alarm from the pending alarm queue
 *					 THIS MAY BE CALLED AT INTERRUPT LEVEL (inInterrupt == true)!
 *					 IF SO, DONT USE GLOBALS!!
 *
 * PARAMETERS:  inAlarmIndex		The position of the alarm to be removed
 *					 inInterrupt		If true, use FeatureMgr to retrieve
 *											application globals.
 *
 * RETURNED:    outPendingInfo	The pending alarm
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/29/99	Initial Revision
 *
 ***********************************************************************/
static void
GetPendingAlarm (
	PendingAlarmQueueType*	inAlarmInternalsP,
	UInt16						inAlarmIndex,
	PendingAlarmType*			outPendingInfoP )
{
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	char								debugStr[80];
	char								numStr[16];
	
	StrCopy (debugStr, "GetPendingAlarm (");
	StrIToH(numStr, inAlarmIndex);
	StrCat(debugStr, numStr);
	StrCat(debugStr, ")\n");
	DbgSrcMessage (debugStr);
	
	StrCopy(debugStr, "    recordNum = ");
	StrIToH(numStr, outPendingInfoP->recordNum);
	StrCat(debugStr, numStr);
	StrCat(debugStr, "\n");
	DbgSrcMessage (debugStr);
	
	StrCopy(debugStr, "    alarmTime = ");
	StrIToH(numStr, outPendingInfoP->alarmTime);
	StrCat(debugStr, numStr);
	StrCat(debugStr, "\n");
	DbgSrcMessage (debugStr);
#endif

	*outPendingInfoP = inAlarmInternalsP->pendingAlarm [inAlarmIndex];
}


/***********************************************************************
 *
 * FUNCTION:    SetPendingAlarm
 *
 * DESCRIPTION: Sets an alarm in the pending alarm queue. Assumes that
 *					 inAlarmIndex is within range.
 *					 THIS MAY BE CALLED AT INTERRUPT LEVEL (inInterrupt == true)!
 *					 IF SO, DONT USE GLOBALS!!
 *
 * PARAMETERS:  inAlarmIndex		The position of the alarm to be removed
 *					 inPendingIndo		the pending alarm
 *					 inInterrupt		If true, use FeatureMgr to retrieve
 *											application globals.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/29/99	Initial Revision
 *
 ***********************************************************************/
static void
SetPendingAlarm (
	PendingAlarmQueueType*	inAlarmInternalsP,
	UInt16						inAlarmIndex,
	PendingAlarmType*			inPendingInfoP )
{
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	char								debugStr[80];
	char								numStr[16];
	
	StrCopy (debugStr, "SetPendingAlarm (");
	StrIToH(numStr, inAlarmIndex);
	StrCat(debugStr, numStr);
	StrCat(debugStr, ", [");
	
	StrIToH(numStr, inPendingInfoP->recordNum);
	StrCat(debugStr, numStr);
	StrCat(debugStr, ", ");
	
	StrIToH(numStr, inPendingInfoP->alarmTime);
	StrCat(debugStr, numStr);
	StrCat(debugStr, "])\n");
	DbgSrcMessage (debugStr);
#endif

	inAlarmInternalsP->pendingAlarm [inAlarmIndex] = *inPendingInfoP;
}


/***********************************************************************
 *
 * FUNCTION:    AddPendingAlarm
 *
 * DESCRIPTION: This routine add an entry to the pending alarms list.
 *
 * PARAMETERS:  record index
 *              time of the alarm.
 *
 * RETURNED:    true if the alarm  has added to the pending alarms list,
 *              false if the alarm replaced a pending alarm or has not 
 *              added at all.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	9/20/95	Initial Revision
 *
 ***********************************************************************/
static Err
AddPendingAlarm (
	PendingAlarmQueueType*	inAlarmInternalsP,
	UInt16						recordNum,
	UInt32						alarmTime,
	UInt16						inBaseIndex )
{
	UInt32						count;
	PendingAlarmType			baseAlarm;
	PendingAlarmType			tempAlarm;
	PendingAlarmType			newAlarm;
	Err							err = 0;
	UInt16						alarmIndex;
	
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	char							debugStr[80];
	char							numStr[16];
	
	StrCopy (debugStr, "AddPendingAlarm (");
	StrIToH (numStr, recordNum);
	StrCat (debugStr, numStr);
	StrCat (debugStr, ", ");
	StrIToH (numStr, alarmTime);
	StrCat (debugStr, numStr);
	StrCat (debugStr, ")\n");
	DbgSrcMessage (debugStr);
#endif
	
	count = GetPendingAlarmCount (inAlarmInternalsP);
	GetPendingAlarm (inAlarmInternalsP, inBaseIndex, &baseAlarm);
	
	// As a fail safe, make sure that the given record isn't already in the list
	for (alarmIndex = 0; alarmIndex < count; ++alarmIndex)
		{
		GetPendingAlarm (inAlarmInternalsP, alarmIndex, &tempAlarm);
		if (recordNum == tempAlarm.recordNum)
			{
			return datebookErrDuplicateAlarm;
			}
		}
	
	// If we're over the limit and the given alarm is newer than the oldest one
	// in the list, remove the oldest one
	if ((count >= apptMaxDisplayableAlarms) && (alarmTime > baseAlarm.alarmTime))
		{
		RemovePendingAlarm (inAlarmInternalsP, inBaseIndex);
		count = apptMaxDisplayableAlarms - 1;
		err = datebookErrAlarmListFull;
		}
	
	// Find the index at which the given alarm should be inserted
	for (alarmIndex = inBaseIndex; alarmIndex < count; ++alarmIndex)
		{
		GetPendingAlarm (inAlarmInternalsP, alarmIndex, &tempAlarm);
		if (alarmTime < tempAlarm.alarmTime)
			{
			break;
			}
		}
	
	newAlarm.recordNum = recordNum;
	newAlarm.alarmTime = alarmTime;
	InsertPendingAlarm (inAlarmInternalsP, alarmIndex, &newAlarm);

	return err;
}


/***********************************************************************
 *
 * FUNCTION:    InsertPendingAlarm
 *
 * DESCRIPTION: Inserts an alarm into the pending alarm queue
 *					 THIS MAY BE CALLED AT INTERRUPT LEVEL (inInterrupt == true)!
 *					 IF SO, DONT USE GLOBALS!!
 *
 * PARAMETERS:  inAlarmIndex		The position at which to insert the alarm
 *					 inInterrupt		If true, use FeatureMgr to retrieve
 *											application globals.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/29/99	Initial Revision
 *
 ***********************************************************************/
static void
InsertPendingAlarm (
	PendingAlarmQueueType*	inAlarmInternalsP,
	UInt16						inAlarmIndex,
	PendingAlarmType *		inPendingInfo )
{
	UInt32						count;

	count = GetPendingAlarmCount (inAlarmInternalsP);
	if (count >= apptMaxDisplayableAlarms)
		{
		return;
		}
	
	SetPendingAlarmCount (inAlarmInternalsP, count + 1);
	
	if (inAlarmIndex < count)
		{
		MemMove (&inAlarmInternalsP->pendingAlarm [inAlarmIndex + 1],
					&inAlarmInternalsP->pendingAlarm [inAlarmIndex],
					(count - inAlarmIndex - 1) * sizeof (PendingAlarmType) );
		}

	SetPendingAlarm (inAlarmInternalsP, inAlarmIndex, inPendingInfo);
}


/***********************************************************************
 *
 * FUNCTION:    RemovePendingAlarm
 *
 * DESCRIPTION: Removes an alarm from the pending alarm queue
 *					 THIS MAY BE CALLED AT INTERRUPT LEVEL (inInterrupt == true)!
 *					 IF SO, DONT USE GLOBALS!!
 *
 * PARAMETERS:  inAlarmIndex		The position of the alarm to be removed
 *					 inInterrupt		If true, use FeatureMgr to retrieve
 *											application globals.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/29/99	Initial Revision
 *
 ***********************************************************************/
static void
RemovePendingAlarm (
	PendingAlarmQueueType*	inAlarmInternalsP,
	UInt16						inAlarmIndex )
{
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	char							debugStr[80];
	char							numStr[16];
#endif

	MemMove (&inAlarmInternalsP->pendingAlarm [inAlarmIndex],
				&inAlarmInternalsP->pendingAlarm [inAlarmIndex + 1],
	 			(inAlarmInternalsP->pendingCount - inAlarmIndex - 1) * sizeof (PendingAlarmType) );
	--(inAlarmInternalsP->pendingCount);
		
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	StrCopy (debugStr, "    alarm removed from pending list, count = ");
	StrIToA (numStr, inAlarmInternalsP->pendingCount);
	StrCat (debugStr, numStr);
	StrCat (debugStr, "\n");
	DbgSrcMessage (debugStr);
#endif
}


/***********************************************************************
 *
 * FUNCTION:    ClearPendingAlarms
 *
 * DESCRIPTION: 
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/29/99	Initial Revision
 *
 ***********************************************************************/
static void
ClearPendingAlarms (
	PendingAlarmQueueType*	inAlarmInternalsP )
{
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	UInt16	i;
	PendingAlarmType pending = {0x1234, 0x56789ABC};
	
	char								debugStr[80];
	char								numStr[16];
	
	DbgSrcMessage ("ClearPendingAlarms\n");
	
	for (i = 0; i < apptMaxDisplayableAlarms; i++)
		{
		SetPendingAlarm (inAlarmInternalsP, i, &pending);
		}
#endif
		
	inAlarmInternalsP->pendingCount = 0;
}


#pragma mark -
/***********************************************************************
 *
 * FUNCTION:    GetDismissedAlarmCount
 *
 * DESCRIPTION: Returns size of dismissed alarms queue.
 *					 THIS MAY BE CALLED AT INTERRUPT LEVEL (inInterrupt == true)!
 *					 IF SO, DONT USE GLOBALS!!
 *
 * PARAMETERS:  inInterrupt		If true, use FeatureMgr to retrieve
 *											application globals.
 *
 * RETURNED:    Number of items in dismissed alarms queue
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/29/99	Initial Revision
 *
 ***********************************************************************/
UInt16
GetDismissedAlarmCount (
	PendingAlarmQueueType*	inAlarmInternalsP )
{
	return inAlarmInternalsP->dismissedCount;
}


/***********************************************************************
 *
 * FUNCTION:    GetDismissedAlarmList
 *
 * DESCRIPTION: Returns list of dismissed alarms.
 *					 THIS MAY BE CALLED AT INTERRUPT LEVEL (inInterrupt == true)!
 *					 IF SO, DONT USE GLOBALS!!
 *
 * PARAMETERS:  inInterrupt		If true, use FeatureMgr to retrieve
 *											application globals.
 *
 * RETURNED:    Number of items in dismissed alarms queue
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/29/99	Initial Revision
 *
 ***********************************************************************/
UInt32 *
GetDismissedAlarmList (
	PendingAlarmQueueType*	inAlarmInternalsP )
{
	return inAlarmInternalsP->dismissedAlarm;
}


/***********************************************************************
 *
 * FUNCTION:    AppendDismissedAlarm
 *
 * DESCRIPTION: Adds an alarm onto the dismissed alarm queue.
 *					 THIS MAY BE CALLED AT INTERRUPT LEVEL (inInterrupt == true)!
 *					 IF SO, DONT USE GLOBALS!!
 *
 * PARAMETERS:  inAlarmIndex		The position at which to insert the alarm
 *					 inInterrupt		If true, use FeatureMgr to retrieve
 *											application globals.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/29/99	Initial Revision
 *
 ***********************************************************************/
static void
AppendDismissedAlarm (
	PendingAlarmQueueType*	inAlarmInternalsP,
	UInt32						inUniqueID )
{
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	char							debugStr[80];
	char							numStr[16];
#endif

	inAlarmInternalsP->dismissedAlarm [inAlarmInternalsP->dismissedCount] = inUniqueID;
	++(inAlarmInternalsP->dismissedCount);
		
#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	DbgSrcMessage ("    alarm added to dismissed list\n");
	StrCopy (debugStr, "        uniqueID = ");
	StrIToA (numStr, inUniqueID);
	StrCat (debugStr, numStr);
	StrCat (debugStr, "\n");
	DbgSrcMessage (debugStr);
	StrCopy (debugStr, "        dismissedCount = ");
	StrIToA (numStr, inAlarmInternalsP->dismissedCount);
	StrCat (debugStr, numStr);
	StrCat (debugStr, "\n");
	DbgSrcMessage (debugStr);
#endif
}


/***********************************************************************
 *
 * FUNCTION:    ClearDismissedAlarms
 *
 * DESCRIPTION: 
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	4/29/99	Initial Revision
 *
 ***********************************************************************/
static void
ClearDismissedAlarms (
	PendingAlarmQueueType*	inAlarmInternalsP)
{
	inAlarmInternalsP->dismissedCount = 0;

#if	DATEBOOK_DEBUG_LEVEL == DATEBOOK_DEBUG_FULL
	DbgSrcMessage ("    all alarms cleared from dismissed list\n");
#endif
}

