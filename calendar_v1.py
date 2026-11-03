"""Serial bridge between Datebook and Google Calendar.

Wire protocol (ASCII, LF terminated, percent-escaped text fields):

    SYNC1|from_epoch|to_epoch
    P|palm_uid|google_id|modified_epoch|start_epoch|end_epoch|all_day|summary|description|location|rrule|deleted
    ...
    END_UPLOAD

The response is a count followed by Google event records and END_DOWNLOAD:

    E|palm_uid|google_id|updated_epoch|start_epoch|end_epoch|all_day|summary|description|location|rrule|status

Epochs are seconds from the Palm 1904 epoch, encoded as local wall time. All-day
end times are exclusive on the wire, as in Google Calendar.
"""

import datetime as dt
import json
import os
from pathlib import Path
from zoneinfo import ZoneInfo

import serial
from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
from googleapiclient.discovery import build


PORT = os.environ.get("PALM_SERIAL_PORT", "/dev/ttyUSB0")
BAUD = int(os.environ.get("PALM_SERIAL_BAUD", "9600"))
CALENDAR_ID = os.environ.get("GOOGLE_CALENDAR_ID", "primary")
TOKEN_FILE = Path(os.environ.get("GOOGLE_TOKEN_FILE", "token.json"))
CLIENT_FILE = Path(os.environ.get("GOOGLE_CLIENT_FILE", "credentials.json"))
STATE_FILE = Path(os.environ.get("PALM_SYNC_STATE", "calendar_sync_state.json"))
PALM_TIMEZONE = ZoneInfo(os.environ.get("PALM_TIMEZONE", "Europe/Paris"))
SCOPES = ["https://www.googleapis.com/auth/calendar.events"]
PALM_EPOCH = dt.datetime(1904, 1, 1)


def _service():
    creds = None
    if TOKEN_FILE.exists():
        creds = Credentials.from_authorized_user_file(str(TOKEN_FILE), SCOPES)
    if not creds or not creds.valid or not creds.has_scopes(SCOPES):
        if creds and creds.expired and creds.refresh_token:
            print("[OAUTH] Refrescando token expirado...")
            creds.refresh(Request())
        else:
            print("[OAUTH] Autenticando por navegador...")
            flow = InstalledAppFlow.from_client_secrets_file(str(CLIENT_FILE), SCOPES)
            creds = flow.run_local_server(port=0)
        TOKEN_FILE.write_text(creds.to_json(), encoding="utf-8")
        print("[OAUTH] Nuevo token guardado correctamente.")
    return build("calendar", "v3", credentials=creds, cache_discovery=False)


