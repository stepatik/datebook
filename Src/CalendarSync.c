#include <PalmOS.h>
#include <SerialMgrOld.h>
#include "Datebook.h"
#include "CalendarSync.h"

#define syncMapType       'GMAP'
#define syncMapName       "DatebookGoogleMapDB"
#define syncLineSize      1536
#define syncGoogleIDSize  160
#define syncPastDays      30
#define syncFutureDays    180
#define syncFieldSize     320
#define syncMaxExceptions 16

typedef struct {
	UInt32 palmID;
	UInt32 modified;
	Char googleID[syncGoogleIDSize];
} SyncMapRecordType;

typedef struct {
	UInt16 numExceptions;
	DateType dates[syncMaxExceptions];
} SyncExceptionListType;

static UInt16 syncSerialRef = sysInvalidRefNum;
static DmOpenRef syncMapDB = NULL;
static Char *syncLine;
static Char (*syncFields)[syncFieldSize];
static Char *syncTxLine;
static UInt16 syncPendingLength = 0;
static UInt16 syncReceivedCount = 0;
static UInt16 syncAppliedCount = 0;
static UInt16 syncSkippedCount = 0;
static UInt16 syncStage = 0;
static UInt16 syncEventIndex = 0;
static UInt16 syncTotalEvents = 0;
static UInt16 syncLastLineLength = 0;
static Boolean syncApplyingDownload = false;
static void SyncUInt32ToA (Char *text, UInt32 value);
static Err SyncSend (const Char *text);

static __attribute__((section("code3"))) void SyncShowProgress (const Char *phase)
	{
	RectangleType bounds;
	Char line[40];
	Char packet[128];
	Int16 y = 30;

	RctSetRectangle (&bounds, 0, 20, 160, 104);
	WinEraseRectangle (&bounds, 0);
	WinDrawChars ("SYNC DEBUG", 10, 8, y);
	y += 16;
	StrCopy (line, "Event ");
	SyncUInt32ToA (line + StrLen (line), syncEventIndex);
	StrCat (line, "/");
	SyncUInt32ToA (line + StrLen (line), syncTotalEvents);
	WinDrawChars (line, StrLen (line), 8, y);
	y += 16;
	StrCopy (line, "Step: ");
	StrCat (line, phase);
	WinDrawChars (line, StrLen (line), 8, y);
	y += 16;
	StrCopy (line, "Line bytes: ");
	SyncUInt32ToA (line + StrLen (line), syncLastLineLength);
	WinDrawChars (line, StrLen (line), 8, y);
	y += 16;
	StrCopy (line, "Applied: ");
	SyncUInt32ToA (line + StrLen (line), syncAppliedCount);
	StrCat (line, "  Skip: ");
	SyncUInt32ToA (line + StrLen (line), syncSkippedCount);
	WinDrawChars (line, StrLen (line), 8, y);

	/* The Palm's fatal-exception screen hides the display and a hard reset
	 * erases RAM. Mirror each checkpoint to the PC over the already-open link. */
	StrCopy (packet, "SYNC_DEBUG|");
	SyncUInt32ToA (packet + StrLen (packet), syncEventIndex);
	StrCat (packet, "|");
	SyncUInt32ToA (packet + StrLen (packet), syncTotalEvents);
	StrCat (packet, "|");
	StrCat (packet, phase);
	StrCat (packet, "|");
	SyncUInt32ToA (packet + StrLen (packet), syncLastLineLength);
	StrCat (packet, "|");
	SyncUInt32ToA (packet + StrLen (packet), syncReceivedCount);
	StrCat (packet, "|");
	SyncUInt32ToA (packet + StrLen (packet), syncAppliedCount);
	StrCat (packet, "|");
	SyncUInt32ToA (packet + StrLen (packet), syncSkippedCount);
	StrCat (packet, "\n");
	if (syncSerialRef != sysInvalidRefNum && SyncSend (packet) == errNone)
		SerSendWait (syncSerialRef, SysTicksPerSecond () / 10);
	}

void CalendarSyncGetStats (UInt16 *received, UInt16 *applied, UInt16 *skipped,
		UInt16 *stage, UInt16 *eventIndex, UInt16 *lineLength)
{
	if (received) *received = syncReceivedCount;
	if (applied) *applied = syncAppliedCount;
	if (skipped) *skipped = syncSkippedCount;
	if (stage) *stage = syncStage;
	if (eventIndex) *eventIndex = syncEventIndex;
	if (lineLength) *lineLength = syncLastLineLength;
}

static void SyncUInt32ToA (Char *text, UInt32 value)
{
	Char reverse[11];
	UInt16 count = 0;
	UInt16 i;
	if (value == 0) reverse[count++] = '0';
	while (value)
		{
		reverse[count++] = (Char) ('0' + (value % 10));
		value /= 10;
		}
	for (i = 0; i < count; i++) text[i] = reverse[count - i - 1];
	text[count] = '\0';
}

static UInt32 SyncAToUInt32 (const Char *text)
{
	UInt32 value = 0;
	while (*text >= '0' && *text <= '9')
		{
		value = value * 10 + (*text - '0');
		text++;
		}
	return value;
}

static Err SyncSend (const Char *text)
{
	Err err = errNone;
	UInt32 length = StrLen (text);
	UInt32 sent = 0;
	UInt32 count;

	while (sent < length)
		{
		count = SerSend (syncSerialRef, (void *) (text + sent), length - sent, &err);
		if (err != errNone || count == 0) return (err ? err : dmErrInvalidParam);
		sent += count;
		}
	return errNone;
}

