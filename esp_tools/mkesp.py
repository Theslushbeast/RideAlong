# Builds RideAlong.esp (ESL-flagged): the "Ride with me." / "You can get down now." follower dialogue,
# the passenger faction (for OAR conditions) and the global that switches the dialogue on and off.
# Responses share vanilla follower lines (no new audio); voice types without one get a silent text line.
import struct, json, os, sys

def sub(sig, data): return sig.encode() + struct.pack('<H', len(data)) + data
def z(s): return s.encode('cp1252') + b'\0'
def rec(sig, fid, body, flags=0): return sig.encode() + struct.pack('<IIIIHH', len(body), flags, fid, 0, 44, 0) + body
def grup(label, gtype, content):
    lab = label.encode() if isinstance(label, str) else struct.pack('<I', label)
    return b'GRUP' + struct.pack('<I', 24 + len(content)) + lab + struct.pack('<iHHHH', gtype, 0, 0, 44, 0) + content

SELF = 0x01000000
QUEST, FACTION, GLOBAL = SELF | 0x800, SELF | 0x802, SELF | 0x803
BR_RIDE, BR_OFF = SELF | 0x804, SELF | 0x805
T_RIDE, T_OFF = SELF | 0x810, SELF | 0x811
CURRENT_FOLLOWER = 0x0005C84E  # Skyrim.esm CurrentFollowerFaction

# CTDA: type/op, comparison value, function, params, run on subject
# op: 0x00 ==, 0x20 !=, 0x40 >, 0x60 >=, 0x80 <, 0xA0 <=
def ctda(func, p1, value, op=0x00):
    return struct.pack('<B3sfHHIIIIi', op, b'\0\0\0', value, func, 0, p1, 0, 0, 0, -1)
GET_IN_FACTION, GET_FACTION_RANK, GET_GLOBAL, GET_VOICE, GET_PLAYER_TEAMMATE = 71, 73, 74, 426, 453  # ids from CommonLib TESCondition.h

vg = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ride_voices.json')))
GROUPS = {
    'Ride': ['0x7a1eb', '0xd8db3', '0xd8db2', '0xd8db1', '0xd8db0', '0x7508e', '0x4c468', '0x7508f', '0xe36db', '0xe3e79',
             '0x7a519', '0xf11b1', '0x7a516', '0xe26f3', '0xe155c'],
    'Off': ['0x7a51b', '0x750a6', '0xe36df'],
}
FALLBACK = {'Ride': 'All right, I\'ll climb up behind you.', 'Off': 'All right, I\'ll get down.'}
PROMPT = {'Ride': 'Ride with me.', 'Off': 'You can get down now.'}
TOPIC = {'Ride': T_RIDE, 'Off': T_OFF}
BRANCH = {'Ride': BR_RIDE, 'Off': BR_OFF}

def base_conds(g):  # each condition is its own CTDA subrecord
    return (sub('CTDA', ctda(GET_GLOBAL, GLOBAL, 1.0)) + sub('CTDA', ctda(GET_PLAYER_TEAMMATE, 0, 1.0)) +
            sub('CTDA', ctda(GET_IN_FACTION, FACTION, 0.0 if g == 'Ride' else 1.0)))

