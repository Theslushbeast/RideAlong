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
BR_RIDE, BR_OFF, BR_FRONT, BR_SHOULDERS = SELF | 0x804, SELF | 0x805, SELF | 0x806, SELF | 0x807
T_RIDE, T_OFF, T_FRONT, T_SHOULDERS = SELF | 0x810, SELF | 0x811, SELF | 0x812, SELF | 0x813
G_BACK, G_FRONT, G_SHOULDERS = SELF | 0x809, SELF | 0x80A, SELF | 0x80B  # per-seat dialogue switches (set by the DLL)
CURRENT_FOLLOWER = 0x0005C84E  # Skyrim.esm CurrentFollowerFaction
# 0.4: she rides your horse and you ride behind her ("Take me somewhere." -> a destination)
BR_TRAVEL, BR_STOP, G_TRAVEL = SELF | 0x808, SELF | 0x80D, SELF | 0x80C
T_TRAVEL, T_STOP = SELF | 0x814, SELF | 0x815
# destinations grouped by hold: "Where to?" lists the holds, each hold lists its towns.
# Order = DESTINATIONS in main.cpp (topic 0x820 + index); MAP is last.
HOLDS = [('Whiterun Hold', ['Whiterun', 'Riverwood', 'Rorikstead']),
         ('Haafingar', ['Solitude', 'Dragon Bridge']),
         ('Eastmarch', ['Windhelm', 'Kynesgrove', 'Darkwater Crossing', 'Mixwater Mill']),
         ('The Rift', ['Riften', 'Ivarstead', "Shor's Stone", 'Heartwood Mill']),
         ('The Reach', ['Markarth', 'Karthwasten', 'Old Hroldan', 'Salvius Farm', "Soljund's Sinkhole", 'Kolskeggr Mine']),
         ('Falkreath Hold', ['Falkreath', 'Helgen', 'Half-Moon Mill', 'Lakeview Manor']),
         ('Hjaalmarch', ['Morthal', 'Stonehills', 'Windstad Manor']),
         ('The Pale', ['Dawnstar', 'Nightgate Inn', 'Heljarchen Hall']),
         ('Winterhold', ['Winterhold'])]
DESTS = [t for _, towns in HOLDS for t in towns] + ['MAP']
assert len(DESTS) <= 32
T_DEST = [SELF | (0x820 + i) for i in range(len(DESTS))]
T_HOLD = [SELF | (0x816 + i) for i in range(len(HOLDS))]  # 0x816..0x81E
DRIVER_RANK = 20

# CTDA: type/op, comparison value, function, params, run on subject
# op: 0x00 ==, 0x20 !=, 0x40 >, 0x60 >=, 0x80 <, 0xA0 <=
def ctda(func, p1, value, op=0x00, run=0):  # op | 0x01 = OR with the next condition; run 1 = on the player (target)
    return struct.pack('<B3sfHHIIIIi', op, b'\0\0\0', value, func, 0, p1, 0, run, 0, -1)
GET_IN_FACTION, GET_FACTION_RANK, GET_GLOBAL, GET_VOICE, GET_PLAYER_TEAMMATE, IS_RIDING_MOUNT = 71, 73, 74, 426, 453, 327  # ids from CommonLib TESCondition.h

vg = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ride_voices.json')))
GROUPS = {
    'Ride': ['0x7a1eb', '0xd8db3', '0xd8db2', '0xd8db1', '0xd8db0', '0x7508e', '0x4c468', '0x7508f', '0xe36db', '0xe3e79',
             '0x7a519', '0xf11b1', '0x7a516', '0xe26f3', '0xe155c'],
    'Off': ['0x7a51b', '0x750a6', '0xe36df'],
}
GROUPS['Front'] = GROUPS['Ride']; GROUPS['Shoulders'] = GROUPS['Ride']  # same agreeing lines
FALLBACK = {'Ride': 'All right, I\'ll climb up behind you.', 'Off': 'All right, I\'ll get down.',
            'Front': 'All right, I\'ll sit in front of you.', 'Shoulders': 'All right, up I go.'}