static __attribute__((section("code3"))) Err SyncReadLine (Char *line, UInt16 capacity, UInt32 timeoutTicks)
{
	UInt16 length = 0;
	UInt16 i;
	UInt32 waiting;
	UInt32 requested;
	UInt32 received;
	Err err;
	Char ch;
	syncLastLineLength = 0;

	while (true)
		{
		/* Consume bytes left over when a serial read contained multiple lines. */
		for (i = 0; i < syncPendingLength; i++)
			{
			ch = syncTxLine[i];
			if (ch == '\n')
				{
				MemMove (syncTxLine, syncTxLine + i + 1,
						syncPendingLength - i - 1);
				syncPendingLength -= i + 1;
			line[length] = '\0';
			syncLastLineLength = length;
			return (length + 1 == capacity ? dmErrInvalidParam : errNone);
			}
			if (ch != '\r')
				{
				if (length + 1 >= capacity) return dmErrInvalidParam;
				line[length++] = ch;
				syncLastLineLength = length;
				}
			}
		syncPendingLength = 0;

		err = SerReceiveWait (syncSerialRef, 1, timeoutTicks);
		if (err != errNone) return err;
		SerReceiveCheck (syncSerialRef, &waiting);
		if (waiting == 0) continue;
		requested = waiting;
		if (requested >= syncLineSize) requested = syncLineSize - 1;
		received = SerReceive (syncSerialRef, syncTxLine, requested, timeoutTicks, &err);
		if (err != errNone) return err;
		if (received == 0) return dmErrInvalidParam;
		for (i = 0; i < received; i++)
			{
			ch = syncTxLine[i];
			if (ch == '\n')
				{
			if (i + 1 < received)
				{
				syncPendingLength = received - i - 1;
				MemMove (syncTxLine, syncTxLine + i + 1, syncPendingLength);
				}
			line[length] = '\0';
			syncLastLineLength = length;
			return (length + 1 == capacity ? dmErrInvalidParam : errNone);
			}
			if (ch != '\r')
				{
				if (length + 1 >= capacity) return dmErrInvalidParam;
				line[length++] = ch;
				syncLastLineLength = length;
				}
			}
		}
}

