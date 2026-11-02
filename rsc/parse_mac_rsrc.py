import os, struct

def parse_rsrc(data):
    if len(data) < 256:
        return []
    
    # Verificar si es contenedor AppleDouble (magic 0x00051607)
    magic = struct.unpack(">I", data[:4])[0]
    if magic == 0x00051607:
        num_entries = struct.unpack(">H", data[24:26])[0]
        pos = 26
        rsrc_offset = 0
        for _ in range(num_entries):
            if pos + 12 > len(data): break
            entry_id, offset, length = struct.unpack(">III", data[pos:pos+12])
            if entry_id == 2: # Entry 2 es la sección de Resource Fork
                rsrc_offset = offset
                break
            pos += 12
        if rsrc_offset == 0 or rsrc_offset >= len(data):
            return []
        data = data[rsrc_offset:]

    if len(data) < 30: return []
    data_off, map_off, data_len, map_len = struct.unpack(">IIII", data[:16])
    if map_off == 0 or map_off + map_len > len(data): return []

    map_data = data[map_off:map_off+map_len]
    if len(map_data) < 30: return []

    type_list_off = struct.unpack(">H", map_data[24:26])[0]
    type_list = map_data[type_list_off:]
    if len(type_list) < 2: return []

    num_types = struct.unpack(">H", type_list[:2])[0] + 1
    resources = []
    type_pos = 2

    for _ in range(num_types):
        if type_pos + 8 > len(type_list): break
        rtype, count_m1, ref_off = struct.unpack(">4sHH", type_list[type_pos:type_pos+8])
        count = count_m1 + 1
        rtype_str = rtype.decode("latin1", errors="ignore")
        
        ref_pos = ref_off
        for _ in range(count):
            if ref_pos + 12 > len(type_list): break
            res_id = struct.unpack(">H", type_list[ref_pos:ref_pos+2])[0]
            attr_and_off = type_list[ref_pos+4:ref_pos+7]
            res_data_off = (attr_and_off[0] << 16) | (attr_and_off[1] << 8) | attr_and_off[2]
            
            abs_pos = data_off + res_data_off
            if abs_pos + 4 <= len(data):
                res_size = struct.unpack(">I", data[abs_pos:abs_pos+4])[0]
                res_bytes = data[abs_pos+4 : abs_pos+4+res_size]
                resources.append((rtype_str, res_id, res_bytes))
            ref_pos += 12
        type_pos += 8
    return resources

print("--- ESCANEANDO ARCHIVOS DE RECURSOS ---")
for root, dirs, files in os.walk(".."):
    for file in files:
        if file.endswith((".rsrc", ".bin")):
            path = os.path.join(root, file)
            try:
                with open(path, "rb") as f: content = f.read()
                recs = parse_rsrc(content)
                for rtype, rid, rbytes in recs:
                    if rtype == "tFRM" and rid == 1000:
                        print(f"\n[!] ¡FORMULARIO DayView (1000) ENCONTRADO!")
                        print(f"    Archivo fuente: {path}")
                        out_name = f"tFRM{rid:04X}.bin"
                        with open(out_name, "wb") as out: out.write(rbytes)
                        print(f"    Recurso extraído a: {out_name} ({len(rbytes)} bytes)")
                    elif rtype == "tFRM":
                        print(f"    Formulario tFRM ID {rid} en: {path}")
            except Exception as e:
                pass