PROMPT = {'Ride': 'Ride with me.', 'Off': 'You can get down now.', 'Front': 'Ride in front of me.', 'Shoulders': 'Ride on my shoulders.'}
TOPIC = {'Ride': T_RIDE, 'Off': T_OFF, 'Front': T_FRONT, 'Shoulders': T_SHOULDERS}
BRANCH = {'Ride': BR_RIDE, 'Off': BR_OFF, 'Front': BR_FRONT, 'Shoulders': BR_SHOULDERS}
SEATGLOBAL = {'Ride': G_BACK, 'Front': G_FRONT, 'Shoulders': G_SHOULDERS}

def base_conds(g):  # each condition is its own CTDA subrecord
    # a follower: a player teammate OR in CurrentFollowerFaction (follower frameworks such as NFF)
    c = (sub('CTDA', ctda(GET_GLOBAL, GLOBAL, 1.0)) + sub('CTDA', ctda(GET_PLAYER_TEAMMATE, 0, 1.0, 0x01)) +
         sub('CTDA', ctda(GET_IN_FACTION, CURRENT_FOLLOWER, 1.0)) +
         sub('CTDA', ctda(GET_IN_FACTION, FACTION, 1.0 if g == 'Off' else 0.0)))
    if g in SEATGLOBAL: c += sub('CTDA', ctda(GET_GLOBAL, SEATGLOBAL[g], 1.0))
    return c

next_info, next_flst = 0x840, 0x880
flst_of = {}  # voice line -> its FLST (Front/Shoulders reuse Ride's)
dial_blob = flst_blob = b''
nrec = 0
for g in ['Ride', 'Off', 'Front', 'Shoulders']:  # keep this order: earlier INFO ids stay the same
    infos = b''
    count = 0
    ids = [sid for sid in GROUPS[g] if vg['cov'][sid]]  # only lines some voice type actually recorded
    for n, sid in enumerate(ids):
        vts = [int(x, 16) for x in vg['cov'][sid]]
        if sid in flst_of: fl = flst_of[sid]
        else:
            fl = SELF | next_flst; next_flst += 1; flst_of[sid] = fl
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

# ---- 0.4: travel dialogue (INFOs from 0x900)
next_info = 0x900
def follower_conds():
    return (sub('CTDA', ctda(GET_GLOBAL, GLOBAL, 1.0)) + sub('CTDA', ctda(GET_GLOBAL, G_TRAVEL, 1.0)) +
            sub('CTDA', ctda(GET_PLAYER_TEAMMATE, 0, 1.0, 0x01)) + sub('CTDA', ctda(GET_IN_FACTION, CURRENT_FOLLOWER, 1.0)) +
            sub('CTDA', ctda(GET_IN_FACTION, FACTION, 0.0)))
def silent(text):
    trdt = struct.pack('<IiIB3sIB3s', 0, 50, 0, 1, b'\0\0\0', 0, 0, b'\0\0\0')
    return sub('TRDT', trdt) + sub('NAM1', z(text)) + sub('NAM2', z('')) + sub('NAM3', z(''))
def voiced_infos(group, conds, last_flags=0x1):
    global next_info, nrec
    out = b''; n = 0
    for sid in [sid for sid in GROUPS[group] if vg['cov'][sid]]:
        body = (sub('ENAM', struct.pack('<HH', 0x1 | 0x2, 0)) + sub('CNAM', b'\0') + sub('DNAM', struct.pack('<I', int(sid, 16))) +
                conds + sub('CTDA', ctda(GET_VOICE, flst_of[sid], 1.0)))
        out += rec('INFO', SELF | next_info, body); next_info += 1; n += 1; nrec += 1
    return out, n