def _state_read():
    try:
        return json.loads(STATE_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {"palm_to_google": {}}


def _state_write(state):
    temp = STATE_FILE.with_suffix(STATE_FILE.suffix + ".tmp")
    temp.write_text(json.dumps(state, separators=(",", ":")), encoding="utf-8")
    temp.replace(STATE_FILE)


def _epoch(value):
    if not value:
        return 0
    if isinstance(value, (int, float)):
        parsed = dt.datetime.fromtimestamp(int(value), dt.timezone.utc)
        parsed = parsed.astimezone(PALM_TIMEZONE).replace(tzinfo=None)
    else:
        parsed = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
        if parsed.tzinfo is not None:
            parsed = parsed.astimezone(PALM_TIMEZONE).replace(tzinfo=None)
    return int((parsed - PALM_EPOCH).total_seconds())


def _gtime(epoch):
    local = PALM_EPOCH + dt.timedelta(seconds=int(epoch))
    return local.replace(tzinfo=PALM_TIMEZONE).astimezone(dt.timezone.utc).isoformat().replace("+00:00", "Z")


def _local_time(epoch):
    return (PALM_EPOCH + dt.timedelta(seconds=int(epoch))).isoformat()


def _field(text):
    raw = (text or "").encode("latin-1", errors="replace")
    return "".join(chr(byte) if byte >= 0x20 and byte != 0x7F and byte not in (ord("%"), ord("|"))
                   else "%%%02X" % byte for byte in raw)


def _record_fields(line):
    fields = []
    for part in line.rstrip("\r\n").split("|"):
        raw = bytearray()
        i = 0
        while i < len(part):
            if part[i] == "%" and i + 2 < len(part):
                try:
                    raw.append(int(part[i + 1:i + 3], 16))
                    i += 3
                    continue
                except ValueError:
                    pass
            raw.append(ord(part[i]) & 0xFF)
            i += 1
        fields.append(raw.decode("latin-1"))
    return fields


def _iter_events(service, time_min, time_max):
    token = None
    while True:
        page = service.events().list(
            calendarId=CALENDAR_ID,
            timeMin=_gtime(time_min),
            timeMax=_gtime(time_max),
            maxResults=250,
            singleEvents=False,
            showDeleted=True,
            pageToken=token,
        ).execute()
        yield from page.get("items", [])
        token = page.get("nextPageToken")
        if not token:
            break


def _upload_record(service, state, parts):
    if len(parts) != 12:
        raise ValueError("registro Palm P con número de campos incorrecto")
    _, uid, google_id, modified, start, end, all_day, summary, description, location, rrule, deleted = parts
    if not uid.isdigit():
        raise ValueError("palm_uid debe ser numérico")
    modified, start, end = int(modified), int(start), int(end)
    event = {
        "summary": summary,
        "description": description,
        "extendedProperties": {"private": {"palmUid": uid, "palmModified": str(modified)}},
    }
    if location:
        event["location"] = location
    if all_day == "1":
        start_date = (PALM_EPOCH + dt.timedelta(seconds=start)).date()
        end_date = (PALM_EPOCH + dt.timedelta(seconds=end)).date()
        if end_date <= start_date:
            end_date = start_date + dt.timedelta(days=1)
        event["start"] = {"date": start_date.isoformat()}
        event["end"] = {"date": end_date.isoformat()}
    else:
        event["start"] = {"dateTime": _local_time(start), "timeZone": PALM_TIMEZONE.key}
        event["end"] = {"dateTime": _local_time(end), "timeZone": PALM_TIMEZONE.key}
    if rrule:
        rule, separator, exdates = rrule.partition(";EXDATE=")
        event["recurrence"] = [rule if rule.startswith("RRULE:") else "RRULE:" + rule]
        if separator and exdates:
            dates = [value for value in exdates.split(",")[:16]
                     if len(value) == 8 and value.isdigit()]
            if "dateTime" in event["start"]:
                start_local = dt.datetime.fromisoformat(event["start"]["dateTime"])
                values = [dt.datetime.combine(dt.date(int(value[:4]), int(value[4:6]), int(value[6:8])),
                                              start_local.time().replace(tzinfo=None)).isoformat()
                          for value in dates]
                event["recurrence"].append("EXDATE;TZID=%s:%s" %
                                           (PALM_TIMEZONE.key, ",".join(values)))
            elif dates:
                event["recurrence"].append("EXDATE;VALUE=DATE:" + ",".join(dates))

    event_id = google_id or state["palm_to_google"].get(uid)
    if deleted == "1":
        if event_id:
            try:
                remote = service.events().get(calendarId=CALENDAR_ID, eventId=event_id).execute()
                if _epoch(remote.get("updated")) > modified:
                    state["palm_to_google"][uid] = event_id
                    return
                service.events().delete(calendarId=CALENDAR_ID, eventId=event_id).execute()
                print(f"   [GOOGLE] Eliminado evento {event_id} (Palm UID: {uid})")
            except Exception as exc:
                if getattr(exc, "resp", None) is None or exc.resp.status != 404:
                    raise
        state["palm_to_google"].pop(uid, None)
        return
    if event_id:
        try:
            remote = service.events().get(calendarId=CALENDAR_ID, eventId=event_id).execute()
            state["palm_to_google"][uid] = event_id
            if _epoch(remote.get("updated")) > modified:
                print(f"   [SYNC] Ignorando cambio de Palm UID {uid}: la versión de Google es más reciente.")
                return
            if remote.get("status") == "cancelled":
                created = service.events().insert(calendarId=CALENDAR_ID, body=event).execute()
                state["palm_to_google"][uid] = created["id"]
                print(f"   [GOOGLE] Creado de nuevo evento (Palm UID: {uid})")
                return
            if not rrule:
                event["recurrence"] = []
            service.events().patch(calendarId=CALENDAR_ID, eventId=event_id, body=event).execute()
            print(f"   [GOOGLE] Actualizado evento '{summary}' (Palm UID: {uid})")
            return
        except Exception as exc:
            if getattr(exc, "resp", None) is None or exc.resp.status != 404:
                raise
    created = service.events().insert(calendarId=CALENDAR_ID, body=event).execute()
    state["palm_to_google"][uid] = created["id"]
    print(f"   [GOOGLE] Creado nuevo evento '{summary}' (Palm UID: {uid})")


def _download_record(event, state):
    start_obj, end_obj = event.get("start") or {}, event.get("end") or {}
    all_day = "date" in start_obj
    start = _epoch(start_obj.get("date") if all_day else start_obj.get("dateTime"))
    end = _epoch(end_obj.get("date") if all_day else end_obj.get("dateTime"))
    private = (event.get("extendedProperties") or {}).get("private", {})
    mapped_uid = next((palm_uid for palm_uid, event_id in
                       state.get("palm_to_google", {}).items() if event_id == event.get("id")), "0")
    if event.get("recurringEventId"):
        uid = mapped_uid
    else:
        uid = private.get("palmUid", "0") or mapped_uid
    updated = _epoch(event.get("updated"))
    recurrence = event.get("_palmRecurrence")
    if recurrence is None:
        recurrence = next((rule[6:] for rule in event.get("recurrence", [])
                           if rule.startswith("RRULE:")), "")
    exdates = list(event.get("_palmExdates", []))
    for rule in event.get("recurrence", []):
        if not rule.startswith("EXDATE") or ":" not in rule:
            continue
        values = rule.split(":", 1)[1].split(",")
        for value in values:
            if len(value) >= 8 and value[:8].isdigit():
                exdates.append(value[:8])
    exdates = sorted(set(exdates))[:16]
    if exdates:
        recurrence += ";EXDATE=" + ",".join(exdates)
    values = [
        "E", uid, event.get("id", ""), str(updated), str(start), str(end),
        "1" if all_day else "0", event.get("summary", "")[:255], event.get("description", "")[:255],
        event.get("location", "")[:40], recurrence[:300], event.get("status", "confirmed"),
    ]
    return "|".join(_field(value) for value in values)


def _combine_recurring_exceptions(events):
    copies = [dict(event) for event in events]
    by_id = {event.get("id"): event for event in copies}
    suppressed = set()
    for event in copies:
        master_id = event.get("recurringEventId")
        if not master_id:
            continue
        master = by_id.get(master_id)
        if not master:
            continue
        original = event.get("originalStartTime") or event.get("start") or {}
        original_date = original.get("date")
        if not original_date and original.get("dateTime"):
            original_date = dt.datetime.fromisoformat(
                original["dateTime"].replace("Z", "+00:00")).astimezone(PALM_TIMEZONE).date().isoformat()
        if original_date:
            date_value = dt.date.fromisoformat(original_date)
            master.setdefault("_palmExdates", []).append(date_value.strftime("%Y%m%d"))
        if event.get("status") == "cancelled":
            suppressed.add(event.get("id"))
        else:
            event["_palmRecurrence"] = ""
    return [event for event in copies if event.get("id") not in suppressed]


def _handle_sync(ser, service, first_line):
    print(f"\n[SYNC] Iniciando protocolo de sincronización bidireccional...")
    request = _record_fields(first_line)
    if len(request) != 3:
        print("[ERR] Cabecera de sincronización no válida.")
        ser.write(b"ERR|bad_sync_header\n")
        return
    time_min, time_max = map(int, request[1:])
    print(f"[SYNC] Rango solicitado por Palm: Epoch {time_min} -> {time_max}")
    if time_max <= time_min or time_max - time_min > 370 * 86400:
        print("[ERR] Rango de fechas no válido.")
        ser.write(b"ERR|invalid_window\n")
        return

    uploads = []
    print("[SYNC] Recibiendo subidas desde la Palm...")
    while True:
        raw = ser.readline()
        if not raw:
            print("[ERR] Timeout esperando registros de la Palm.")
            ser.write(b"ERR|upload_timeout\n")
            return
        line = raw.decode("latin-1").rstrip("\r\n")
        if line == "END_UPLOAD":
            print(f"[SYNC] Fin de subida de la Palm. Se recibieron {len(uploads)} registros modificados.")
            break
        if line.startswith("P|"):
            print(f"   [PALM -> PC] {line}")
            uploads.append(_record_fields(line))
        else:
            print(f"[ERR] Línea de subida desconocida: {line}")
            ser.write(b"ERR|bad_upload_record\n")
            return

    state = _state_read()
    state.setdefault("palm_to_google", {})
    if uploads:
        print("[SYNC] Procesando registros locales de la Palm en Google Calendar...")
        for record in uploads:
            _upload_record(service, state, record)

    print("[GOOGLE] Consultando eventos desde Google Calendar...")
    events = _combine_recurring_exceptions(list(_iter_events(service, time_min, time_max)))
    events.sort(key=lambda item: (item.get("start") or {}).get(
        "dateTime", (item.get("start") or {}).get("date", "")))
    _state_write(state)
    
    header = "OK|%d\n" % len(events)
    print(f"[PC -> PALM] Enviando respuesta a la Palm: {header.strip()}")
    ser.write(header.encode("ascii"))
    ser.flush()

    # Palm OS 3 has a small serial receive buffer. Send one event at a time
    # and wait for the Palm's progress packet before sending the next one.
    # This also streams crash diagnostics to the terminal while the Palm works.
    for event_index, event in enumerate(events, 1):
        while True:
            raw = ser.readline()
            if not raw:
                raise TimeoutError(
                    f"Timeout esperando a Palm antes del evento {event_index}"
                )
            line = raw.decode("latin-1").rstrip("\r\n")
            if line.startswith("SYNC_RESULT|"):
                print(f"[PALM RESULT] {line}")
                return
            if not line.startswith("SYNC_DEBUG|"):
                print(f"[PALM -> PC] Trama inesperada durante descarga: {line!r}")
                continue
            parts = line.split("|")
            print(f"[PALM DEBUG] {' | '.join(parts[1:])}")
            if (len(parts) >= 8 and parts[1].isdigit()
                    and int(parts[1]) == event_index
                    and parts[3] == "read event line"):
                break

        data = _download_record(event, state)
        print(f"   [PC -> PALM] {data}")
        ser.write((data + "\n").encode("latin-1"))
        ser.flush()

        while True:
            raw = ser.readline()
            if not raw:
                raise TimeoutError(
                    f"Timeout esperando procesamiento del evento {event_index}"
                )
            line = raw.decode("latin-1").rstrip("\r\n")
            if line.startswith("SYNC_RESULT|"):
                print(f"[PALM RESULT] {line}")
                return
            if not line.startswith("SYNC_DEBUG|"):
                print(f"[PALM -> PC] Trama inesperada durante descarga: {line!r}")
                continue
            parts = line.split("|")
            print(f"[PALM DEBUG] {' | '.join(parts[1:])}")
            if (len(parts) >= 8 and parts[1].isdigit()
                    and int(parts[1]) == event_index
                    and parts[3] == "event complete"):
                break

    print("[PC -> PALM] Enviando marcador final: END_DOWNLOAD")
    ser.write(b"END_DOWNLOAD\n")
    ser.flush()
    print("[SYNC] ¡Sincronización completada con éxito!\n" + "-" * 50)


def serve():
    print(f"=== SERVIDOR PALM <-> GOOGLE CALENDAR ===")
    print(f"Puerto: {PORT} a {BAUD} baudios")
    print("Conectando con la API de Google Calendar...")
    service = _service()
    print("Listo y esperando señales de la Palm...\n")

    try:
        with serial.Serial(PORT, BAUD, timeout=30, write_timeout=30) as ser:
            ser.reset_input_buffer()
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("latin-1").strip()
                if not line:
                    continue

                print(f"\n[PALM -> PC] Señal recibida: '{line}'")

                if line.startswith("SYNC1|"):
                    try:
                        _handle_sync(ser, service, line)
                    except Exception as exc:
                        print("Error de sincronización:", exc)
                        ser.write(b"ERR|sync_failed\n")
                        ser.flush()

                elif line == "GET_CALENDAR":
                    print("[PRUEBA] Orden 'GET_CALENDAR' recibida. Generando vista previa...")
                    now = _epoch(dt.datetime.now(PALM_TIMEZONE).isoformat())
                    records = list(_iter_events(service, now, now + 30 * 86400))
                    for event in records[:5]:
                        start = event.get("start", {})
                        label = start.get("dateTime", start.get("date", ""))[11:16]
                        msg = label + " " + event.get("summary", "")[:25] + "\n"
                        print(f"   [PC -> PALM] {msg.strip()}")
                        ser.write(msg.encode("latin-1", "replace"))
                    ser.flush()
                    print("[PRUEBA] Datos de vista previa enviados a la Palm.")

                elif line.startswith("SYNC_DEBUG|"):
                    print(f"[PALM DEBUG] {line}")

                elif line.startswith("SYNC_RESULT|"):
                    print(f"[PALM RESULT] {line}")

                else:
                    print(f"[ALERTA] Comando o trama no reconocida: '{line}'")

    except KeyboardInterrupt:
        print("\nServidor detenido manualmente por el usuario.")
    except serial.SerialException as exc:
        print("\nError en el puerto serie:", exc)


if __name__ == "__main__":
    serve()
