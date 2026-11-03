/******************************************************************************
 * Copyright (c) 1995-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: Datebook.c
 *
 * Description:
 *	  This is the Datebook application's main module.  This module
 *   starts the application, dispatches events, and stops
 *   the application.
 *
 * History:
 *		June 12, 1995	Created by Art Lamb
 *			Name		Date		Description
 *			----		----		-----------
 *			???		????		Initial Revision
 *			frigino	970909	Added alarmSoundUniqueRecID to DatebookPreferenceType
 *									to remember the alarm sound to play. Moved
 *									DatebookPreferenceType out of this file.
 *			grant		3/5/99	Removed dependece on MemDeref and MemoryPrv.h.
 *									DetailsH was a handle that was always left locked;
 *									replaced by a pointer DetailsP.
 *			rbb		4/9/99	Removed time bar and end-time for zero-duration appts
 *			rbb		4/22/99	Added snooze
 *			rbb		6/10/99	Removed obsoleted code that worked around
 *									single-segment linker limitation
 *			grant		6/28/99	New global - RepeatDetailsP.  When editing an event's details,
 *									there is one details info block that is pointed to by either
 *									DetailsP or RepeatDetailsP but not both.  When the "Details"
 *									form is active, then DetailsP is valid and RepeatDetailsP
 *									should be NULL.  And vice versa for the "Repeat" form.
 *
 *****************************************************************************/

#include <PalmOS.h>
#include <TxtGlue.h>

#include "Datebook.h"
#include "CalendarSync.h"

/***********************************************************************
 *
 *	Global variables, declarded in DateGlobals.c.  Because of a bug in
 *  the Metrowerks compiler, we must compile the globals separately with
 *	 PC-relative strings turned off.
 *
 ***********************************************************************/

extern	UInt16					TopVisibleAppt;
extern privateRecordViewEnum	PrivateRecordVisualStatus;

// The following global variables are used to keep track of the edit
// state of the application.
extern 	UInt16					CurrentRecord;						// record being edited
extern 	Boolean					ItemSelected;						// true if a day view item is selected
extern 	UInt16					DayEditPosition;					// position of the insertion point in the desc field
extern	UInt16					DayEditSelectionLength;			// length of the current selection.
extern	Boolean					RecordDirty;						// true if a record has been modified


/***********************************************************************
 *
 *	Internal Functions
 *
 ***********************************************************************/

static void EventLoop (void);


/***********************************************************************
 *
 *	Test Code
 *
 ***********************************************************************/

// Useful structure field offset and size macros
#define prvFieldOffset(type, field)		((UInt32)(&((type*)0)->field))
#define prvFieldSize(type, field)		(sizeof(((type*)0)->field))



/***********************************************************************
 *
 * FUNCTION:     SetDBBackupBit
 *
 * DESCRIPTION:  This routine sets the backup bit on the given database.
 *					  This is to aid syncs with non Palm software.
 *					  If no DB is given, open the app's default database and set
 *					  the backup bit on it.
 *
 * PARAMETERS:   dbP -	the database to set backup bit,
 *								can be NULL to indicate app's default database
 *
 * RETURNED:     nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			grant	4/1/99	Initial Revision
 *
 ***********************************************************************/