static Int16 SyncHex (Char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

static void SyncDecode (Char *text)
{
	Char *src = text;
	Char *dst = text;
	Int16 high, low;

	while (*src)
		{
		if (*src == '%' && (high = SyncHex (src[1])) >= 0 &&
				(low = SyncHex (src[2])) >= 0)
			{
			*dst++ = (Char) ((high << 4) | low);
			src += 3;
			}
		else
			*dst++ = *src++;
		}
	*dst = '\0';
}

static Boolean SyncSplit (Char *line, UInt16 expected)
{
	UInt16 count = 0;
	Char *p = line;

	while (count < expected)
		{
		Char *separator;
		if (!p) return false;
		separator = StrChr (p, '|');
		if (separator) *separator = '\0';
		StrNCopy (syncFields[count], p, syncFieldSize - 1);
		syncFields[count][syncFieldSize - 1] = '\0';
		SyncDecode (syncFields[count]);
		count++;
		p = separator ? separator + 1 : NULL;
		}
	return (p == NULL);
}

static DmOpenRef SyncOpenMapDB (void)
{
	DmOpenRef db;
	Err err;

	db = DmOpenDatabaseByTypeCreator (syncMapType, sysFileCDatebook, dmModeReadWrite);
	if (db) return db;
	err = DmCreateDatabase (0, syncMapName, sysFileCDatebook, syncMapType, false);
	if (err != errNone && err != dmErrAlreadyExists) return NULL;
	return DmOpenDatabaseByTypeCreator (syncMapType, sysFileCDatebook, dmModeReadWrite);
}

static Boolean SyncMapGet (UInt32 palmID, Char *googleID, UInt32 *modified)
{
	UInt16 i, count = DmNumRecords (syncMapDB);
	MemHandle recordH;
	SyncMapRecordType *recordP;

	for (i = 0; i < count; i++)
		{
		if (syncApplyingDownload) SyncShowProgress ("map search");
		recordH = DmQueryRecord (syncMapDB, i);
		if (!recordH) continue;
		recordP = MemHandleLock (recordH);
		if (recordP->palmID == palmID)
			{
			StrCopy (googleID, recordP->googleID);
			if (modified) *modified = recordP->modified;
			MemHandleUnlock (recordH);
			return true;
			}
		MemHandleUnlock (recordH);
		}
	googleID[0] = '\0';
	if (modified) *modified = 0;
	return false;
}

static Boolean SyncMapFindPalmID (const Char *googleID, UInt32 *palmID)
{
	UInt16 i, count = DmNumRecords (syncMapDB);
	MemHandle recordH;
	SyncMapRecordType *recordP;

	for (i = 0; i < count; i++)
		{
		recordH = DmQueryRecord (syncMapDB, i);
		if (!recordH) continue;
		recordP = MemHandleLock (recordH);
		if (StrCompare (recordP->googleID, googleID) == 0)
			{
			*palmID = recordP->palmID;
			MemHandleUnlock (recordH);
			return true;
			}
		MemHandleUnlock (recordH);
		}
	return false;
}

static Err SyncMapPut (UInt32 palmID, const Char *googleID, UInt32 modified)
{
	UInt16 i, index, count = DmNumRecords (syncMapDB);
	MemHandle recordH;
	SyncMapRecordType *recordP;
	SyncMapRecordType mapRecord;
	Err err;

	for (i = 0; i < count; i++)
		{
		if (syncApplyingDownload) SyncShowProgress ("map search");
		recordH = DmQueryRecord (syncMapDB, i);
		if (!recordH) continue;
		recordP = MemHandleLock (recordH);
		if (recordP->palmID == palmID)
			{
			MemMove (&mapRecord, recordP, sizeof (mapRecord));
			MemHandleUnlock (recordH);
			if (syncApplyingDownload) SyncShowProgress ("map get record");
			recordH = DmGetRecord (syncMapDB, i);
			if (!recordH) return DmGetLastErr ();
			recordP = MemHandleLock (recordH);
			mapRecord.palmID = palmID;
			if (googleID && *googleID)
				{
				StrNCopy (mapRecord.googleID, googleID, syncGoogleIDSize - 1);
				mapRecord.googleID[syncGoogleIDSize - 1] = '\0';
				}
			if (modified) mapRecord.modified = modified;
			err = DmWrite (recordP, 0, &mapRecord, sizeof (mapRecord));
			MemHandleUnlock (recordH);
			if (syncApplyingDownload) SyncShowProgress ("map release update");
			DmReleaseRecord (syncMapDB, i, err == errNone);
			return err;
			}
		MemHandleUnlock (recordH);
		}

	index = count;
	if (syncApplyingDownload) SyncShowProgress ("map create record");
	recordH = DmNewRecord (syncMapDB, &index, sizeof (SyncMapRecordType));
	if (!recordH) return memErrNotEnoughSpace;
	if (syncApplyingDownload) SyncShowProgress ("map lock new");
	recordP = MemHandleLock (recordH);
	if (syncApplyingDownload) SyncShowProgress ("map handle locked");
	MemSet (&mapRecord, sizeof (mapRecord), 0);
	if (syncApplyingDownload) SyncShowProgress ("map record cleared");
	mapRecord.palmID = palmID;
	if (syncApplyingDownload) SyncShowProgress ("map ID written");
	if (googleID)
		StrNCopy (mapRecord.googleID, googleID, syncGoogleIDSize - 1);
	mapRecord.googleID[syncGoogleIDSize - 1] = '\0';
	if (syncApplyingDownload) SyncShowProgress ("map Google ID written");
	mapRecord.modified = modified;
	if (syncApplyingDownload) SyncShowProgress ("map timestamp written");
	if (syncApplyingDownload) SyncShowProgress ("map write data");
	err = DmWrite (recordP, 0, &mapRecord, sizeof (mapRecord));
	MemHandleUnlock (recordH);
	if (syncApplyingDownload) SyncShowProgress ("map unlocked");
	if (syncApplyingDownload) SyncShowProgress ("map database release");
	DmReleaseRecord (syncMapDB, index, err == errNone);
	return err;
}

__attribute__((section("code4"))) void CalendarSyncRecordModified (UInt32 palmID)
{
	DmOpenRef oldDB = syncMapDB;
	/* ApptNewRecord/ApptChangeRecord invoke this hook too. Server-originated
	 * writes must not create a temporary local-dirty map entry. */
	if (syncApplyingDownload) return;
	if (!syncMapDB) syncMapDB = SyncOpenMapDB ();
	if (syncMapDB)
		SyncMapPut (palmID, "", TimGetSeconds ());
	if (!oldDB && syncMapDB)
		{
		DmCloseDatabase (syncMapDB);
		syncMapDB = NULL;
		}
}

static UInt32 SyncDateTimeToSeconds (DateType date, UInt16 hour, UInt16 minute)
{
	DateTimeType dateTime;
	MemSet (&dateTime, sizeof (dateTime), 0);
	dateTime.year = date.year + firstYear;
	dateTime.month = date.month;
	dateTime.day = date.day;
	dateTime.hour = hour;
	dateTime.minute = minute;
	return TimDateTimeToSeconds (&dateTime);
}

static UInt32 SyncRecordStart (ApptDBRecordPtr apptP)
{
	UInt16 time = (UInt16) TimeToInt (apptP->when->startTime);
	if ((Int16) time == apptNoTime) time = 0;
	return SyncDateTimeToSeconds (apptP->when->date, time >> 8, time & 0xff);
}

static void SyncAppendEscaped (Char *out, UInt16 capacity, UInt16 *length, const Char *value)
{
	static const Char hex[] = "0123456789ABCDEF";
	const UInt8 *p = (const UInt8 *) value;
	UInt16 needed;

	while (*p && *length + 1 < capacity)
		{
		if ((*p >= 0x20 && *p != 0x7f && *p != '%' && *p != '|'))
			{
			out[(*length)++] = *p++;
			}
		else
			{
			needed = 3;
			if (*length + needed >= capacity) break;
			out[(*length)++] = '%';
			out[(*length)++] = hex[*p >> 4];
			out[(*length)++] = hex[*p & 0x0f];
			p++;
			}
		}
	out[*length] = '\0';
}

static void SyncAppendNumber (Char *out, UInt16 capacity, UInt16 *length, UInt32 number)
{
	Char value[16];
	SyncUInt32ToA (value, number);
	if (*length + 1 < capacity) out[(*length)++] = '|';
	SyncAppendEscaped (out, capacity, length, value);
}

static void SyncAppendField (Char *out, UInt16 capacity, UInt16 *length,
		const Char *value, UInt16 maxLength)
{
	Char clipped[256];
	UInt16 copyLength = StrLen (value);
	if (copyLength > maxLength) copyLength = maxLength;
	if (copyLength >= sizeof (clipped)) copyLength = sizeof (clipped) - 1;
	MemMove (clipped, value, copyLength);
	clipped[copyLength] = '\0';
	if (*length + 1 < capacity) out[(*length)++] = '|';
	SyncAppendEscaped (out, capacity, length, clipped);
}

static void SyncRuleAppendDate (Char *rule, DateType date)
{
	UInt16 year = date.year + firstYear;
	UInt16 length = StrLen (rule);
	rule[length++] = (Char) ('0' + (year / 1000) % 10);
	rule[length++] = (Char) ('0' + (year / 100) % 10);
	rule[length++] = (Char) ('0' + (year / 10) % 10);
	rule[length++] = (Char) ('0' + year % 10);
	rule[length++] = (Char) ('0' + date.month / 10);
	rule[length++] = (Char) ('0' + date.month % 10);
	rule[length++] = (Char) ('0' + date.day / 10);
	rule[length++] = (Char) ('0' + date.day % 10);
	rule[length] = '\0';
}

static void SyncBuildRule (ApptDBRecordPtr apptP, Char *rule, UInt16 capacity)
{
	UInt16 length = 0;
	UInt16 i;
	Boolean firstDay = true;
	static const Char *days[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
	RepeatInfoPtr repeatP = apptP->repeat;
	
	rule[0] = '\0';
	if (!repeatP) return;
	StrCopy (rule, "FREQ=");
	switch (repeatP->repeatType)
		{
		case repeatDaily: StrCat (rule, "DAILY"); break;
		case repeatWeekly: StrCat (rule, "WEEKLY"); break;
		case repeatMonthlyByDay: StrCat (rule, "MONTHLY"); break;
		case repeatMonthlyByDate: StrCat (rule, "MONTHLY"); break;
		case repeatYearly: StrCat (rule, "YEARLY"); break;
		default: rule[0] = '\0'; return;
		}
	if (repeatP->repeatFrequency > 1)
		{
		StrCat (rule, ";INTERVAL=");
		StrIToA (rule + StrLen (rule), repeatP->repeatFrequency);
		}
	if (repeatP->repeatType == repeatWeekly)
		{
		StrCat (rule, ";BYDAY=");
		for (i = 0; i < daysInWeek; i++)
			if (repeatP->repeatOn & (1 << i))
				{
				if (!firstDay) StrCat (rule, ",");
				StrCat (rule, days[i]);
				firstDay = false;
				}
		}
	else if (repeatP->repeatType == repeatMonthlyByDate)
		{
		StrCat (rule, ";BYMONTHDAY=");
		StrIToA (rule + StrLen (rule), apptP->when->date.day);
		}
	else if (repeatP->repeatType == repeatMonthlyByDay)
		{
		Int16 ordinal = (apptP->when->date.day - 1) / 7 + 1;
		if (apptP->when->date.day + 7 > DaysInMonth (apptP->when->date.month,
				apptP->when->date.year + firstYear)) ordinal = -1;
		StrCat (rule, ";BYDAY=");
		StrIToA (rule + StrLen (rule), ordinal);
		StrCat (rule, days[DayOfWeek (apptP->when->date.month, apptP->when->date.day,
				apptP->when->date.year + firstYear)]);
		}
	if (DateToInt (repeatP->repeatEndDate) != apptNoEndDate)
		{
		StrCat (rule, ";UNTIL=");
		SyncRuleAppendDate (rule, repeatP->repeatEndDate);
		StrCat (rule, "T235959Z");
		}
	if (apptP->exceptions && apptP->exceptions->numExceptions)
		{
		ExceptionsListPtr exceptionsP = apptP->exceptions;
		StrCat (rule, ";EXDATE=");
		for (i = 0; i < exceptionsP->numExceptions && i < syncMaxExceptions; i++)
			{
			if (i) StrCat (rule, ",");
			SyncRuleAppendDate (rule, *(&exceptionsP->exception + i));
			}
		}
	length = StrLen (rule);
	if (length >= capacity) rule[capacity - 1] = '\0';
}

static Err SyncSendDirtyRecords (UInt32 windowStart, UInt32 windowEnd)
{
	UInt16 i, count = DmNumRecords (ApptDB);
	UInt16 attr;
UInt32 palmID, start, end, modified, lastModified;
	MemHandle recordH;
	ApptDBRecordType appt;
	Char googleID[syncGoogleIDSize];
	Char rule[300];
	UInt16 length;
	Boolean deleted;
	Err err;

	for (i = 0; i < count; i++)
		{
		if (DmRecordInfo (ApptDB, i, &attr, &palmID, NULL) != errNone ||
				!(attr & dmRecAttrDirty)) continue;
		deleted = (attr & dmRecAttrDelete) != 0;
		if (deleted)
			{
			SyncMapGet (palmID, googleID, &lastModified);
			modified = lastModified ? lastModified : TimGetSeconds ();
			syncTxLine[0] = '\0';
			length = 0;
			SyncAppendEscaped (syncTxLine, syncLineSize, &length, "P");
			SyncAppendNumber (syncTxLine, syncLineSize, &length, palmID);
			SyncAppendField (syncTxLine, syncLineSize, &length, googleID, syncGoogleIDSize - 1);
			SyncAppendNumber (syncTxLine, syncLineSize, &length, modified);
			SyncAppendNumber (syncTxLine, syncLineSize, &length, windowStart);
			SyncAppendNumber (syncTxLine, syncLineSize, &length, windowStart);
			SyncAppendNumber (syncTxLine, syncLineSize, &length, 0);
			SyncAppendField (syncTxLine, syncLineSize, &length, "", 0);
			SyncAppendField (syncTxLine, syncLineSize, &length, "", 0);
			SyncAppendField (syncTxLine, syncLineSize, &length, "", 0);
			SyncAppendField (syncTxLine, syncLineSize, &length, "", 0);
			SyncAppendNumber (syncTxLine, syncLineSize, &length, 1);
			if (length + 1 >= syncLineSize) return dmErrInvalidParam;
			syncTxLine[length++] = '\n';
			syncTxLine[length] = '\0';
			err = SyncSend (syncTxLine);
			if (err != errNone) return err;
			continue;
			}
		if (ApptGetRecord (ApptDB, i, &appt, &recordH) != errNone) continue;
		start = SyncRecordStart (&appt);
		if (start >= windowEnd ||
				(start < windowStart && (!appt.repeat ||
				 (DateToInt (appt.repeat->repeatEndDate) != apptNoEndDate &&
				  SyncDateTimeToSeconds (appt.repeat->repeatEndDate, 23, 59) < windowStart))))
			{
			MemHandleUnlock (recordH);
			continue;
			}
		end = start;
		if ((Int16) TimeToInt (appt.when->endTime) != apptNoTime)
			{
			UInt16 t = (UInt16) TimeToInt (appt.when->endTime);
			end = SyncDateTimeToSeconds (appt.when->date, t >> 8, t & 0xff);
			}
		modified = TimGetSeconds ();
		SyncMapGet (palmID, googleID, &lastModified);
		if (lastModified) modified = lastModified;
		SyncBuildRule (&appt, rule, sizeof (rule));
		syncTxLine[0] = '\0';
		length = 0;
		SyncAppendEscaped (syncTxLine, syncLineSize, &length, "P");
		SyncAppendNumber (syncTxLine, syncLineSize, &length, palmID);
		SyncAppendField (syncTxLine, syncLineSize, &length, googleID, syncGoogleIDSize - 1);
		SyncAppendNumber (syncTxLine, syncLineSize, &length, modified);
		SyncAppendNumber (syncTxLine, syncLineSize, &length, start);
		SyncAppendNumber (syncTxLine, syncLineSize, &length, end);
		SyncAppendNumber (syncTxLine, syncLineSize, &length,
				((Int16) TimeToInt (appt.when->startTime) == apptNoTime) ? 1 : 0);
		SyncAppendField (syncTxLine, syncLineSize, &length,
				appt.description ? appt.description : "", maxDescSize - 1);
		SyncAppendField (syncTxLine, syncLineSize, &length,
				appt.note ? appt.note : "", maxDescSize - 1);
		SyncAppendField (syncTxLine, syncLineSize, &length, "", 0);
		SyncAppendField (syncTxLine, syncLineSize, &length, rule, sizeof (rule) - 1);
		SyncAppendNumber (syncTxLine, syncLineSize, &length, deleted ? 1 : 0);
		if (length + 1 >= syncLineSize)
			{
			MemHandleUnlock (recordH);
			return dmErrInvalidParam;
			}
		syncTxLine[length++] = '\n';
		syncTxLine[length] = '\0';
		MemHandleUnlock (recordH);
		err = SyncSend (syncTxLine);
		if (err != errNone) return err;
		}
	return SyncSend ("END_UPLOAD\n");
}

static Boolean SyncParseRule (const Char *rule, RepeatInfoType *repeat, DateType startDate,
		SyncExceptionListType *exceptions)
{
	Char copy[200];
	Char *token, *next;
	Boolean hasRepeat = false;
	UInt16 i;
	static const Char *days[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};

	MemSet (repeat, sizeof (*repeat), 0);
	MemSet (exceptions, sizeof (*exceptions), 0);
	repeat->repeatFrequency = 1;
	DateToInt (repeat->repeatEndDate) = apptNoEndDate;
	if (!rule || !*rule) return false;
	StrNCopy (copy, rule, sizeof (copy) - 1);
	copy[sizeof (copy) - 1] = '\0';
	token = copy;
	while (token)
		{
		next = StrChr (token, ';');
		if (next) *next++ = '\0';
		if (StrNCompare (token, "FREQ=DAILY", 10) == 0) repeat->repeatType = repeatDaily, hasRepeat = true;
		else if (StrNCompare (token, "FREQ=WEEKLY", 11) == 0) repeat->repeatType = repeatWeekly, hasRepeat = true;
		else if (StrNCompare (token, "FREQ=MONTHLY", 12) == 0) repeat->repeatType = repeatMonthlyByDate, hasRepeat = true;
		else if (StrNCompare (token, "FREQ=YEARLY", 11) == 0) repeat->repeatType = repeatYearly, hasRepeat = true;
		else if (StrNCompare (token, "INTERVAL=", 9) == 0)
			{
			UInt16 frequency = StrAToI (token + 9);
			repeat->repeatFrequency = (UInt8) (frequency < 1 ? 1 : (frequency > 99 ? 99 : frequency));
			}
		else if (StrNCompare (token, "BYMONTHDAY=", 11) == 0)
			repeat->repeatType = repeatMonthlyByDate;
		else if (StrNCompare (token, "BYDAY=", 6) == 0)
			{
				Char *day = token + 6;
				if (repeat->repeatType == repeatWeekly) repeat->repeatOn = 0;
				while (day && *day)
					{
					Char *comma = StrChr (day, ',');
					UInt16 dayLength;
					if (comma) *comma++ = '\0';
					dayLength = StrLen (day);
					if (dayLength > 2)
						{
						Char dayCode[3];
						Int16 ordinal;
						MemMove (dayCode, day + dayLength - 2, 2);
						dayCode[2] = '\0';
						ordinal = (day[0] == '-') ? -1 : StrAToI (day);
						for (i = 0; i < daysInWeek; i++)
							if (StrCompare (dayCode, days[i]) == 0)
								repeat->repeatOn = (ordinal < 0 ? 28 : (ordinal - 1) * 7) + i;
						if (repeat->repeatType == repeatMonthlyByDate)
							repeat->repeatType = repeatMonthlyByDay;
						day = comma;
						continue;
						}
					if (repeat->repeatType == repeatMonthlyByDate && dayLength == 2)
						{
						for (i = 0; i < daysInWeek; i++)
							if (StrCompare (day, days[i]) == 0)
								repeat->repeatOn = DayOfMonth (startDate.month, startDate.day,
									startDate.year + firstYear);
						repeat->repeatType = repeatMonthlyByDay;
						day = comma;
						continue;
						}
					for (i = 0; i < daysInWeek; i++)
						if (StrCompare (day, days[i]) == 0) repeat->repeatOn |= (1 << i);
					day = comma;
				}
				if (repeat->repeatType == repeatMonthlyByDate)
					repeat->repeatType = repeatMonthlyByDay;
			}
		else if (StrNCompare (token, "UNTIL=", 6) == 0 && StrLen (token) >= 14)
			{
				Char digits[5];
				digits[4] = '\0';
				MemMove (digits, token + 6, 4);
				repeat->repeatEndDate.year = (UInt16) StrAToI (digits) - firstYear;
				MemMove (digits, token + 10, 2); digits[2] = '\0';
				repeat->repeatEndDate.month = (UInt8) StrAToI (digits);
				MemMove (digits, token + 12, 2);
				repeat->repeatEndDate.day = (UInt8) StrAToI (digits);
			}
		else if (StrNCompare (token, "EXDATE=", 7) == 0)
			{
				Char *dateText = token + 7;
				while (dateText && *dateText && exceptions->numExceptions < syncMaxExceptions)
					{
					Char *comma = StrChr (dateText, ',');
					Char digits[5];
					DateType *exceptionDate = &exceptions->dates[exceptions->numExceptions];
					if (comma) *comma++ = '\0';
					if (StrLen (dateText) == 8)
						{
						MemMove (digits, dateText, 4); digits[4] = '\0';
						exceptionDate->year = (UInt16) StrAToI (digits) - firstYear;
						MemMove (digits, dateText + 4, 2); digits[2] = '\0';
						exceptionDate->month = (UInt8) StrAToI (digits);
						MemMove (digits, dateText + 6, 2);
						exceptionDate->day = (UInt8) StrAToI (digits);
						exceptions->numExceptions++;
						}
					dateText = comma;
					}
			}
		token = next;
		}
	if (repeat->repeatType == repeatWeekly && repeat->repeatOn == 0)
		repeat->repeatOn = 1 << DayOfWeek (startDate.month, startDate.day, startDate.year + firstYear);
	repeat->repeatStartOfWeek = StartDayOfWeek;
	return hasRepeat;
}

static UInt16 SyncFindRecord (UInt32 palmID)
{
	UInt16 i, count = DmNumRecords (ApptDB);
	UInt32 id;
	for (i = 0; i < count; i++)
		if (DmRecordInfo (ApptDB, i, NULL, &id, NULL) == errNone && id == palmID)
			return i;
	return 0xffff;
}

static __attribute__((section("code3"))) Err SyncApplyEvent (Char *line)
{
	UInt16 recordNum, attr;
	UInt32 palmID, googleUpdated, start, end, restoreID, localModified;
	DateTimeType startDT, endDT;
	ApptDateTimeType when;
	ApptDBRecordType record;
	ApptDBRecordFlags changed;
	RepeatInfoType repeat;
	SyncExceptionListType exceptions;
	Char googleID[syncGoogleIDSize];
	Boolean allDay;
	Boolean restoreDeleted = false;
	Err err;

	SyncShowProgress ("parse fields");
	if (!SyncSplit (line, 12) || StrCompare (syncFields[0], "E") != 0) return dmErrInvalidParam;
	syncReceivedCount++;
	palmID = SyncAToUInt32 (syncFields[1]);
	StrNCopy (googleID, syncFields[2], sizeof (googleID) - 1);
	googleID[sizeof (googleID) - 1] = '\0';
	googleUpdated = SyncAToUInt32 (syncFields[3]);
	start = SyncAToUInt32 (syncFields[4]);
	end = SyncAToUInt32 (syncFields[5]);
	allDay = StrCompare (syncFields[6], "1") == 0;
	/* A newer local edit wins; map timestamps are Palm epoch seconds. */
	SyncShowProgress ("find mapping");
	if (palmID == 0) SyncMapFindPalmID (googleID, &palmID);
	SyncMapGet (palmID, syncFields[2], &localModified);
	if (localModified > googleUpdated)
		{
		syncSkippedCount++;
		return errNone;
		}
	if (StrCompare (syncFields[11], "cancelled") == 0)
		{
		recordNum = palmID ? SyncFindRecord (palmID) : 0xffff;
		if (recordNum != 0xffff)
			{
			DmDeleteRecord (ApptDB, recordNum);
			DmRecordInfo (ApptDB, recordNum, &attr, NULL, NULL);
			attr &= ~dmRecAttrDirty;
			DmSetRecordInfo (ApptDB, recordNum, &attr, NULL);
			DmMoveRecord (ApptDB, recordNum, DmNumRecords (ApptDB));
			}
		syncAppliedCount++;
		return errNone;
		}

	if (palmID == 0) SyncMapFindPalmID (googleID, &palmID);
	SyncShowProgress ("build record");
	MemSet (&when, sizeof (when), 0);
	TimSecondsToDateTime (start, &startDT);
	when.date.year = startDT.year - firstYear;
	when.date.month = startDT.month;
	when.date.day = startDT.day;
	if (allDay)
		{
		TimeToInt (when.startTime) = apptNoTime;
		TimeToInt (when.endTime) = apptNoTime;
		}
	else
		{
		TimeToInt (when.startTime) = (startDT.hour << 8) | startDT.minute;
		TimSecondsToDateTime (end, &endDT);
		TimeToInt (when.endTime) = (endDT.hour << 8) | endDT.minute;
		if (TimeToInt (when.endTime) > apptMaxEndTime) TimeToInt (when.endTime) = apptMaxEndTime;
		}

	MemSet (&record, sizeof (record), 0);
	/* Events without an RRULE still need a valid empty exception list. */
	MemSet (&exceptions, sizeof (exceptions), 0);
	record.when = &when;
	record.description = syncFields[7];
	record.note = syncFields[8];
	if (SyncParseRule (syncFields[10], &repeat, when.date, &exceptions)) record.repeat = &repeat;
	if (exceptions.numExceptions) record.exceptions = (ExceptionsListPtr) &exceptions;
	MemSet (&changed, sizeof (changed), 0);
	changed.when = changed.description = changed.note = changed.repeat = changed.exceptions = true;

	recordNum = (palmID ? SyncFindRecord (palmID) : 0xffff);
	if (recordNum != 0xffff &&
			DmRecordInfo (ApptDB, recordNum, &attr, NULL, NULL) == errNone &&
			(attr & dmRecAttrDelete))
		{
		restoreID = palmID;
		DmRemoveRecord (ApptDB, recordNum);
		recordNum = 0xffff;
		restoreDeleted = true;
		}
	if (recordNum == 0xffff)
		{
		SyncShowProgress ("write appointment");
		err = ApptNewRecord (ApptDB, &record, &recordNum);
		if (err != errNone) return err;
		if (restoreDeleted)
			{
			err = DmSetRecordInfo (ApptDB, recordNum, NULL, &restoreID);
			if (err != errNone) return err;
			palmID = restoreID;
			}
		}
	else
		{
		SyncShowProgress ("update appointment");
		err = ApptChangeRecord (ApptDB, &recordNum, &record, changed);
		if (err != errNone) return err;
		}
	SyncShowProgress ("set record flags");
	DmRecordInfo (ApptDB, recordNum, &attr, &palmID, NULL);
	attr &= ~(dmRecAttrDirty | dmRecAttrDelete);
	DmSetRecordInfo (ApptDB, recordNum, &attr, NULL);
	if (googleID[0])
		{
		SyncShowProgress ("write Google map");
		err = SyncMapPut (palmID, googleID, googleUpdated);
		if (err != errNone) return err;
		}
	syncAppliedCount++;
	SyncShowProgress ("event complete");
	return errNone;
}

__attribute__((section("code3"))) Err CalendarSyncWithPC (void)
{
	Err err;
	DateTimeType now;
	DateType first, last;
	UInt32 windowStart, windowEnd;
	UInt16 count, i;
	Char request[64];
	Char status[32];
	MemHandle recordH;
	ApptDBRecordType appt;
	UInt16 attr;
	UInt32 palmID, start;

	syncMapDB = SyncOpenMapDB ();
	if (!syncMapDB) return dmErrCantOpen;
	syncReceivedCount = 0;
	syncAppliedCount = 0;
	syncSkippedCount = 0;
	syncStage = 0;
	syncEventIndex = 0;
	syncTotalEvents = 0;
	syncLastLineLength = 0;
	syncPendingLength = 0;
	syncLine = MemPtrNew (syncLineSize);
	syncFields = MemPtrNew (12 * syncFieldSize);
	syncTxLine = MemPtrNew (syncLineSize);
	if (!syncLine || !syncFields || !syncTxLine)
		{
		err = memErrNotEnoughSpace;
		goto exit;
		}
	err = SysLibFind ("Serial Library", &syncSerialRef);
	if (err != errNone) goto exit;
	err = SerOpen (syncSerialRef, 0, 9600);
	if (err == serErrAlreadyOpen)
		{
		SerClose (syncSerialRef);
		err = SerOpen (syncSerialRef, 0, 9600);
		}
	if (err != errNone) goto exit;
	SerReceiveFlush (syncSerialRef, 1);

	TimSecondsToDateTime (TimGetSeconds (), &now);
	first.year = now.year - firstYear;
	first.month = now.month;
	first.day = now.day;
	last = first;
	DateAdjust (&first, -syncPastDays);
	DateAdjust (&last, syncFutureDays);
	windowStart = SyncDateTimeToSeconds (first, 0, 0);
	windowEnd = SyncDateTimeToSeconds (last, 23, 59);
	StrCopy (request, "SYNC1|");
	SyncUInt32ToA (request + StrLen (request), windowStart);
	StrCat (request, "|");
	SyncUInt32ToA (request + StrLen (request), windowEnd);
	StrCat (request, "\n");
	err = SyncSend (request);
	if (err != errNone) goto closeSerial;
	syncStage = 1;

	err = SyncSendDirtyRecords (windowStart, windowEnd);
	if (err != errNone) goto closeSerial;
	syncStage = 2;
	SerSendWait (syncSerialRef, SysTicksPerSecond () * 2);
	err = SyncReadLine (status, sizeof (status), SysTicksPerSecond () * 60);
	if (err != errNone) goto closeSerial;
	if (StrNCompare (status, "OK|", 3) != 0)
		{
			err = dmErrInvalidParam;
		goto closeSerial;
		}
	count = StrAToI (status + 3);
	syncTotalEvents = count;
	syncStage = 3;

	for (i = 0; i < count; i++)
		{
		syncEventIndex = i + 1;
		syncStage = 4;
		SyncShowProgress ("read event line");
		/* syncLine is heap allocated; sizeof(syncLine) is only the pointer size
		 * on this 68K target, which truncated the receive buffer to four bytes. */
		err = SyncReadLine (syncLine, syncLineSize, SysTicksPerSecond () * 10);
		if (err != errNone) goto closeSerial;
		syncStage = 5;
		SyncShowProgress ("line received");
		syncApplyingDownload = true;
		err = SyncApplyEvent (syncLine);
		syncApplyingDownload = false;
		if (err != errNone) goto closeSerial;
		syncStage = 6;
		}
	err = SyncReadLine (status, sizeof (status), SysTicksPerSecond () * 10);
	if (err == errNone && StrCompare (status, "END_DOWNLOAD") != 0) err = dmErrInvalidParam;
	if (err != errNone) goto closeSerial;
	syncStage = 7;

	// Only clear dirty flags after the server accepted the upload and the full download.
	count = DmNumRecords (ApptDB);
	for (i = 0; i < count; i++)
		{
		if (DmRecordInfo (ApptDB, i, &attr, &palmID, NULL) != errNone || !(attr & dmRecAttrDirty)) continue;
		if (attr & dmRecAttrDelete)
			{
			attr &= ~dmRecAttrDirty;
			DmSetRecordInfo (ApptDB, i, &attr, NULL);
			continue;
			}
		if (ApptGetRecord (ApptDB, i, &appt, &recordH) != errNone) continue;
		start = SyncRecordStart (&appt);
		MemHandleUnlock (recordH);
		if (start >= windowStart && start < windowEnd)
			{
			attr &= ~dmRecAttrDirty;
			DmSetRecordInfo (ApptDB, i, &attr, NULL);
			}
		}
	syncStage = 8;
closeSerial:
	/* Report both success and mid-download failures while the port is open. */
	if (syncTxLine)
		{
		Err resultErr = err;
		syncTxLine[0] = '\0';
		StrCopy (syncTxLine, "SYNC_RESULT|");
		SyncUInt32ToA (syncTxLine + StrLen (syncTxLine), (UInt32) resultErr);
		StrCat (syncTxLine, "|");
		SyncUInt32ToA (syncTxLine + StrLen (syncTxLine), syncStage);
		StrCat (syncTxLine, "|");
		SyncUInt32ToA (syncTxLine + StrLen (syncTxLine), syncEventIndex);
		StrCat (syncTxLine, "|");
		SyncUInt32ToA (syncTxLine + StrLen (syncTxLine), syncReceivedCount);
		StrCat (syncTxLine, "|");
		SyncUInt32ToA (syncTxLine + StrLen (syncTxLine), syncAppliedCount);
		StrCat (syncTxLine, "|");
		SyncUInt32ToA (syncTxLine + StrLen (syncTxLine), syncSkippedCount);
		StrCat (syncTxLine, "|");
		SyncUInt32ToA (syncTxLine + StrLen (syncTxLine), syncLastLineLength);
		StrCat (syncTxLine, "\n");
		if (SyncSend (syncTxLine) == errNone)
			SerSendWait (syncSerialRef, SysTicksPerSecond () * 2);
		}
	SerClose (syncSerialRef);
	syncSerialRef = sysInvalidRefNum;
exit:
	if (syncMapDB) DmCloseDatabase (syncMapDB);
	syncMapDB = NULL;
	if (syncLine) MemPtrFree (syncLine);
	if (syncFields) MemPtrFree (syncFields);
	if (syncTxLine) MemPtrFree (syncTxLine);
	syncLine = NULL;
	syncFields = NULL;
	syncTxLine = NULL;
	syncPendingLength = 0;
	return err;
}