next_info, next_flst = 0x840, 0x880
dial_blob = flst_blob = b''
nrec = 0
for g in ['Ride', 'Off']:
    infos = b''
    count = 0
    ids = [sid for sid in GROUPS[g] if vg['cov'][sid]]  # only lines some voice type actually recorded
    for n, sid in enumerate(ids):
        vts = [int(x, 16) for x in vg['cov'][sid]]
        fl = SELF | next_flst; next_flst += 1
        flst_blob += rec('FLST', fl, sub('EDID', z(f'RideAlong_V{g}_{int(sid, 16):06X}')) + b''.join(sub('LNAM', struct.pack('<I', v)) for v in vts)); nrec += 1
        flags = 0x1 | 0x2 | (0x20 if n == len(ids) - 1 else 0)  # goodbye, random (last one: random end)
        body = (sub('ENAM', struct.pack('<HH', flags, 0)) + sub('CNAM', b'\0') + sub('DNAM', struct.pack('<I', int(sid, 16))) +
                base_conds(g) + sub('CTDA', ctda(GET_VOICE, fl, 1.0)))
        infos += rec('INFO', SELF | next_info, body); next_info += 1; count += 1; nrec += 1
    # silent fallback for every other voice type
    trdt = struct.pack('<IiIB3sIB3s', 0, 50, 0, 1, b'\0\0\0', 0, 0, b'\0\0\0')
    body = (sub('ENAM', struct.pack('<HH', 0x1, 0)) + sub('CNAM', b'\0') + sub('TRDT', trdt) + sub('NAM1', z(FALLBACK[g])) +
            sub('NAM2', z('')) + sub('NAM3', z('')) + base_conds(g))
    infos += rec('INFO', SELF | next_info, body); next_info += 1; count += 1; nrec += 1
    dial = (sub('EDID', z(f'RideAlong_{g}Topic')) + sub('FULL', z(PROMPT[g])) + sub('PNAM', struct.pack('<f', 50.0)) +
            sub('BNAM', struct.pack('<I', BRANCH[g])) + sub('QNAM', struct.pack('<I', QUEST)) + sub('DATA', bytes(4)) +
            sub('SNAM', b'CUST') + sub('TIFC', struct.pack('<I', count)))
    dial_blob += rec('DIAL', TOPIC[g], dial) + grup(TOPIC[g], 7, infos); nrec += 2

glob = rec('GLOB', GLOBAL, sub('EDID', z('RideAlong_DialogueEnabled')) + sub('FNAM', b'f') + sub('FLTV', struct.pack('<f', 1.0))); nrec += 1
fact = rec('FACT', FACTION, sub('EDID', z('RideAlong_PassengerFaction')) + sub('FULL', z('Ride Along passenger')) + sub('DATA', struct.pack('<I', 0x1))); nrec += 1
quest = rec('QUST', QUEST, sub('EDID', z('RideAlong_DialogueQuest')) + sub('DNAM', bytes.fromhex('110000000000000000000000')) +
            sub('NEXT', b'') + sub('ANAM', struct.pack('<I', 0))); nrec += 1
dlbr = b''
for g in ['Ride', 'Off']:
    dlbr += rec('DLBR', BRANCH[g], sub('EDID', z(f'RideAlong_{g}Branch')) + sub('QNAM', struct.pack('<I', QUEST)) +
                sub('TNAM', struct.pack('<I', 0)) + sub('DNAM', struct.pack('<I', 1)) + sub('SNAM', struct.pack('<I', TOPIC[g]))); nrec += 1

body = (grup('GLOB', 0, glob) + grup('FACT', 0, fact) + grup('DIAL', 0, dial_blob) + grup('QUST', 0, quest) +
        grup('FLST', 0, flst_blob) + grup('DLBR', 0, dlbr))
nrec += 6
hdr = b''.join([
    sub('HEDR', struct.pack('<fII', 1.71, nrec, 0x900)), sub('CNAM', z('Clyde')),
    sub('SNAM', z('Ride Along: "Ride with me." follower dialogue and the passenger faction. Optional - the hotkey works without it.')),
    sub('MAST', z('Skyrim.esm')), sub('DATA', struct.pack('<Q', 0)),
])
tes4 = rec('TES4', 0, hdr, flags=0x200)  # ESL flag
out = sys.argv[1] if len(sys.argv) > 1 else 'RideAlong.esp'
open(out, 'wb').write(tes4 + body)
print(out, len(tes4 + body), 'bytes,', nrec, 'records; infos 0x840..', hex(next_info - 1), 'flst 0x880..', hex(next_flst - 1))
