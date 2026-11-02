import os, struct

for root, dirs, files in os.walk("."):
    for file in files:
        if file.endswith(".rsrc"):
            path = os.path.join(root, file)
            with open(path, "rb") as f:
                data = f.read()
            if len(data) < 78:
                continue
            num_recs = struct.unpack(">H", data[76:78])[0]
            pos = 78
            for _ in range(num_recs):
                if pos + 10 > len(data):
                    break
                rtype, rid = struct.unpack(">4sH", data[pos:pos+6])
                if rtype == b"tFRM" and rid == 1000:
                    print(f"--> ENCONTRADO EN: {path}")
                pos += 10
