import os, struct

for root, dirs, files in os.walk(".."):
    for file in files:
        if file.endswith((".rsrc", ".bin", ".palmrsrc")):
            path = os.path.join(root, file)
            try:
                with open(path, "rb") as f:
                    data = f.read()
                idx = 0
                while True:
                    idx = data.find(b"tFRM", idx)
                    if idx == -1:
                        break
                    if idx + 6 <= len(data):
                        rid = struct.unpack(">H", data[idx+4:idx+6])[0]
                        print(f"--> Formulario tFRM ID {rid} (0x{rid:04X}) en: {path}")
                    idx += 4
            except Exception:
                pass
