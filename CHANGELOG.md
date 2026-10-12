Ride Along - Changelog
======================

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
