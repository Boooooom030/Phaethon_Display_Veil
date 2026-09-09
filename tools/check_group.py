# Dump all RT_ICON raw payloads from the built exe and compare byte-for-byte
# with the source icon.ico images.
import struct

data = open('build/Phaethon.exe','rb').read()
pe_off = struct.unpack_from('<I', data, 0x3C)[0]
nsec = struct.unpack_from('<H', data, pe_off+6)[0]
opt_size = struct.unpack_from('<H', data, pe_off+20)[0]
opt_off = pe_off + 24
res_rva = struct.unpack_from('<I', data, opt_off + 112 + 8*2)[0]
sec_off = opt_off + opt_size
def rva_to_off(rva):
    for i in range(nsec):
        s = sec_off + i*40
        vsz, va, rsz, ro = struct.unpack_from('<IIII', data, s+8)
        if va <= rva < va + max(vsz, rsz):
            return ro + (rva - va)
res_base = rva_to_off(res_rva)
rsrc = data[res_base:res_base+90320]

ico = open('icon.ico','rb').read()
count = struct.unpack_from('<H', ico, 4)[0]
ico_entries = []
for i in range(count):
    off = 6 + i*16
    w,h,colors,res,planes,bpp,size,rva = struct.unpack_from('<BBBBHHII', ico, off)
    payload = ico[rva:rva+size]
    ico_entries.append((256 if w==0 else w, bpp, size, payload))

print('icon.ico contains:', [(w,b) for w,_,s,_ in ico_entries])

# For each RT_ICON id found in the group, compare with corresponding ico payload
pos = 0
found_group = rsrc.find(b'\x00\x00\x01\x00\x04\x00')
entries = []
for i in range(4):
    e = found_group + 6 + i*14
    w, h, colors, res, planes, bpp, gsize, rid = struct.unpack_from('<BBBBHHIH', rsrc, e)
    # locate the ICON data dir: scan rsrc for data entries pointing to payloads
    entries.append((256 if w==0 else w, bpp, gsize, rid))

# Extract RT_ICON payloads: search rsrc for each known payload and compare
print()
for w, bpp, gsize, rid in entries:
    payload = next(p for ww,b,s,p in ico_entries if ww==w and b==bpp)
    present = payload in data
    print(f'{w}x{h if False else ""}{w}x{w} bpp={bpp}: payload {gsize}B present-in-exe={present}')
