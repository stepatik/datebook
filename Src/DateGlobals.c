/******************************************************************************
 *
 * Copyright (c) 1995-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: DateGlobals.c
 *
 * Description:
 *	  Because of a bug in Metrowerks, we must compile the globals separately with
 *		PC-relative strings turned off. Otherwise, we get linker errors with
 *		pre-initialized structures.
 *
 * History:
 *		June 12, 1995	Created by Art Lamb
 *		09/09/97	frigino Added initial values to AlarmSoundXXXX vars
 *		03/05/99	grant	DetailsH replaced by DetailsP
 *		04/23/99	rbb	Added AlarmSnooze
 *		08/03/99	kwk	Removed initialization of font globals - they all
 *							have to be set up in StartApplication.
 *		11/04/99	jmp	Added InPhoneLookup global.
 *
 *****************************************************************************/

#include <PalmOS.h>

#define	NON_PORTABLE
//#include "MemoryPrv.h"

#include "Datebook.h"

/***********************************************************************
 *
 *	Global variables
 *
 ***********************************************************************/
MemHandle			ApptsOnlyH;
UInt16				NumApptsOnly;
MemHandle			ApptsH;
UInt16				NumAppts;

DmOpenRef			ApptDB;									// datebook database
DateType				Date = { 91, 7, 31 };				// date currently displayed
TimeFormatType		TimeFormat;
DateFormatType		LongDateFormat;
DateFormatType		ShortDateFormat;
UInt16				TopVisibleAppt;
privateRecordViewEnum	PrivateRecordVisualStatus;
Boolean				NoteUpScrollerVisible;				// true if note can be scroll winUp
Boolean				NoteDownScrollerVisible;			// true if note can be scroll winUp
UInt16				PendingUpdate = 0;					// code of pending day view update
RGBColorType 		colorLine = {0x00, 0x77, 0x77, 0x77};	// like 0x88 but draws as black in 1bpp mode

Boolean				InPhoneLookup = false;				// true if we've called PhoneNumberLookup()

// The following global variables are used to keep track of the edit
// state of the application.
UInt16				DayEditPosition = 0;					// position of the insertion point in the desc field
UInt16				DayEditSelectionLength;				// length of the current selection.
UInt16				CurrentRecord = noRecordSelected;// record being edited
Boolean				ItemSelected = false;				// true if a day view item is selected
Boolean				RecordDirty = false;					// true if a record has been modified


// The following global variable are only valid while editng the detail
// of an appointment.
void *				DetailsP;
DateType				RepeatEndDate;
RepeatType			RepeatingEventType;

// The following global variables are only valid while editing the repeat information
// of an appointment
void *				RepeatDetailsP;

// The following global variable are saved to a state file.
UInt16				DayStartHour = defaultDayStartHour;	// start of the day 8:00am
UInt16				DayEndHour = defaultDayEndHour;		// end of the day 6:00pm
UInt16				StartDayOfWeek = sunday;
UInt16				RepeatStartOfWeek = sunday;		//	status of Repeat Dialog
FontID				NoteFont;										// font used in note view
AlarmInfoType		AlarmPreset = { defaultAlarmPresetAdvance, defaultAlarmPresetUnit };
Boolean				SaveBackup = defaultSaveBackup;				// default setting "Backuo tp PC" checkbox
Boolean				ShowTimeBars = defaultShowTimeBars;			// show time bars in the day view
Boolean				CompressDayView = defaultCompressDayView;	// remove empty time slot to prevent scrolling
Boolean				ShowTimedAppts = defaultShowTimedAppts;	// show timed appointments in month view
Boolean				ShowUntimedAppts = defaultShowUntimedAppts;	// show untimed appointments in month view
Boolean				ShowDailyRepeatingAppts = defaultShowDailyRepeatingAppts;	// show daily repeating appointments in month view

#if 0		// moved to DatePref.c	vmk 12/9/97
	// The following global variable are only valid while editng the datebook's
	// preferences.
	UInt16			PrefDayStartHour;
	UInt16			PrefDayEndHour;
#endif

// Tracks whether an input event has occurred since entering the Week View.
// The Datebook hard button uses this to choose the Day or Month View.
Boolean				EventInCurrentView;


UInt16				TimeBarColumns;						// Number of columns of time bars.


// The following global variable is used to control the displaying of the
// current time in the title of a view.
Boolean				TimeDisplayed = false;				// True if time in been displayed
UInt32				TimeDisplayTick;						// Tick count when we stop showing time

// The following globals are for the repeat rates of the alarms.
																		// number of times to repeat alarm sound 
UInt16				AlarmSoundRepeatCount = defaultAlarmSoundRepeatCount;
																	
																		// interval between repeat sounds, in seconds
UInt16				AlarmSoundRepeatInterval = defaultAlarmSoundRepeatInterval;

																		// Alarm sound MIDI file unique ID record identifier
UInt32				AlarmSoundUniqueRecID = defaultAlarmSoundUniqueRecID;

FontID				ApptDescFont;								// font for drawing event description.
																			
UInt16				AlarmSnooze = defaultAlarmSnooze;		// snooze delay, in seconds

UInt16				DismissedAlarmCount = 0;
UInt32				DismissedAlarms [apptMaxDisplayableAlarms] = {};