def topic(fid, edid, prompt, branch, infos, count):
    global nrec
    d = (sub('EDID', z(edid)) + sub('FULL', z(prompt)) + sub('PNAM', struct.pack('<f', 50.0)) + sub('BNAM', struct.pack('<I', branch)) +
         sub('QNAM', struct.pack('<I', QUEST)) + sub('DATA', bytes(4)) + sub('SNAM', b'CUST') + sub('TIFC', struct.pack('<I', count)))
    nrec += 2
    return rec('DIAL', fid, d) + grup(fid, 7, infos)
# "Take me somewhere." -> "Where to?" + the destination choices (only on foot)
body = (sub('ENAM', struct.pack('<HH', 0, 0)) + sub('CNAM', b'\0') + b''.join(sub('TCLT', struct.pack('<I', t)) for t in T_HOLD + [T_DEST[-1]]) +
        silent('Where to?') + follower_conds() + sub('CTDA', ctda(IS_RIDING_MOUNT, 0, 0.0, 0x00, 1)))
dial_blob += topic(T_TRAVEL, 'RideAlong_TravelTopic', 'Take me somewhere on my horse.', BR_TRAVEL, rec('INFO', SELF | next_info, body), 1)
next_info += 1; nrec += 1
for h, (hold, towns) in enumerate(HOLDS):  # "Whiterun Hold." -> "Which town?" + its towns
    choices = [T_DEST[DESTS.index(t)] for t in towns]
    body = (sub('ENAM', struct.pack('<HH', 0, 0)) + sub('CNAM', b'\0') + b''.join(sub('TCLT', struct.pack('<I', t)) for t in choices) +
            silent('Where in ' + hold + '?' if len(towns) > 1 else 'All right.') + follower_conds())
    dial_blob += topic(T_HOLD[h], f'RideAlong_Hold{h}', f'{hold}.', BR_TRAVEL, rec('INFO', SELF | next_info, body), 1)
    next_info += 1; nrec += 1
for i, name in enumerate(DESTS):
    prompt = 'The place I marked on my map.' if name == 'MAP' else f'{name}.'
    reply = "All right. Hold on." if name == 'MAP' else f'All right. To {name}.'
    infos, n = voiced_infos('Ride', follower_conds())
    body = sub('ENAM', struct.pack('<HH', 0x1, 0)) + sub('CNAM', b'\0') + silent(reply) + follower_conds()
    infos += rec('INFO', SELF | next_info, body); next_info += 1; nrec += 1
    dial_blob += topic(T_DEST[i], f'RideAlong_Travel{"".join(c for c in name if c.isalnum())}', prompt, BR_TRAVEL, infos, n + 1)
# "Stop here." while she's driving (passenger faction rank 20)
stop_conds = sub('CTDA', ctda(GET_GLOBAL, GLOBAL, 1.0)) + sub('CTDA', ctda(GET_FACTION_RANK, FACTION, float(DRIVER_RANK)))
infos, n = voiced_infos('Off', stop_conds)
body = sub('ENAM', struct.pack('<HH', 0x1, 0)) + sub('CNAM', b'\0') + silent('All right.') + stop_conds
infos += rec('INFO', SELF | next_info, body); next_info += 1; nrec += 1
dial_blob += topic(T_STOP, 'RideAlong_StopTopic', "Stop here. Let's get off.", BR_STOP, infos, n + 1)

glob = rec('GLOB', GLOBAL, sub('EDID', z('RideAlong_DialogueEnabled')) + sub('FNAM', b'f') + sub('FLTV', struct.pack('<f', 1.0))); nrec += 1
for gid, name in [(G_BACK, 'Back'), (G_FRONT, 'Front'), (G_SHOULDERS, 'Shoulders'), (G_TRAVEL, 'Travel')]:
    glob += rec('GLOB', gid, sub('EDID', z(f'RideAlong_{name}SeatEnabled')) + sub('FNAM', b'f') + sub('FLTV', struct.pack('<f', 1.0))); nrec += 1
