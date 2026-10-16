Ride Along - Changelog
======================

v0.4.0
------
New
- She rides, you ride behind her. Ask a follower "Take me somewhere on my horse." (on foot, near your horse), pick a
  hold, then a place. She takes the saddle, you sit behind her, and your horse rides the roads there on its own -
  no fast travel, no loading screen.
  Destinations (31): the nine hold capitals (the walled cities: their stables), Riverwood, Rorikstead, Dragon Bridge,
  Kynesgrove, Darkwater Crossing, Mixwater Mill, Ivarstead, Shor's Stone, Heartwood Mill, Karthwasten, Old Hroldan,
  Salvius Farm, Soljund's Sinkhole, Kolskeggr Mine, Helgen, Half-Moon Mill, Stonehills, Nightgate Inn, the three
  Hearthfire homes (Lakeview Manor, Windstad Manor, Heljarchen Hall - once they're on your map), and the place you
  marked on your map.
  Get down on the way with your Ride Along key, or tell her "Stop here. Let's get off."
  If you're attacked on the way, you both get down (or keep riding - your choice).
  A horse that gets stuck is given its route again, then helped along a little; if it still can't get through,
  you both get down.
  Needs PapyrusUtil SE (the horse's travel order goes through it). Everything else in Ride Along works without it.
  Settings (SKSE Menu Framework / RideAlong.ini [Travel]): the dialogue line on/off, horse speed (walk, trot, run),
  what happens if you're attacked, arrival distance, her seat in the saddle.

Fixed
- Climb-on and climb-off animations stretched the passenger's body ("spaghetti"): they were the game's paired
  rider-and-horse clips, whose horse tracks drove the passenger's bones. They are now rider-only clips, and the
  passenger starts them where the game would (beside the horse for climbing on, on the saddle for climbing off).

v0.3.0
------
New
- Four passengers on one horse: two behind you, one in front of you, one on your shoulders.
  Dialogue: "Ride with me." (behind you), "Ride in front of me.", "Ride on my shoulders.", "You can get down now."
  Each seat can be switched on or off (SKSE Menu Framework or RideAlong.ini [Seats]); a switched-off seat's line goes away.
  The key seats the nearest follower in the next free seat; once nobody else can get on, it lets everyone down.
  Front and shoulder animations made for Ride Along by a member of the Nexus community.
- Climbing on and off: passengers behind you play the game's own mount and dismount animations (Ride Along - Seats, OAR).
- Talk while riding (option): switch it off and passengers have no prompt, so from the saddle you can open gates and
  talk to people. Let them off with the key or "Everyone get down now" in the menu.
- Passenger weapons (option): kept sheathed, or bows and crossbows only (melee weapons go back when they get off).
- "Everyone get down now" button in the menu.

Fixed
- Followers from follower frameworks (Nether's Follower Framework and others) didn't get the "Ride with me." line:
  any follower in CurrentFollowerFaction now counts, not only player teammates.
- Your melee attacks could hit the passenger behind you (with or without Precision): attacks now pass through
  passengers (option, on by default).
- Passengers drawing and sheathing their weapon over and over: their weapon draw is blocked while they ride.
- The dismount animation did nothing: it now uses the game's own climb-off animation.
- Photo Mode / free camera: passengers were left behind while the camera moved. They stay seated now, and the
  Ride Along key is ignored while the free camera is active.

Compatibility
- One DLL for Skyrim SE 1.5.97, AE 1.6.x (including 1.6.1170) and VR. Game-version-specific hooks are checked
  before they are installed and skipped (with a log line) if the game code doesn't match.
- VR: untested. Settings are in RideAlong.ini (SKSE Menu Framework isn't available in VR). RideAlong.esp now uses
  header version 1.70 so VR loads it.
- Princess Seat add-on: update it too - its OAR condition now checks the passenger's seat (faction rank 1).

v0.2.1
------
Fixed
- Princess Seat: passengers wearing high heels had their feet sunk into the ground after getting off.
  The side-saddle seat has no get-off animation, so high-heels mods (HH_OFFSET) never restored the heel height.
  Her shoes are now taken off and put back on right after she's set down, which makes heels mods re-apply it.
  New setting: PrincessRefreshShoes (RideAlong.ini [Princess], or the in-game menu). On by default.

v0.2.0
------
New
- Dialogue: ask any follower "Ride with me." and "You can get down now." (needs RideAlong.esp).
  Ask on foot and they climb on as soon as you mount.
- Princess Seat (optional add-on): female passengers sit side-saddle while you ride normally.
  Requires YY Anim Replacer - Princess' Horse Riding by yukl (animations not included - see the add-on's readme).
  Without those animations, passengers simply ride astride.
- Dismount animation: the passenger plays a get-off animation on the seat, then steps down
  on the horse's left side. Side-saddle passengers step straight down.
- Peaceful passenger: while riding, your passenger stays out of fights and keeps her weapon away,
  but her AI stays on, so you can still talk to her.
- Controller support: hold a button of your choice to seat or drop off a follower.
- In-game settings page in SKSE Menu Framework (Ride Along > Settings): hotkey, controller button,
  range, seat position, princess seat offsets and more. Saved back to RideAlong.ini.

Changed
- Removed "get off when combat starts" and the drive-by (passenger switching to a bow). Both were
  unreliable; the passenger now simply stays seated and out of the fight.
- Rebuilding from source no longer overwrites your RideAlong.ini.

Fixed
- Crash when a passenger got off (including in the middle of a fight). The get-off steps are now
  spread over a few frames.
- Passenger repeatedly drawing and sheathing her weapon during combat.

Settings added to RideAlong.ini
- [General] HotkeyEnabled, GamepadButton, GamepadHoldSeconds, DialogueEnabled, PeacefulPassenger
- [Dismount] DismountAnimation, DismountEvent, DismountSeconds
- [Princess] PrincessSeat, PrincessCopyPose, PrincessBackOffset, PrincessUpOffset, PrincessSideOffset,
  PrincessTurnDegrees, PrincessDismountEvent
- Removed: DismountInCombat

Updating from v0.1.0
- Install over the old version (replace files). Safe mid-playthrough.
- Your old RideAlong.ini works; missing settings use their defaults. Use the new ini or the
  in-game menu to see every option.
- Enable RideAlong.esp (ESL-flagged, no load order slot) for the dialogue options.

v0.1.0
------
- First release: a follower rides behind you on your horse (hotkey), with a smooth seated pose
  that follows the horse's movement.
