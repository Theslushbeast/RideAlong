# Ride Along - builds the climb-on / climb-off clips for the OAR "Getting on" / "Getting off" folders from YOUR copy of
# Skyrim (Data\Skyrim - Animations.bsa). The game's mount/dismount clips are paired (rider + horse in one file); this
# keeps only the rider's 99 tracks (NPC Root .. Camera Control, the human skeleton's order) so they play on a passenger.
#
#   python make_climb_clips.py "C:\...\Skyrim Special Edition\Data" ..\oar\RideAlongSeats
#
# Needs Python 3 and the lz4 module (pip install lz4). Only the hkx files are written; nothing in the game is changed.
import os, struct, sys
import lz4.frame

PAIRED = {'Getting on': r'meshes\actors\sharedkillmoves\human&horse\paired_mount.hkx',
          'Getting off': r'meshes\actors\sharedkillmoves\human&horse\paired_dismount.hkx'}
CLIPS = ['idle', 'idle_head1', 'idle_head2', 'idle_head3', 'idle_head4', 'idle_headshake', 'idle_pet', 'idle_rearup',
         'idle_tail1', 'idle_tail2', 'idle_tail3', 'idle_tail4', 'idle_tail5', 'idle_tail6', 'jumpfall', 'jumpfall_sprint',
         'jumpland', 'jumpland_sprint', 'jumpstart', 'jumpstart_sprint', 'recoil', 'runforward', 'runforwardl', 'runforwardr',
         'sprintforward', 'staggerbacklarge', 'staggerbacksmall', 'staggerforward', 'swimf', 'trotforward', 'trotforwardl',
         'trotforwardr', 'turncannedl180', 'turncannedl90', 'turncannedr180', 'turncannedr90', 'turnloopingl', 'turnloopingr',
         'walkforward']  # every horse_rider clip the passenger's graph can be in


def bsa_read(bsa, wanted):  # BSA v105 (SSE)
    f = open(bsa, 'rb')
    magic, ver, off, flags, nfold, nfile, folen, filen, fflags = struct.unpack('<4sIIIIIIII', f.read(36))
    f.seek(off)
    folds = [struct.unpack('<QIIQ', f.read(24)) for _ in range(nfold)]
    entries = []
    for _, cnt, _, _ in folds:
        n = f.read(1)[0]
        folder = f.read(n)[:-1].decode()
        for _ in range(cnt):
            _, size, foff = struct.unpack('<QII', f.read(16))
            entries.append((folder, size, foff))
    names = f.read(filen).split(b'\0')
    out = {}
    for (folder, size, foff), nm in zip(entries, names):
        path = (folder + '\\' + nm.decode()).lower()
        if path not in wanted: continue
        comp = bool(flags & 4) ^ bool(size & 0x40000000)
        size &= 0x3FFFFFFF
        f.seek(foff)
        if flags & 0x100:
            n = f.read(1)[0]; f.read(n); size -= n + 1
        if comp:
            f.read(4); out[path] = lz4.frame.decompress(f.read(size - 4))
        else:
            out[path] = f.read(size)
    return out


class Pack:  # hk_2010 64-bit packfile: data section, local fixups, objects
    def __init__(s, b):
        s.b = b
        secs = []
        for i in range(3):
            o = 0x40 + i * 0x30
            secs.append(struct.unpack_from('<7I', b, o + 20))
        s.secs = secs
        start, lf, gf, vf, ex = secs[2][:5]
        s.ds, s.lf, s.gf = start, lf, gf
        s.local = {}
        for o in range(start + lf, start + gf, 8):
            a, d = struct.unpack_from('<ii', b, o)
            if a != -1: s.local[a] = d
        cn = secs[0][0]
        s.objs = []
        for o in range(start + vf, min(start + ex, len(b)) - 11, 12):
            a, _, d = struct.unpack_from('<iii', b, o)
            if a != -1: s.objs.append((a, b[cn + d:b.index(b'\0', cn + d)].decode()))

    def find(s, n): return [a for a, c in s.objs if c == n][0]
    def ptr(s, off): return s.local.get(off)
    def u32s(s, off):
        n = struct.unpack_from('<i', s.b, s.ds + off + 8)[0]; p = s.ptr(off)
        return [struct.unpack_from('<I', s.b, s.ds + p + 4 * i)[0] for i in range(n)] if p is not None else []
    def cstr(s, off):
        p = s.ptr(off); o = s.ds + p
        return s.b[o:s.b.index(b'\0', o)].decode()


