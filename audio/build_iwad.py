"""Rebuild the GBADoom IWAD for the VB: drop lumps the VB port never uses."""
import struct, sys
DROP = {"HELP1", "HELP2", "CREDIT", "DEMO1", "DEMO2", "DEMO3", "ENDOOM"}
src = open(sys.argv[1], 'rb').read()
n, off = struct.unpack_from('<II', src, 4)
dirs = [struct.unpack_from('<II8s', src, off + 16 * i) for i in range(n)]
data = bytearray()
base = 12 + 16 * n
placed = {}
newdir = []
saved = 0
for p, sz, name in dirs:
    nm = name.rstrip(b'\0').decode()
    if nm in DROP or sz == 0:
        saved += sz
        newdir.append((base, 0, name)); continue
    key = (p, sz)
    if key not in placed:
        while len(data) % 4: data.append(0)
        placed[key] = base + len(data)
        data += src[p:p + sz]
    newdir.append((placed[key], sz, name))
# extra lumps for the VB port (e.g. the "Select Level" menu patch)
import os
extra = []
for path in sys.argv[4:]:
    nm = os.path.splitext(os.path.basename(path))[0].upper().encode()
    extra.append((nm.ljust(8, b'\0'), open(path, 'rb').read()))
n += len(extra)
base = 12 + 16 * n
newdir = [(p - 16 * len(extra) * 0 + 16 * len(extra), sz, name) if sz else (base, 0, name) for p, sz, name in newdir]
for name, blob in extra:
    while len(data) % 4: data.append(0)
    newdir.append((base + len(data), len(blob), name))
    data += blob
out = bytearray(src[:4]) + struct.pack('<II', n, 12)
for p, sz, name in newdir: out += struct.pack('<II8s', p, sz, name)
out += data
open(sys.argv[2], 'wb').write(out)
# C source
with open(sys.argv[3], 'w') as f:
    f.write("const unsigned char doom_iwad[%dUL] = {\n" % len(out))
    for i in range(0, len(out), 40):
        f.write(",".join("0x%02x" % b for b in out[i:i + 40]) + ",\n")
    f.write("};\n")
print("old", len(src), "new", len(out), "saved", len(src) - len(out))
