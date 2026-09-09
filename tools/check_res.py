import struct

def res_summary(path):
    data = open(path,'rb').read()
    pe_off = struct.unpack_from('<I', data, 0x3C)[0]
    nsec = struct.unpack_from('<H', data, pe_off+6)[0]
    opt_size = struct.unpack_from('<H', data, pe_off+20)[0]
    opt_off = pe_off + 24
    dd_off = opt_off + 112  # PE32+
    res_rva = struct.unpack_from('<I', data, dd_off + 8*2)[0]
    sec_off = opt_off + opt_size
    def rva_to_off(rva):
        for i in range(nsec):
            s = sec_off + i*40
            vsz, va, rsz, ro = struct.unpack_from('<IIII', data, s+8)
            if va <= rva < va + max(vsz, rsz):
                return ro + (rva - va)
        return None
    res_base_off = rva_to_off(res_rva)
    ico = open('icon.ico','rb').read()
    # icon.ico payload offsets: header 6 bytes, then 4 entries * 16
    p16  = ico[6+4*16 : 6+4*16+1128]
    p128 = ico[6+3*16+16 : 6+3*16+16+67624]
    print(path)
    print('  exe size:', len(data))
    print('  res section:', 'yes' if res_base_off else 'NO')
    if not res_base_off:
        return
    print('  16px icon payload present:', p16 in data)
    print('  128px icon payload present:', p128 in data)
    # read top dir
    n_named, n_id = struct.unpack_from('<HH', data, res_base_off + 12)
    print('  top-level resource types:', n_id)

res_summary('build/Phaethon.exe')
res_summary('rel_check.exe')