static void SetDBBackupBit(DmOpenRef dbP)
{
	DmOpenRef localDBP;
	LocalID dbID;
	UInt16 cardNo;
	UInt16 attributes;

	// Open database if necessary. If it doesn't exist, simply exit (don't create it).
	if (dbP == NULL)
		{
		localDBP = DmOpenDatabaseByTypeCreator (datebookDBType, sysFileCDatebook, dmModeReadWrite);
		if (localDBP == NULL)  return;
		}
	else
		{
		localDBP = dbP;
		}

	// now set the backup bit on localDBP
	DmOpenDatabaseInfo(localDBP, &dbID, NULL, NULL, &cardNo, NULL);
	DmDatabaseInfo(cardNo, dbID, NULL, &attributes, NULL, NULL,
				NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	attributes |= dmHdrAttrBackup;
	DmSetDatabaseInfo(cardNo, dbID, NULL, &attributes, NULL, NULL,
				NULL, NULL, NULL, NULL, NULL, NULL, NULL);

	// close database if necessary
   if (dbP == NULL)
   	{
   	DmCloseDatabase(localDBP);
      }
}


/***********************************************************************
 *
 * FUNCTION:     DateGetDatabase
 *
 * DESCRIPTION:  Get the application's database.  Open the database if it
 * exists, create it if neccessary.
 *
 * PARAMETERS:   *dbPP - pointer to a database ref (DmOpenRef) to be set
 *					  mode - how to open the database (dmModeReadWrite)
 *
 * RETURNED:     Err - zero if no error, else the error
 *
 * REVISION HISTORY:
 *			Name		Date		Description
 *			----		----		-----------
 *			roger		12/3/97	Initial Revision
 *
 ***********************************************************************/
static Err DateGetDatabase (DmOpenRef *dbPP, UInt16 mode)
{
	Err error = 0;
	DmOpenRef dbP;
	UInt16 cardNo;
	LocalID dbID;


	*dbPP = 0;
	dbP = DmOpenDatabaseByTypeCreator(datebookDBType, sysFileCDatebook, mode);
	if (! dbP)
		{
		error = DmCreateDatabase (0, datebookDBName, sysFileCDatebook,
								datebookDBType, false);
		if (error) return error;

		dbP = DmOpenDatabaseByTypeCreator(datebookDBType, sysFileCDatebook, mode);
		if (! dbP) return ~0;

		// Set the backup bit.  This is to aid syncs with non Palm software.
		SetDBBackupBit(dbP);

		error = ApptAppInfoInit (dbP);
      if (error)
      	{
			DmOpenDatabaseInfo(dbP, &dbID, NULL, NULL, &cardNo, NULL);
      	DmCloseDatabase(dbP);
      	DmDeleteDatabase(cardNo, dbID);
         return error;
         }
		}

	*dbPP = dbP;
	return 0;
}


/***********************************************************************
 *
 * FUNCTION:     StartApplication
 *
 * DESCRIPTION:  This routine opens the application's database, loads the
 *               saved-state information and initializes global variables.
 *
 * PARAMETERS:   nothing
 *
 * RETURNED:     nothing
 *
 * REVISION HISTORY:
 *			Name		Date		Description
 *			----		----		-----------
 *			art		6/12/95	Initial Revision
 *			trm		8/5/97	adds variable alarm sound settings
 *			frigino	9/9/97	Add AlarmSoundUniqueRecID initialization
 *			vmk		12/9/97	Call DatebookLoadPrefs() to load and fixup prefs
 *			rbb		6/20/99	Removed multi-segment workaround
 *
 ***********************************************************************/
static UInt16 StartApplication (void)
{
	Err err = 0;
	UInt16 mode;
	DateTimeType dateTime;
	DatebookPreferenceType prefs;
	Int16 prefsVersion;
	SystemPreferencesType sysPrefs;


	// Determime if secret record should be shown.
	PrefGetPreferences (&sysPrefs);
	if (PrefGetPreference (prefVersion) >= 8)
		PrivateRecordVisualStatus = (privateRecordViewEnum)PrefGetPreference(prefShowPrivateRecords);
	else
		PrivateRecordVisualStatus = sysPrefs.hideSecretRecords ? hidePrivateRecords : showPrivateRecords;
	mode = (PrivateRecordVisualStatus == hidePrivateRecords) ?
					dmModeReadWrite : (dmModeReadWrite | dmModeShowSecret);


	// Get the time formats from the system preferences.
	TimeFormat = (TimeFormatType)PrefGetPreference(prefTimeFormat);

	// Get the date formats from the system preferences.
	LongDateFormat = (DateFormatType)PrefGetPreference(prefLongDateFormat);
	ShortDateFormat = (DateFormatType)PrefGetPreference(prefDateFormat);

	// Get the starting day of the week from the system preferences.
	StartDayOfWeek = PrefGetPreference(prefWeekStartDay);


	// Get today's date.
	TimSecondsToDateTime (TimGetSeconds (), &dateTime);
	Date.year = dateTime.year - firstYear;
	Date.month = dateTime.month;
	Date.day = dateTime.day;


	// Find the application's data file.  If it don't exist create it.
	err = DateGetDatabase (&ApptDB, mode);
	if (err)	return err;


	// Read the preferences / saved-state information (will fix up incompatible versions).
	prefsVersion = DatebookLoadPrefs (&prefs);
	DayStartHour = prefs.dayStartHour;
	DayEndHour = prefs.dayEndHour;
	AlarmPreset = prefs.alarmPreset;
	NoteFont = prefs.noteFont;
	SaveBackup = prefs.saveBackup;
	ShowTimeBars = prefs.showTimeBars;
	CompressDayView = prefs.compressDayView;
	ShowTimedAppts = prefs.showTimedAppts;
	ShowUntimedAppts = prefs.showUntimedAppts;
	ShowDailyRepeatingAppts = prefs.showDailyRepeatingAppts;

	AlarmSoundRepeatCount = prefs.alarmSoundRepeatCount;
	AlarmSoundRepeatInterval = prefs.alarmSoundRepeatInterval;
	AlarmSoundUniqueRecID = prefs.alarmSoundUniqueRecID;
	ApptDescFont = prefs.apptDescFont;

	AlarmSnooze = prefs.alarmSnooze;

	TopVisibleAppt = 0;
	CurrentRecord = noRecordSelected;


 	// Initialize the alarm handling mechanism
	AlarmInit();
	return 0;
}


/***********************************************************************
 *
 * FUNCTION:    StopApplication
 *
 * DESCRIPTION: This routine closes the application's database
 *              and saves the current state of the application.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name		Date		Description
 *			----		----		-----------
 *			art		6/12/95		Initial Revision
 *			trm		8/5/97		adds variable alarm sound settings
 *			frigino	9/9/97		Add saving of AlarmSoundUniqueRecID
 *			vmk		12/9/97		Call DatebookSavePrefs() to write out the prefs
 *			rbb		6/20/99		Removed multi-segment workaround
 *
 ***********************************************************************/
static void StopApplication (void)
{

	DatebookSavePrefs();

	// Send a frmSave event to all the open forms.
	FrmSaveAllForms ();

	// Close all the open forms.
	FrmCloseAllForms ();

	// Close the application's data file.
	DmCloseDatabase (ApptDB);
}

/***********************************************************************
 *
 * FUNCTION:		DatebookLoadPrefs
 *
 * DESCRIPTION:	Loads app's preferences and fixes them up if they didn't exist
 *						or were of the wrong version.
 *
 * PARAMETERS:		prefsP		-- ptr to preferences structure to fill in
 *
 * RETURNED:		the version of preferences from which values were read
 *
 * REVISION HISTORY:
 *			Name		Date		Description
 *			----		----		-----------
 *			vmk		12/9/97	Initial version
 *			vmk		12/11/97	Fix up note font
 *			rbb		4/23/99	Added alarmSnooze
 *
 ***********************************************************************/
Int16 DatebookLoadPrefs (DatebookPreferenceType* prefsP)
{
	UInt16	prefsSize;
	Int16	prefsVersion = noPreferenceFound;
	Boolean haveDefaultFont = false;
	UInt32 defaultFont = stdFont;

	ErrNonFatalDisplayIf(!prefsP, "null prefP arg");


	// Read the preferences / saved-state information.  Fix-up if no prefs or older/newer version
	prefsSize = sizeof (DatebookPreferenceType);
	prefsVersion = PrefGetAppPreferences (sysFileCDatebook, datebookPrefID, prefsP, &prefsSize, true);

	// If the preferences version is from a future release (as can happen when going back
	// and syncing to an older version of the device), treat it the same as "not found" because
	// it could be significantly different
	if ( prefsVersion > datebookPrefsVersionNum )
		prefsVersion = noPreferenceFound;

	if ( prefsVersion == noPreferenceFound )
		{
		// Version 1 and 2 preferences
		prefsP->dayStartHour = defaultDayStartHour;
		prefsP->dayEndHour = defaultDayEndHour;
		prefsP->alarmPreset.advance = defaultAlarmPresetAdvance;
		prefsP->alarmPreset.advanceUnit = defaultAlarmPresetUnit;
		prefsP->saveBackup = defaultSaveBackup;
		prefsP->showTimeBars = defaultShowTimeBars;
		prefsP->compressDayView = defaultCompressDayView;
		prefsP->showTimedAppts = defaultShowTimedAppts;
		prefsP->showUntimedAppts = defaultShowUntimedAppts;
		prefsP->showDailyRepeatingAppts = defaultShowDailyRepeatingAppts;

		// We need to set up the note font with a default value for the system.
		defaultFont = stdFont;
		if (FtrGet (sysFtrCreator, sysFtrDefaultFont, &defaultFont) != errNone ||
			defaultFont > largeBoldFont)
			defaultFont = stdFont;
		haveDefaultFont = true;

		prefsP->v20NoteFont = (FontID)defaultFont;
		}

	if ((prefsVersion == noPreferenceFound) || (prefsVersion < datebookPrefsVersionNum))
		{
		// Version 3 preferences
		prefsP->alarmSoundRepeatCount = defaultAlarmSoundRepeatCount;
		prefsP->alarmSoundRepeatInterval = defaultAlarmSoundRepeatInterval;
		prefsP->alarmSoundUniqueRecID = defaultAlarmSoundUniqueRecID;
		prefsP->noteFont = prefsP->v20NoteFont;	// 2.0 compatibility (BGT)

		// Fix up the note font if we copied from older preferences.
		if ((prefsVersion != noPreferenceFound) && (prefsP->noteFont == largeFont))
			prefsP->noteFont = largeBoldFont;

		if (!haveDefaultFont)
			{
			defaultFont = stdFont;
			if (FtrGet (sysFtrCreator, sysFtrDefaultFont, &defaultFont) != errNone ||
				defaultFont > largeBoldFont)
				defaultFont = stdFont;
			}

		prefsP->apptDescFont = (FontID)defaultFont;
		}

	if ((prefsVersion == noPreferenceFound) || (prefsVersion < datebookPrefsVersionNum))
		{
		// Version 4 preferences
		prefsP->alarmSnooze = defaultAlarmSnooze;
		}

	// Discard font IDs from incompatible or damaged preference records before
	// they reach FntSetFont/FldSetFont on an older ROM.
	if (prefsP->noteFont > largeBoldFont)
		prefsP->noteFont = stdFont;
	if (prefsP->apptDescFont > largeBoldFont)
		prefsP->apptDescFont = stdFont;

	return prefsVersion;
}


/***********************************************************************
 *
 * FUNCTION:    DatebookSavePrefs
 *
 * DESCRIPTION: Saves the current preferences of the application.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * CALLED:	from DatePref.c and Datebook.c
 *
 * REVISION HISTORY:
 *			Name		Date		Description
 *			----		----		-----------
 *			vmk		12/9/97	Initial version
 *			rbb		4/23/99	Added alarmSnooze
 *
 ***********************************************************************/
void DatebookSavePrefs (void)
{
	DatebookPreferenceType prefs;


	// Write the preferences / saved-state information.
	prefs.dayStartHour = DayStartHour;
	prefs.dayEndHour = DayEndHour;
	prefs.alarmPreset = AlarmPreset;
	prefs.saveBackup = SaveBackup;
	prefs.showTimeBars = ShowTimeBars;
	prefs.compressDayView = CompressDayView;
	prefs.showTimedAppts = ShowTimedAppts;
	prefs.showUntimedAppts = ShowUntimedAppts;
	prefs.showDailyRepeatingAppts = ShowDailyRepeatingAppts;
	prefs.alarmSoundRepeatCount = AlarmSoundRepeatCount;
	prefs.alarmSoundRepeatInterval = AlarmSoundRepeatInterval;
	prefs.alarmSoundUniqueRecID = AlarmSoundUniqueRecID;
	prefs.apptDescFont = ApptDescFont;
	prefs.noteFont = NoteFont;
	prefs.alarmSnooze = AlarmSnooze;

	// Clear reserved field so prefs don't look "different" just from stack garbage!
	prefs.reserved = 0;

	// Handle 2.0 backwards compatibility for fonts.	(BGT)
	prefs.v20NoteFont = stdFont;


	// Write the state information.
	PrefSetAppPreferences (sysFileCDatebook, datebookPrefID, datebookPrefsVersionNum,
		&prefs, sizeof (DatebookPreferenceType), true);
}


/***********************************************************************
 *
 * FUNCTION:    InitDatabase
 *
 * DESCRIPTION: This routine initializes the datebook database.
	*
 * PARAMETERS:	 datebase
 *
 * RETURNED:	 nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	10/19/95	Initial Revision
 *			grant	6/23/99	Set the backup bit.
 *
 ***********************************************************************/
static void InitDatabase (DmOpenRef dbP)
{
	ApptAppInfoInit (dbP);

	// Set the backup bit.  This is to aid syncs with non-Palm software.
	SetDBBackupBit(dbP);
}


/***********************************************************************
 *
 * FUNCTION:    SyncNotification
 *
 * DESCRIPTION: This routine is called when the datebook database is
 *              synchronized.  This routine will sort the database
 *              and schedule the next alarm.
 *
 * PARAMETERS:	 nothing
 *
 * RETURNED:	 nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/6/95	Initial Revision
 *
 ***********************************************************************/
static void SyncNotification (void)
{
	UInt16 mode;
	DmOpenRef dbP;

	// Find the application's data file.
	mode = dmModeReadWrite;
	dbP = DmOpenDatabaseByTypeCreator(datebookDBType, sysFileCDatebook, mode);
	if (!dbP) return;

	// Resort the appointment database.
	ApptSort (dbP);

	// Reschedule the next alarm.
	RescheduleAlarms (dbP);

	DmCloseDatabase (dbP);
}

/***********************************************************************
 *
 * FUNCTION:    SearchDraw
 *
 * DESCRIPTION: This routine draws the description, date, and time of
 *              an appointment found by the text search routine.
 *
 * PARAMETERS:	 apptRecP - pointer to an appointment record.
 *              x        - draw position
 *              y        - draw position
 *              width    - maximum width to draw.
 *
 * RETURNED:	 nothing
 *
 *	HISTORY:
 *		04/18/95	art	Created by Art Lamb
 *		08/03/99	kwk	Use the compatible truncation helper.
 *
 ***********************************************************************/

// DOLATER - these should be based on the screen width, not hard coded.

#define maxSearchRepeatDescWidth	100
#define maxSearchDateWidth			42
#define maxSearchTimeWidth			39

static void SearchDraw (ApptDBRecordPtr apptRecP, Int16 x, Int16 y,
	Int16 width)
{
	UInt16 i;
	Char timeStr [timeStringLength];
	Char dateStr [dateStringLength];
	UInt16 len;
	UInt16 maxDescWidth;
	Coord drawX;
	Char* ptr;
	Char* desc;
	Char* rscP;
	MemHandle rscH;
	SystemPreferencesType sysPrefs;


	if (apptRecP->repeat)
		maxDescWidth = maxSearchRepeatDescWidth;
	else
		maxDescWidth = width - maxSearchDateWidth - maxSearchTimeWidth;

	// Draw the appointment's desciption.
	desc = apptRecP->description;
	ptr = StrChr(desc, linefeedChr);
	len = (ptr == NULL ? StrLen(desc) : (UInt16)(ptr - desc));
	DateDrawTruncChars (desc, len, x, y, maxDescWidth);

	// If the event is repeating, draw the repeat type.
	if (apptRecP->repeat)
		{
		rscH = DmGetResource (strRsc, repeatTypesStrID);
		rscP = MemHandleLock (rscH);
		for (i = 0; i < apptRecP->repeat->repeatType; i++)
			rscP += StrLen(rscP) + 1;
		x += (width - FntCharsWidth (rscP, StrLen (rscP)));
		WinDrawChars (rscP, StrLen (rscP), x, y);
		MemHandleUnlock (rscH);
		}

	// Draw the appointment's date and time.
	else
		{
		// Get time and date formats for the system preference.
		PrefGetPreferences (&sysPrefs);

		if (TimeToInt (apptRecP->when->startTime) != apptNoTime)
			{
			TimeToAscii (apptRecP->when->startTime.hours,
				apptRecP->when->startTime.minutes, sysPrefs.timeFormat, timeStr);
			len = StrLen (timeStr);
			drawX = x + (width - FntCharsWidth (timeStr, len));
			WinDrawChars (timeStr, len, drawX, y);
			}

		DateToAscii (apptRecP->when->date.month,
				 		 apptRecP->when->date.day,
				 		 apptRecP->when->date.year + firstYear,
				 		 sysPrefs.dateFormat, dateStr);
		len = StrLen (dateStr);
		drawX = x + (width - FntCharsWidth (dateStr, len) - maxSearchTimeWidth);
		WinDrawChars (dateStr, len, drawX, y);
		}
}


/***********************************************************************
 *
 * FUNCTION:    Search
 *
 * DESCRIPTION: This routine searchs the datebook database for records
 *              containing the string passed.
 *
 * PARAMETERS:	 findParams - text search parameter block
 *
 * RETURNED:	 nothing
 *
 *	HISTORY:
 *		04/18/95	art	Created by Art Lamb.
 *		05/27/99	jaq	Two-pass process (future first)
 *		08/03/99	kwk	Use TxtFindString for source match length result.
 *		10/21/99	jmp	Made this routine a bit more like everyone else's.
 *
 ***********************************************************************/
static void Search (FindParamsPtr findParams)
{
	Err					err;
	UInt16 				cardNo = 0;
	UInt16				fieldNum;
	UInt16 				recordNum;
	Char* 				desc;
	Char* 				note;
	Char* 				header;
	Boolean 				done;
	Boolean 				match;
	MemHandle			headerStringH;
	LocalID 				dbID;
	DateType				date;
	MemHandle 			recordH;
	DmOpenRef 			dbP;
	RectangleType 		r;
	ApptDBRecordType	apptRec;
	DmSearchStateType searchInfo;
	UInt16				matchLength;
	UInt32				matchPos;

	// Find the application's data file.
	err =  DmGetNextDatabaseByTypeCreator (true, &searchInfo, datebookDBType,
						sysFileCDatebook, true, &cardNo, &dbID);
	if (err)
		{
		findParams->more = false;
		return;
		}

	// Open the appointment database.
	dbP = DmOpenDatabase(cardNo, dbID, findParams->dbAccesMode);
	if (!dbP)
		{
		findParams->more = false;
		return;
		}

	// Display the heading line.
	headerStringH = DmGetResource(strRsc, findDatebookHeaderStrID);
	header = MemHandleLock(headerStringH);
	done = FindDrawHeader(findParams, header);
   MemHandleUnlock(headerStringH);
   DmReleaseResource(headerStringH);
   if (done)
      goto Exit;

	// Search the description and note fields for the "find" string.
	recordNum = findParams->recordNum;
	while (true)
		{
		// Because applications can take a long time to finish a find when
		// the result may be on the screen or for other reasons, users like
		// to be able to stop the find.  Stop the find if an event is pending.
		// This stops if the user does something with the device.  Because
		// this call slows down the search we perform it every so many
		// records instead of every record.  The response time should still
		// be short without introducing much extra work to the search.

		// Note that in the implementation below, if the next 16th record is
		// secret the check doesn't happen.  Generally this shouldn't be a
		// problem since if most of the records are secret then the search
		// won't take long anyways!
		if ((recordNum & 0x000f) == 0 &&			// every 16th record
			EvtSysEventAvail(true))
			{
			// Stop the search process.
			findParams->more = true;
			break;
			}

		recordH = DmQueryNextInCategory (dbP, &recordNum, dmAllCategories);

		//Should we wrap around to repeating and past events?
		if (! recordH)
			{
			findParams->more = false;
			break;
			}

		ApptGetRecord (dbP, recordNum, &apptRec, &recordH);

		// Search the description field,  if a match is not found search the
		// note field.
		fieldNum = descSeacrchFieldNum;
		desc = apptRec.description;
		match = TxtGlueFindString (desc, findParams->strToFind, &matchPos, &matchLength);
		if (! match)
			{
			note = apptRec.note;
			if (note)
				{
				fieldNum = noteSeacrchFieldNum;
				match = TxtGlueFindString (note, findParams->strToFind, &matchPos, &matchLength);
				}
			}

		// If a match occurred in a repeating event,  make sure there is
		// a displayable occurrence of the event.
		if (match && apptRec.repeat)
			{
			date = apptRec.when->date;
			match = ApptNextRepeat (&apptRec, &date);
			}

		if (match)
			{
			// Add the match to the find paramter block,  if there is no room to
			// display the match the following function will return true.
			done = FindSaveMatch (findParams, recordNum, matchPos, fieldNum, matchLength, cardNo, dbID);
			if (done)
				{
				MemHandleUnlock (recordH);
				break;
				}

			// Get the bounds of the region where we will draw the results.
			FindGetLineBounds (findParams, &r);

			// Display the appointment info.
			SearchDraw (&apptRec, r.topLeft.x+1, r.topLeft.y, r.extent.x-2);

			findParams->lineNumber++;
			}

		MemHandleUnlock (recordH);
		recordNum++;
		}

Exit:
	DmCloseDatabase (dbP);
}


/***********************************************************************
 *
 * FUNCTION:    GoToItem
 *
 * DESCRIPTION: This routine is called as the result of hitting the
 *              "Go to" button in the text search dialog.
 *
 * PARAMETERS:	 goToParams   - where to go to.
 *              launchingApp - true if the application is being launched
 *
 * RETURNED:	 nothing
 *
 *	HISTORY:
 *		08/23/95	art	Created by Art Lamb.
 *		08/03/99	kwk	Use custom param to set up match length.
 *
 ***********************************************************************/
void GoToItem (GoToParamsPtr goToParams, Boolean launchingApp)
{
	UInt16 formID;
	UInt16 recordNum;
	EventType event;
	UInt32 uniqueID;

	recordNum = goToParams->recordNum;

	// If the application is already running, destroy the UI, this may
	// change the index of record we're going to.
	if (! launchingApp)
		{
		DmRecordInfo (ApptDB, recordNum, NULL, &uniqueID, NULL);
		FrmCloseAllForms ();
		ClearEditState ();
		DmFindRecordByID (ApptDB, uniqueID, &recordNum);
		}

	// Go to day view or note view.
	if (goToParams->matchFieldNum == noteSeacrchFieldNum)
		formID = NewNoteView;
	else
		formID = DayView;

	MemSet (&event, 0, sizeof(EventType));

	// Send an event to load the form we want to goto.
	event.eType = frmLoadEvent;
	event.data.frmLoad.formID = formID;
	EvtAddEventToQueue (&event);


	// Send an event to goto a form and select the matching text.
	event.eType = frmGotoEvent;
	event.data.frmGoto.formID = formID;
	event.data.frmGoto.recordNum = recordNum;
	event.data.frmGoto.matchPos = goToParams->matchPos;
	event.data.frmGoto.matchLen = goToParams->matchCustom;
	event.data.frmGoto.matchFieldNum = goToParams->matchFieldNum;
	EvtAddEventToQueue (&event);
}


/***********************************************************************
 *
 * FUNCTION:    DrawTime
 *
 * DESCRIPTION: Draws the given time. Used by the custom draw routines
 *					 for the date columns of the agenda and day views.
 *
 * PARAMETERS:  inTime		- the time
 *              inFormat	- time-to-ascii conversion format
 *					 inBoundsP	- drawing area
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	6/4/99	Initial Revision
 *
 ***********************************************************************/
void DrawTime (
	TimeType							inTime,
	TimeFormatType					inFormat,
	FontID							inFont,
	JustificationType				inJustification,
	RectangleType*					inBoundsP )
{
	FontID							curFont;
	char								timeStr [timeStringLength];
	TimeFormatType					format;
	UInt16							len;
	UInt16							width;
	Int16								x;

	// No-time appointment?
	if (TimeToInt(inTime) == apptNoTime)
		{
		// Show a centered diamond symbol, overriding the font params
		inFont = symbolFont;
		inJustification  = centerAlign;

		timeStr[0] = symbolNoTime;
		timeStr[1] = '\0';
		}
	else
		{
		if (inFormat == tfColonAMPM)
			format = tfColon;
		 else if (inFormat == tfDotAMPM)
		 	format = tfDot;
		 else
			format = inFormat;

		TimeToAscii (inTime.hours, inTime.minutes, format, timeStr);

		}

	// Use the string width and alignment to compute its starting point
	len = StrLen (timeStr);
	width = FntCharsWidth (timeStr, len);
	x = inBoundsP->topLeft.x;
	switch (inJustification)
		{
		case rightAlign:
			x += inBoundsP->extent.x - width;
			break;

		case centerAlign:
			x += (inBoundsP->extent.x - width) / 2;
			break;
		}

	x = max (inBoundsP->topLeft.x, x);

	// Draw the time
	curFont = FntSetFont (inFont);
	WinDrawChars (timeStr, len, x, inBoundsP->topLeft.y);
	FntSetFont (curFont);
}


/***********************************************************************
 *
 * FUNCTION:    GetObjectPtr
 *
 * DESCRIPTION: This routine returns a pointer to an object in the current
 *              form.
 *
 * PARAMETERS:  formId - id of the form to display
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	2/21/95	Initial Revision
 *
 ***********************************************************************/
void* GetObjectPtr (UInt16 objectID)
{
	FormPtr frm;

	frm = FrmGetActiveForm ();
	return (FrmGetObjectPtr (frm, FrmGetObjectIndex (frm, objectID)));

}


/***********************************************************************
 *
 * FUNCTION:    DirtyRecord
 *
 * DESCRIPTION: Mark a record dirty (modified).  Record marked dirty
 *              will be synchronized.
 *
 * PARAMETERS:  record index
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/15/95	Initial Revision
 *
 ***********************************************************************/
void DirtyRecord (DmOpenRef dbP, UInt16 index)
{
	UInt16		attr;
	UInt32		uniqueID;
	Err			err;

	err = DmRecordInfo (dbP, index, &attr, &uniqueID, NULL);
	ErrFatalDisplayIf (err, "Invalid index");

	attr |= dmRecAttrDirty;
	DmSetRecordInfo (dbP, index, &attr, NULL);
	CalendarSyncRecordModified (uniqueID);
}


/***********************************************************************
 *
 * FUNCTION:    ClearEditState
 *
 * DESCRIPTION: This routine take the application out of edit mode.
 *              The edit state of the current record is remember until
 *              this routine is called.
 *
 *              If the current record is empty its deleted by this
 *              routine.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    true is current record is deleted by this routine.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/28/95	Initial Revision
 *
 ***********************************************************************/
Boolean ClearEditState (void)
{
	UInt16 recordNum;
	Boolean empty;
	Boolean hasAlarm;
	MemHandle recordH;
	ApptDBRecordType apptRec;

	if ( ! ItemSelected)
		{
		CurrentRecord = noRecordSelected;
		return (false);
		}

	recordNum = CurrentRecord;

	// Clear the global variables that keep track of the edit state of the
	// current record.
	ItemSelected = false;
	CurrentRecord = noRecordSelected;
	DayEditPosition = 0;
	DayEditSelectionLength = 0;

	// If the description field is empty and the note field is empty, delete
	// the record.
	ApptGetRecord (ApptDB, recordNum, &apptRec, &recordH);
	empty = true;
	if (apptRec.description && *apptRec.description)
		empty = false;
	else if (apptRec.note && *apptRec.note)
		empty = false;

	hasAlarm = (apptRec.alarm != NULL);
	MemHandleUnlock (recordH);


	if (empty)
		{
		UInt32		alarmRef;	// Ignored. Needed for AlarmGetTrigger()

		// If the record was not modified, and the description and
		// note fields are empty, remove the record from the database.
		// This can occur when a new empty record is deleted.
		if (RecordDirty)
			{
			UInt32 uniqueID;
			if (DmRecordInfo (ApptDB, recordNum, NULL, &uniqueID, NULL) == errNone)
				CalendarSyncRecordModified (uniqueID);
			DmDeleteRecord (ApptDB, recordNum);
			DmMoveRecord (ApptDB, recordNum, DmNumRecords (ApptDB));
			}
		else
			DmRemoveRecord (ApptDB, recordNum);

		// If the appointment had the currently scheduled alarm, reschedule the next alarm
		if (hasAlarm)
			{
			UInt32 alarmTrigger = AlarmGetTrigger (&alarmRef);

			if (alarmTrigger && (alarmTrigger == ApptGetAlarmTime (&apptRec, TimGetSeconds ())) )
				{
				RescheduleAlarms (ApptDB);
				}
			}

		return (true);
		}

	return (false);
}


// Branch island to get to the glue code, linked in first.

Char* DateParamString(const Char* inTemplate, const Char* param0,
			const Char* param1, const Char* param2, const Char* param3)
{
	const Char* params[4];
	const Char* src;
	Char* result;
	Char* dst;
	UInt32 capacity;
	UInt16 i;

	/* TxtParamString dispatches through IntlDispatch, which is absent on
	 * some of the older ROMs this application must support. */
	params[0] = param0;
	params[1] = param1;
	params[2] = param2;
	params[3] = param3;
	capacity = StrLen(inTemplate) + 1;
	for (i = 0; i < 4; i++)
		if (params[i])
			capacity += StrLen(params[i]);

	result = (Char*)MemPtrNew(capacity);
	if (!result)
		return NULL;

	src = inTemplate;
	dst = result;
	while (*src)
		{
		if (src[0] == '^' && src[1] >= '0' && src[1] <= '3')
			{
			const Char* param = params[src[1] - '0'];
			if (param)
				{
				while (*param)
					*dst++ = *param++;
				}
			src += 2;
			}
		else
			*dst++ = *src++;
		}
	*dst = '\0';
	return result;
}


/* These helpers replace newer truncation traps missing from older ROMs. */
UInt16 DateTextWidthToFit (const Char* text, UInt16 length,
		Coord maxWidth, Int16* textWidthP)
{
	UInt16 offset = 0;
	UInt16 charBytes;
	Int16 candidateWidth;
	WChar character;

	if (textWidthP)
		*textWidthP = 0;
	if (maxWidth <= 0)
		return 0;

	while (offset < length)
		{
		charBytes = TxtGlueGetNextChar (text, offset, &character);
		if (charBytes == 0 || offset + charBytes > length)
			break;

		candidateWidth = FntCharsWidth (text, offset + charBytes);
		if (candidateWidth > maxWidth)
			break;

		offset += charBytes;
		if (textWidthP)
			*textWidthP = candidateWidth;
		}

	return offset;
}


void DateDrawTruncChars (const Char* text, UInt16 length,
		Coord x, Coord y, Coord maxWidth)
{
	static const Char ellipsis[] = "...";
	UInt16 charsToDraw;
	Int16 drawWidth = 0;
	Int16 ellipsisWidth;

	if (maxWidth <= 0 || length == 0)
		return;

	charsToDraw = DateTextWidthToFit (text, length, maxWidth, &drawWidth);
	if (charsToDraw >= length)
		{
		WinDrawChars (text, length, x, y);
		return;
		}

	ellipsisWidth = FntCharsWidth (ellipsis, 3);
	if (maxWidth <= ellipsisWidth)
		{
		charsToDraw = DateTextWidthToFit (text, length, maxWidth, &drawWidth);
		if (charsToDraw > 0)
			WinDrawChars (text, charsToDraw, x, y);
		return;
		}

	charsToDraw = DateTextWidthToFit (text, length,
			maxWidth - ellipsisWidth, &drawWidth);
	if (charsToDraw > 0)
		WinDrawChars (text, charsToDraw, x, y);
	WinDrawChars (ellipsis, 3, x + drawWidth, y);
}


#pragma mark ----------------
/***********************************************************************
 *
 * FUNCTION:    PilotMain
 *
 * DESCRIPTION: This is the main entry point for the Datebook
 *              application.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/12/95	Initial Revision
 *			vmk	10/22/97	Moved MIDI db creation to general panel
 *			rbb	4/22/99	Moved code to link w/ 16-bit jumps
 *			rbb	6/20/99	Removed multi-segment workaround
 *			jmp	10/18/99	Note that Datebook doesn't attempt to create an empty
 *								database if a default ("demo") database doesn't exist.
 *			rbb	11/12/99	Added recentFormFeature to pick default view on launch
 *
 ***********************************************************************/
UInt32	PilotMain (UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
	UInt16 error;
	UInt32 defaultForm;



	// The only place the following define is used is in the Jamfile for this application.
	// It's sole purpose is to provide a way to check if cross segment calls are being made
	// when application globals are unavailable.
#ifndef SINGLE_SEGMENT_CHECK
	Boolean launched;

	// Launch code sent by the launcher or the datebook button.
	if (cmd == sysAppLaunchCmdNormalLaunch)
		{
		error = StartApplication ();
		if (error) return (error);

		// Return to the day view.
		error = FtrGet (sysFileCDatebook, recentFormFeature, &defaultForm);
		if (error || defaultForm != DayView)
			defaultForm = DayView;

		FrmGotoForm (defaultForm);
		EventLoop ();
		StopApplication ();
		}


	// This action code might be sent to the app when it's already running
	// if the use hits the "Go To" button in the Find Results dialog box.
	else if (cmd == sysAppLaunchCmdGoTo)
		{
		launched = launchFlags & sysAppLaunchFlagNewGlobals;
		if (launched)
			{
			error = StartApplication ();
			if (error) return (error);

			GoToItem ((GoToParamsPtr) cmdPBP, launched);

			EventLoop ();
			StopApplication ();
			}
		else
			GoToItem ((GoToParamsPtr) cmdPBP, launched);
		}
#else
	if (false) {}
#endif

	//////////////////////////////////////////////////////////////////////////////////
	//
	// WARNING! Launch codes below this point can be called from a context where
	// 			application globals are unavailable.
	//
	//////////////////////////////////////////////////////////////////////////////////

	// Launch code sent by text search.
	else if (cmd == sysAppLaunchCmdFind)
		{
		Search ((FindParamsPtr)cmdPBP);
		}


	// Launch code sent by sync application to notify the datebook
	// application that its database was been synced.
	else if (cmd == sysAppLaunchCmdSyncNotify)
		{
		SyncNotification ();
		}


	// Launch code sent by Alarm Manager to notify the datebook
	// application that an alarm has triggered.
	else if (cmd == sysAppLaunchCmdAlarmTriggered)
		{
		AlarmTriggered ((SysAlarmTriggeredParamType *)cmdPBP);
		}


	// Launch code sent by Alarm Manager to notify the datebook
	// application that is should display its alarm dialog.
	else if (cmd == sysAppLaunchCmdDisplayAlarm)
		{
		DisplayAlarm ();
		}


	// Launch code sent when the system time is changed.
	else if (cmd == sysAppLaunchCmdTimeChange)
		{
		AlarmReset (false);
		}


	// This action code is sent after the system is reset.  We use this time
	// to create our default database.  Note:  Unlike several other built-in
	// apps, we do not attempt to create an empty database if the default
	// database image doesn't exist.
	else if (cmd == sysAppLaunchCmdSystemReset)
		{
		if (((SysAppLaunchCmdSystemResetType*)cmdPBP)->createDefaultDB)
			{
			MemHandle resH;

			resH = DmGet1Resource(sysResTDefaultDB, sysResIDDefaultDB);
			if (resH)
				{
				DmCreateDatabaseFromImage(MemHandleLock(resH));
				MemHandleUnlock(resH);
				DmReleaseResource(resH);

				// set the backup bit on the new database
				SetDBBackupBit(NULL);
				}

			}

		if (! ((SysAppLaunchCmdSystemResetType*)cmdPBP)->hardReset)
			{
			AlarmInit ();				// Init alarm internals
			AlarmReset (false);		// Find and install next upcoming alarm
			}
		}


	// This action code is sent by the DesktopLink server when it create
	// a new database.  We will initializes the new database.
	else if (cmd == sysAppLaunchCmdInitDatabase)
		{
		InitDatabase (((SysAppLaunchCmdInitDatabaseType*)cmdPBP)->dbP);
		}

	return (0);
}


#pragma mark ----------------
/***********************************************************************
 *
 * FUNCTION:    TimeToWait
 *
 * DESCRIPTION:	This routine calculates the number of ticks until the
 *						time display should be hidden.  If the time is not being
 *						displayed, we return evtWaitForever - a good default for
 *						EvtGetEvent.
 * PARAMETERS:  none
 *
 * RETURNED:    number of ticks to wait
 *						== evtWaitForever if we are not showing the time
 *						== 0 if time is up
 *
 *	NOTES:		Uses the global variables TimeDisplayed and TimeDisplayTick.
 *					TimeDisplayed is true when some temporary information is on
 *					display.  In that case, TimeDisplayTick is the time when the
 *					temporary display should go away.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			grant	2/2/99	Initial Revision
 *	 *
 ***********************************************************************/
Int32 TimeToWait()
{
	Int32 timeRemaining;

	if (!TimeDisplayed)
		{
		return evtWaitForever;
		}

	timeRemaining = TimeDisplayTick - TimGetTicks();
	if (timeRemaining < 0)
		{
		timeRemaining = 0;
		}

	return timeRemaining;
}


/***********************************************************************
 *
 * FUNCTION:    ApplicationHandleEvent
 *
 * DESCRIPTION: This routine loads form resources and set the event
 *              handler for the form loaded.
 *
 * PARAMETERS:  event  - a pointer to an EventType structure
 *
 * RETURNED:    true if the event has handle and should not be passed
 *              to a higher level handler.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	9/11/95	Initial Revision
 *			jmp	9/17/99	Use NewNoteVie instead of NoteView.
 *			rbb	11/12/99	Added recentFormFeature to pick default view on launch
 *
 ***********************************************************************/
static Boolean ApplicationHandleEvent (EventType* event)
{
	UInt16 formId;
	FormPtr frm;

	if (event->eType == frmLoadEvent)
		{
		// Load the form resource.
		formId = event->data.frmLoad.formID;
		frm = FrmInitForm (formId);
		if (frm == NULL)
			{
			EventType stopEvent;
			MemSet (&stopEvent, sizeof (stopEvent), 0);
			stopEvent.eType = appStopEvent;
			EvtAddEventToQueue (&stopEvent);
			return (true);
			}
		FrmSetActiveForm (frm);

		// Set the event handler for the form.  The handler of the currently
		// active form is called by FrmDispatchEvent each time is receives an
		// event.
		if (formId == DayView)
			{
			FrmSetEventHandler (frm, DayViewHandleEvent);
			}

		else if (formId == WeekView)
			FrmSetEventHandler (frm, WeekViewHandleEvent);

		else if (formId == MonthView)
			FrmSetEventHandler (frm, MonthViewHandleEvent);

		else if (formId == NewNoteView)
			FrmSetEventHandler (frm, NoteViewHandleEvent);

		else if (formId == DetailsDialog)
			FrmSetEventHandler (frm, DetailsHandleEvent);

		else if (formId == RepeatDialog)
			FrmSetEventHandler (frm, RepeatHandleEvent);

		else if (formId == PreferencesDialog)
			FrmSetEventHandler (frm, PreferencesHandleEvent);

		else if (formId == DisplayDialog)
			FrmSetEventHandler (frm, DisplayOptionsHandleEvent);

		FtrSet (sysFileCDatebook, recentFormFeature, DayView);

		return (true);
		}

	return (false);
}


/***********************************************************************
 *
 * FUNCTION:    PreprocessEvent
 *
 * DESCRIPTION: This routine handles special event processing that
 *              needs to occur before the System Event Handler get
 *              and event.
 *
 * PARAMETERS:  event  - a pointer to an EventType structure
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	1/10/97	Initial Revision
 *			CSS	06/22/99	Standardized keyDownEvent handling
 *								(TxtCharIsHardKey, commandKeyMask, etc.)
 *
 ***********************************************************************/
static void PreprocessEvent (EventType* event)
{

	// We want to allow the datebook event handled see the command keys
	// before the any other event handler get them.  Some of the databook
	// views display UI that dispappears automaticial (the time in the
	// title, or an event's descripition in the week view).  This UI
	// needs to be dismissed before the System Event Handler or the
	// Menu Event Handler displays any UI objects.
	if (event->eType == keyDownEvent)
		{
		if	(	(!TxtCharIsHardKey(	event->data.keyDown.modifiers,
											event->data.keyDown.chr))
			&&	(EvtKeydownIsVirtual(event))
			&&	(event->data.keyDown.chr != vchrPageUp)
			&&	(event->data.keyDown.chr != vchrPageDown)
			&&	(event->data.keyDown.chr != vchrSendData))
			{
			FrmDispatchEvent (event);
			}
		}
}


/***********************************************************************
 *
 * FUNCTION:    EventLoop
 *
 * DESCRIPTION: This routine is the event loop for the Datebook
 *              aplication.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/12/95	Initial Revision
 *			grant 2/2/99	Use TimeToWait() for time to wait for next event
 *
 ***********************************************************************/
static void EventLoop (void)
{
	UInt16 error;
	EventType event;

	do
		{
		EvtGetEvent (&event, TimeToWait());

		PreprocessEvent (&event);

		if (! SysHandleEvent (&event))
			if (! MenuHandleEvent (NULL, &event, &error))
				if (! ApplicationHandleEvent (&event))
					FrmDispatchEvent (&event);

//		#if	EMULATION_LEVEL != EMULATION_NONE
//		ECApptDBValidate (ApptDB);
//		#endif
		}
	while (event.eType != appStopEvent);
}
