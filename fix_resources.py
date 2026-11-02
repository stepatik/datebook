import os, glob, struct

def parse_rsrc(data):
    if len(data) < 16: return []
    magic = struct.unpack(">I", data[:4])[0]
    if magic == 0x00051607:
        if len(data) < 26: return []
        num_entries = struct.unpack(">H", data[24:26])[0]
        pos = 26
        rsrc_offset = 0
        for _ in range(num_entries):
            if pos + 12 > len(data): break
            entry_id, offset, length = struct.unpack(">III", data[pos:pos+12])
            if entry_id == 2:
                rsrc_offset = offset
                break
            pos += 12
        if rsrc_offset == 0 or rsrc_offset + 16 > len(data): return []
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
            # Resource-reference layout: ID (2), name offset (2), attributes (1),
            # data offset (3), handle (4). The offset starts at byte +5; +4 is
            # the attributes byte and must not be included.
            res_data_off = int.from_bytes(type_list[ref_pos+5:ref_pos+8], "big")
            
            abs_pos = data_off + res_data_off
            if abs_pos + 4 <= len(data):
                res_size = struct.unpack(">I", data[abs_pos:abs_pos+4])[0]
                end_pos = abs_pos + 4 + res_size
                if end_pos <= len(data):
                    res_bytes = data[abs_pos+4 : end_pos]
                    resources.append((rtype_str, res_id, res_bytes))
            ref_pos += 12
        type_pos += 8
    return resources

def main():
    # Extract the default English resources only. Localized resource forks are
    # intentionally excluded; build-prc consumes the resulting root-level bins.
    src_files = sorted(glob.glob(os.path.join("rsc", "*.rsrc")))
    output_dir = "."
    extracted_count = 0

    for path in src_files:
        with open(path, "rb") as source:
            recs = parse_rsrc(source.read())
        if not recs:
            raise ValueError(f"No se pudieron leer recursos de {path}")
        for rtype, rid, rbytes in recs:
            filename = f"{rtype}{rid:04X}.bin"
            out_path = os.path.join(output_dir, filename)
            with open(out_path, "wb") as out:
                out.write(rbytes)
            extracted_count += 1
            print(f"OK: {filename} ({len(rbytes)} bytes) <- {path}")

    print(f"\nSe extrajeron {extracted_count} recursos ingleses a {output_dir}")


if __name__ == "__main__":
    main()
