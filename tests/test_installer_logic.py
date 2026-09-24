from __future__ import annotations
import struct

FLASH_SIZE = 0x1000000
SKU = b"bread-compact-wifi\x00"


def part_entry(label: str, ptype: int, subtype: int, offset: int, size: int, flags: int = 0) -> bytes:
    lab = label.encode('ascii')[:15] + b'\x00'
    lab = lab.ljust(16, b'\x00')
    return b'\xAA\x50' + bytes([ptype, subtype]) + struct.pack('<II', offset, size) + lab + struct.pack('<I', flags)


def table(stock: bool) -> bytes:
    entries = [
        part_entry('nvs', 1, 2, 0x9000, 0x4000),
        part_entry('otadata', 1, 0, 0xD000, 0x2000),
        part_entry('phy_init', 1, 1, 0xF000, 0x1000),
        part_entry('ota_0', 0, 0x10, 0x20000, 0x3F0000),
        part_entry('ota_1', 0, 0x11, 0x410000, 0x3F0000),
    ]
    if stock:
        entries += [part_entry('assets', 1, 0x82, 0x800000, 0x800000)]
    else:
        entries += [
            part_entry('assets', 1, 0x82, 0x800000, 0x600000),
            part_entry('care_data', 1, 0x82, 0xE00000, 0x100000),
            part_entry('voice', 1, 0x82, 0xF00000, 0x100000),
        ]
    return b''.join(entries) + b'\xFF' * 32


def app(version='2.5.0', project='xiaozhi', sku='bread-compact-wifi', data_len=768) -> bytes:
    if data_len < 256:
        raise ValueError
    header = bytearray(24)
    header[0] = 0xE9
    header[1] = 1  # one segment
    seg_header = bytearray(8)
    struct.pack_into('<I', seg_header, 0, 0x3C000020)
    struct.pack_into('<I', seg_header, 4, data_len)
    data = bytearray(b'\x00' * data_len)
    # esp_app_desc_t begins at first segment data, file offset 32
    struct.pack_into('<I', data, 0, 0xABCD5432)
    data[16:48] = version.encode()[:31].ljust(32, b'\x00')
    data[48:80] = project.encode()[:31].ljust(32, b'\x00')
    marker = sku.encode() + b'\x00'
    data[160:160+len(marker)] = marker
    return bytes(header + seg_header + data)


def parse_parts(flash: bytes):
    out=[]
    for pos in range(0, 0xC00, 32):
        o=0x8000+pos
        if flash[o:o+2] == b'\xff\xff': break
        if flash[o:o+2] != b'\xaa\x50': continue
        ptype, subtype=flash[o+2], flash[o+3]
        off,size=struct.unpack_from('<II',flash,o+4)
        label=flash[o+12:o+28].split(b'\0',1)[0].decode('ascii')
        out.append((label,ptype,subtype,off,size))
    return out


def layout_kind(parts):
    d={x[0]:x for x in parts}
    def p(label,off,size): return label in d and d[label][3:5] == (off,size)
    common=all([
        p('nvs',0x9000,0x4000), p('otadata',0xD000,0x2000), p('phy_init',0xF000,0x1000),
        p('ota_0',0x20000,0x3F0000), p('ota_1',0x410000,0x3F0000)])
    if not common:return 'unsupported'
    if p('assets',0x800000,0x800000) and 'care_data' not in d and 'voice' not in d:return 'xiaozhi-v2-16m'
    if p('assets',0x800000,0x600000) and p('care_data',0xE00000,0x100000) and p('voice',0xF00000,0x100000):return 'xiaozhi-care-16m'
    return 'unsupported'


def image_length(flash: bytes, off: int):
    if flash[off] != 0xE9:return 0
    n=flash[off+1]
    if not 1 <= n <= 16:return 0
    pos=off+24
    for _ in range(n):
        if pos+8 > len(flash):return 0
        ln=struct.unpack_from('<I',flash,pos+4)[0]
        pos += 8
        if ln > 0x400000 or pos+ln>len(flash):return 0
        pos += ln
    return pos-off


def app_info(flash: bytes, off: int):
    if flash[off] != 0xE9:return None
    desc=off+32
    if struct.unpack_from('<I',flash,desc)[0] != 0xABCD5432:return None
    ln=image_length(flash,off)
    if not 0 < ln <= 0x3F0000:return None
    z=lambda b: b.split(b'\0',1)[0].decode('ascii')
    return dict(offset=off,image_length=ln,version=z(flash[desc+16:desc+48]),project=z(flash[desc+48:desc+80]))


def contains_exact_sku(flash: bytes, info, sku: str):
    area=flash[info['offset']:info['offset']+info['image_length']]
    return (sku.encode()+b'\0') in area


def make_flash(stock=True, ota0=None, ota1=None):
    f=bytearray(b'\xFF'*FLASH_SIZE)
    t=table(stock)
    f[0x8000:0x8000+len(t)]=t
    if ota0:
        f[0x20000:0x20000+len(ota0)] = ota0
    if ota1:
        f[0x410000:0x410000+len(ota1)] = ota1
    return bytes(f)


def main():
    f=make_flash(True, app(), None)
    assert layout_kind(parse_parts(f)) == 'xiaozhi-v2-16m'
    i=app_info(f,0x20000); assert i and i['version']=='2.5.0' and i['project']=='xiaozhi'
    assert contains_exact_sku(f,i,'bread-compact-wifi')

    fc=make_flash(False, app(), app())
    assert layout_kind(parse_parts(fc)) == 'xiaozhi-care-16m'

    # Safety regression: exact SKU outside the valid ESP image must NOT count.
    bad=bytearray(make_flash(True, app(sku='other-board'), None))
    stale=0x20000+0x20000
    bad[stale:stale+len(SKU)] = SKU
    bi=app_info(bytes(bad),0x20000); assert bi
    assert not contains_exact_sku(bytes(bad),bi,'bread-compact-wifi')

    # Second valid OTA with another profile is detectable as ambiguity.
    mixed=make_flash(True, app(), app(sku='other-board'))
    infos=[x for x in (app_info(mixed,0x20000),app_info(mixed,0x410000)) if x]
    supported=[contains_exact_sku(mixed,x,'bread-compact-wifi') for x in infos if x['project']=='xiaozhi']
    assert supported == [True,False]

    # Partition drift is rejected.
    drift=bytearray(make_flash(True, app(), None))
    # Corrupt assets size field in sixth partition entry.
    assets_entry=0x8000+5*32
    struct.pack_into('<I',drift,assets_entry+8,0x700000)
    assert layout_kind(parse_parts(bytes(drift))) == 'unsupported'

    print('INSTALLER LOGIC SIMULATION: OK')
    print(' - stock XiaoZhi v2 layout: OK')
    print(' - XiaoZhi Care layout: OK')
    print(' - ESP app descriptor/version/project: OK')
    print(' - exact SKU bounded to real image: OK')
    print(' - mixed-profile OTA ambiguity detection: OK')
    print(' - partition drift rejection: OK')

if __name__ == '__main__':
    main()