RSZ, RAL = [4, 5, 6, 3, 2, 16], [4, 1, 2, 1, 2, 4]
def al(o, base, a): r = (o - base) % a; return o if r == 0 else o + a - r
def vec(b, o, base, types, q):
    sp = [(types >> (4 + i)) & 1 for i in range(3)]; st = [(types >> i) & 1 for i in range(3)]
    if any(sp):
        n, deg = struct.unpack_from('<HB', b, o); o += 3 + n + deg + 2; o = al(o, base, 4)
        o += sum(8 if sp[i] else 4 if st[i] else 0 for i in range(3))
        o += (n + 1) * sum(sp) * (1 if q == 0 else 2)
    else:
        o += 4 * sum(st)
    return al(o, base, 4)
def rot(b, o, base, types, q):
    if types & 0xF0:
        n, deg = struct.unpack_from('<HB', b, o); o += 3 + n + deg + 2; o = al(o, base, RAL[q]); o += (n + 1) * RSZ[q]
    elif types & 0x0F:
        o = al(o, base, RAL[q]) + RSZ[q]
    return al(o, base, 4)


def rider_only(data):
    p = Pack(data); b = bytearray(data); d = p.ds
    a = p.find('hkaSplineCompressedAnimation')
    nt, nf = struct.unpack_from('<ii', data, d + a + 0x18)
    nblocks = struct.unpack_from('<i', data, d + a + 0x3C)[0]
    boffs = p.u32s(a + 0x58)
    dbuf = d + p.ptr(a + 0x98); dsize = struct.unpack_from('<i', data, d + a + 0xA0)[0]
    ap = p.ptr(a + 0x28)
    names = [p.cstr(ap + i * 0x18) for i in range(nt)]
    first, last = names.index('NPC Root [Root]'), names.index('Camera Control')
    keep = list(range(first, last + 1))
    assert len(keep) == 99 and nf == 0, (len(keep), nf)
    new = bytearray(); nb, fb = [], []
    for blk in range(nblocks):
        base = dbuf + boffs[blk]
        o = al(base + 4 * nt + nf, base, 4); starts = []
        for t in range(nt):
            qt, pt, rt, stp = data[base + 4 * t:base + 4 * t + 4]
            starts.append(o - base)
            o = vec(data, o, base, pt, qt & 3); o = rot(data, o, base, rt, (qt >> 2) & 0xF); o = vec(data, o, base, stp, (qt >> 6) & 3)
        starts.append(o - base)
        nb.append(len(new)); blk_ = bytearray()
        for t in keep: blk_ += data[base + 4 * t:base + 4 * t + 4]
        while len(blk_) % 4: blk_ += b'\0'
        blk_ += data[base + starts[first]:base + starts[last + 1]]
        fb.append(len(blk_))
        while len(blk_) % 16: blk_ += b'\0'
        new += blk_
    assert len(new) <= dsize
    b[dbuf:dbuf + len(new)] = new
    struct.pack_into('<i', b, d + a + 0x18, 99)
    struct.pack_into('<i', b, d + a + 0x44, 4 * 99 + nf)
    for i, v in enumerate(nb): struct.pack_into('<I', b, d + p.ptr(a + 0x58) + 4 * i, v)
    for i, v in enumerate(fb): struct.pack_into('<I', b, d + p.ptr(a + 0x68) + 4 * i, v)
    struct.pack_into('<i', b, d + a + 0x78 + 8, 0)       # no per-track offsets, like solo clips
    struct.pack_into('<i', b, d + a + 0x98 + 8, len(new))
    struct.pack_into('<i', b, d + a + 0x28 + 8, 99)      # annotation tracks
    bind = p.find('hkaAnimationBinding')
    for o in range(d + p.lf, d + p.gf, 8):
        src, _ = struct.unpack_from('<ii', b, o)
        if src == a + 0x28: struct.pack_into('<i', b, o + 4, ap + first * 0x18)
        if src == bind + 0x10: struct.pack_into('<i', b, o + 4, p.ptr(ap + first * 0x18))
    return bytes(b)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__ or 'usage: make_climb_clips.py <Skyrim Data folder> <oar\\RideAlongSeats folder>')
    data_dir, seats = sys.argv[1], sys.argv[2]
    got = bsa_read(os.path.join(data_dir, 'Skyrim - Animations.bsa'), set(PAIRED.values()))
    for folder, path in PAIRED.items():
        clip = rider_only(got[path])
        out = os.path.join(seats, folder, 'horse_rider'); os.makedirs(out, exist_ok=True)
        for n in CLIPS:
            open(os.path.join(out, n + '.hkx'), 'wb').write(clip)
        print(f'{folder}: {len(CLIPS)} clips written to {out}')
