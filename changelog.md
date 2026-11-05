# v0.5.0

- Simulation now passes common jump pads and ring/orb object types into the
  collision engine instead of filtering them all out. Ring interactions are
  allowed on hidden clones; real-player jump counters and global level-effect
  triggers remain suppressed.
- Added per-run snapshots for activation flags on touched `GameObject` and
  `EffectGameObject` instances. The snapshots are restored before the simulation
  ends so a prediction does not consume the real orb, pad or portal.
- Dummy synchronization now copies jump-buffer, ring/pad, dash and last-portal
  state in addition to position, size, gravity and speed.
- Portal detection now uses player/object hitbox intersection instead of a loose
  centre-point distance. Gravity/speed portals are passed through the game's
  collision path; mode, size, dual and teleport portals end the prediction at
  their hitbox so the next frame can re-sync from the real player. Full
  prediction beyond these complex transitions is still unsupported.
- Reworked the optional wave path setting to test multi-switch sequences and
  schedule the first planned switch across frames. It remains off by default:
  randomized offline tests did not show a consistent survival improvement,
  and the experimental path costs several times more simulation work.
- Added an offline interaction-state restoration test and a multi-switch wave
  sequence test.

# v0.4.0

- Fixed auto play only jumping once at the Stereo Madness pillar staircase.
  Two causes, both reproduced in an offline physics model of that section:
  the first jump was always made at the very last moment, so the cube landed
  on the far edge of the 1-block pillar with no time to jump again; and the
  "wait" rule kept waiting even when a jump right now still worked. Jumps are
  now judged over the whole lookahead, so the bot picks a jump that also sets
  up the next obstacle, and it presses as soon as waiting one more frame would
  no longer work.
- Single taps are preferred; holding (bunny hop) is only used when no single
  tap gets through (pillar staircases). Isolated spikes are still one jump
  each, taken at the last safe moment.
- Auto Play now works in dual mode: a second hidden player is simulated and the
  shared input has to keep both players alive.
- Ship / UFO / swing: the controller now also tries short hold and release
  pulses instead of only hold-forever vs release-forever. Wave keeps its
  hold-vs-release controller.
- New experimental setting "Wave: Roomy Path": the wave tries several
  hold/release patterns and prefers the one with the most room around it
  (candidates that only fit exactly on the line are dropped first). Off by
  default: it costs about 3-7x more simulation time, and in the offline model
  it did not survive more courses than the default, so it is unproven in game.
- Portals (gravity, mode, size, dual, speed, teleport) end the prediction: what
  comes after them is not modelled, so the bot only plans up to the portal and
  re-plans once the real player has gone through.
- New "Debug Log" setting: logs the bot's decisions (position, mode, danger,
  chosen action) so problems in specific sections can be diagnosed.
- Auto Play Lookahead default is now 200 (max 480). An already saved value is
  kept, so raise it in the settings if you want the new default.
- Added tests/ (offline physics model used to reproduce and verify the above).

# v0.3.0

- Fixed auto play only jumping once at multi-pillar hold sections (e.g. the
  three-pillar part of Stereo Madness): the simulation validated tap/hold
  plans but the real input could be released by the next frame's decision
  while the sim assumed the button stayed down. The bot now executes the same
  plan the simulation validated - a validated tap stays held until the player
  has left the ground after touching it, and an in-flight tap is always
  finished before any new decision can release the button.
- Added real Tap as a bot action (press, auto-release once the jump has
  launched) instead of only press-and-hold or release.
- Added flying mode support (ship, swing, wave) for auto play: bang-bang
  hold/release control with hysteresis so the input does not flap every frame.

# v0.2.0

- Fixed the trajectory overlay drawing almost nothing: simulation now steps
  the player with the correct GD 2.2 substep delta (~0.25 at 240 physics
  ticks per second) instead of the raw frame delta.
- Fixed auto play never jumping: the lookahead now covers real time
  (~0.55 s by default) instead of a few milliseconds.
- Simulation is a faithful port of the Show Trajectory / Predictive Input
  Buffer style simulator: update -> checkCollisions per substep, collision
  logs and last-collision trackers reset, hitbox size synchronised, side
  effects (orbs, triggers, streak, jump counter) muted while simulating.
- Bot now shares the trajectory simulations, keeps the button held while
  holding is the longest-surviving option, releases otherwise, and gets a
  landing safety bonus for jumps that reach ground safely.
- Trajectory draws a marker where the simulated player crashes.
- Auto play is disabled in dual mode and platformer levels (sim only
  tracks player 1).
