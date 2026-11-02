import os
from parse_mac_rsrc import parse_rsrc

rsrc_dir = "../rsc"
output_dir = "../Debug"

print("--- EXTRAYENDO RECURSOS BASE (INGLÉS) ---")
for root, dirs, files in os.walk(rsrc_dir):
    if "Localized" in root:
        continue
    for file in files:
        if file.endswith(".rsrc"):
            path = os.path.join(root, file)
            try:
                with open(path, "rb") as f:
                    recs = parse_rsrc(f.read())
                for rtype, rid, rbytes in recs:
                    filename = f"{rtype}{rid:04X}.bin"
                    out_path = os.path.join(output_dir, filename)
                    with open(out_path, "wb") as out:
                        out.write(rbytes)
                    print(f"Extraído: {filename} ({len(rbytes)} bytes) <- {path}")
            except Exception as e:
                pass