fact = rec('FACT', FACTION, sub('EDID', z('RideAlong_PassengerFaction')) + sub('FULL', z('Ride Along passenger')) + sub('DATA', struct.pack('<I', 0x1))); nrec += 1
quest = rec('QUST', QUEST, sub('EDID', z('RideAlong_DialogueQuest')) + sub('DNAM', bytes.fromhex('11003c000000000000000000')) +
            sub('NEXT', b'') + sub('ANAM', struct.pack('<I', 0))); nrec += 1
dlbr = b''
for g in ['Ride', 'Off', 'Front', 'Shoulders']:
    dlbr += rec('DLBR', BRANCH[g], sub('EDID', z(f'RideAlong_{g}Branch')) + sub('QNAM', struct.pack('<I', QUEST)) +
                sub('TNAM', struct.pack('<I', 0)) + sub('DNAM', struct.pack('<I', 1)) + sub('SNAM', struct.pack('<I', TOPIC[g]))); nrec += 1
for br, t, name in [(BR_TRAVEL, T_TRAVEL, 'Travel'), (BR_STOP, T_STOP, 'Stop')]:
    dlbr += rec('DLBR', br, sub('EDID', z(f'RideAlong_{name}Branch')) + sub('QNAM', struct.pack('<I', QUEST)) +
                sub('TNAM', struct.pack('<I', 0)) + sub('DNAM', struct.pack('<I', 1)) + sub('SNAM', struct.pack('<I', t))); nrec += 1

# the horse's travel package (Skyrim.esm "Travel" template 0x16FAA); the DLL writes the destination into its location
TRAVEL_PACK = SELF | 0x8F0
pk = (sub('EDID', z('RideAlong_HorseTravel')) +
      sub('PKDT', struct.pack('<IBBBBHH', 0x08042004, 0x12, 0, 2, 0, 0, 0)) +   # preferred speed, must complete, swim, no combat alert; run
      sub('PSDT', bytes.fromhex('ffff00ffff00000000000000')) + sub('PKCU', struct.pack('<III', 3, 0x00016FAA, 3)) +
      sub('ANAM', z('Location')) + sub('PLDT', struct.pack('<IiI', 12, 0, 500)) +  # near self until the DLL sets it
      sub('ANAM', z('Bool')) + sub('CNAM', b'\0') + sub('ANAM', z('Bool')) + sub('CNAM', b'\0') +
      sub('UNAM', b'\x00') + sub('UNAM', b'\x02') + sub('UNAM', b'\x04') + sub('XNAM', b'\x05') +
      b''.join(sub(t, b'') + sub('INAM', bytes(4)) + sub('PDTO', bytes(8)) for t in ('POBA', 'POEA', 'POCA')))
pack = rec('PACK', TRAVEL_PACK, pk); nrec += 1
body = (grup('GLOB', 0, glob) + grup('FACT', 0, fact) + grup('PACK', 0, pack) + grup('DIAL', 0, dial_blob) + grup('QUST', 0, quest) +
        grup('FLST', 0, flst_blob) + grup('DLBR', 0, dlbr))
nrec += 7
hdr = b''.join([  # HEDR 1.70: loads in Skyrim VR as well
    sub('HEDR', struct.pack('<fII', 1.70, nrec, 0x900)), sub('CNAM', z('Clyde')),
    sub('SNAM', z('Ride Along: "Ride with me." follower dialogue and the passenger faction. Optional - the hotkey works without it.')),
    sub('MAST', z('Skyrim.esm')), sub('DATA', struct.pack('<Q', 0)),
])
tes4 = rec('TES4', 0, hdr, flags=0x200)  # ESL flag
out = sys.argv[1] if len(sys.argv) > 1 else 'RideAlong.esp'
open(out, 'wb').write(tes4 + body)
print(out, len(tes4 + body), 'bytes,', nrec, 'records; last info', hex(next_info - 1), 'flst 0x880..', hex(next_flst - 1))
