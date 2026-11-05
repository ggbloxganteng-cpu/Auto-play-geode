# v0.7.0 — Adaptive Performance & Advanced Controls

- Added Ultra Low, Low, Balanced and High performance presets to the floating panel.
- Added Adaptive Budget and Jump Protection controls, plus live frame/budget telemetry.
- Jump edges start a short grace period that defers non-critical trajectory preview work.
- Frame hitches temporarily suspend preview work; Auto Play's live decision path and cached/current input handling remain active.
- Throttled trajectory preview refresh and skip redundant predictive passes when Auto Play is disabled.
- Capped simulation lookahead by performance preset and restrict deep Wave search to High/Custom outside jump grace.
- Expanded the draggable panel and updated its header touch region for the taller layout.
- Added portable scheduler tests to the offline test suite.

### Limitations
- Full-level scanning is still synchronous during level initialization.
- Global route planning and checkpoint-isolated physics simulation are not implemented yet.
- Static fingerprint cache and incremental-work helpers are included and tested but not yet connected to the live scanner/planner.
- This is not an Android-device performance guarantee; a Geode Android build and in-game profiling are still required.

# v0.6.0

- Split portable simulation logic into `src/core/Simulation.hpp` and added `src/core/LevelScan.hpp` for level object indexing.
- Added a four-zone (0/25/50/75/100%) static level pre-scan with per-zone hazard, orb, pad and portal counts.
- Added live zone/progress reporting to the in-game panel and Geode log. Zone summaries are informational; the existing controller continues to re-plan from the live player state and does not yet score routes using future zone counts.
- Replaced the fixed in-game control stack with a draggable, collapsible floating panel. Header drag moves it; header tap collapses or expands it. Touch handling is isolated in `src/ui/FloatingPanelTouch.hpp` and uses the Cocos2d-x 2.x targeted-touch delegate path.
- Added a lightweight Wave profile (four candidates / 108 steps) and an optional deep profile (six candidates / 144 steps). Light is cheaper but less reliable in some synthetic tight corridors; deep preserves more search breadth. The planner remains disabled by default.
- Updated offline tests to compile the actual simulation header and added level-zone scanner tests.
- The scanner is not a complete level clone: future dynamic object/trigger states are not simulated in parallel. The controller still uses live-state receding-horizon predictions.

# v0.5.6

- Reduce Wave experimental cost with a two-stage search: evaluate the two
  straight inputs first and skip correction simulations whenever either path
  survives the horizon. Only test four correction patterns if both straight
  paths fail; cap the horizon at 144 physics substeps and candidates at six.
- Add a compact in-game floating control stack for Auto Play, Trajectory and
  Wave Planner toggles. Detailed numeric settings remain in Geode settings.
- Keep Wave Planner opt-in; the lightweight randomized benchmark improves
  worst-case simulation work from roughly 7–8x to about 2.5–3.5x classic in
  this test harness, while the very-tight toy layouts show only a modest
  survival improvement. Real-level performance still needs device testing.
- Document staged 25% checkpoint planning as future work: current simulations
  use hidden player clones against the live level collision objects; they do
  not make a fully independent copy of every level object or run safely in
  parallel because simulation state and object activation are shared.

# v0.5.5

- Add orb hitbox-contact telemetry, separated from confirmed `ringJump()`
  activation so overlap alone is not treated as a successful click.
- Compare immediate and nearby frame-aligned delayed orb taps. Prefer the
  immediate click when it is equally safe; otherwise schedule the selected
  delayed click in live gameplay and cancel stale schedules if the release path
  can no longer reach the planned timing window.
- Continue hidden-clone prediction through mini/normal size portals using the
  game's collision path, allowing the post-size-transition Ship physics to be
  evaluated in narrow gaps. Mode, dual and teleport portals remain prediction
  boundaries.
- Preserve object activation state after each prediction and keep jump-pad
  handling unchanged. Add offline regression coverage for orb contact versus
  activation, immediate/delayed selection, scheduled clicks and size portals.

# v0.5.4

- Fix Cube's repeated-defer bug: it now tests Tap-now first and commits when
  that one-frame pulse survives the full prediction horizon. A later safe tap
  is considered only when Tap-now fails.
- Apply the same current-first rule to Robot finite holds and Ball gravity
  flips, preventing safe immediate actions from being postponed indefinitely.
- Keep Tap pulses to one rendered frame; this clears the old long lock-held
  behavior even when the player is airborne.
- Refine the optional Wave planner to include one-frame and two-frame early
  corrections. On the very-tight randomized test set, deaths fell from
  146/300 to 41/300 (lookahead 132) and 143/300 to 37/300 (lookahead 200),
  while simulation work rose to roughly 6–8 times the classic controller.
- Orb activation still requires simulated Jump input and checks per-player
  used flags. Custom/event-linked ring behavior remains a known limitation.
- Pad handling is unchanged.

# v0.5.3

- Fix Cube fallback choosing Hold even when Tap was the best surviving option.
- Replace the long 180-substep Tap lock with an exact one-rendered-frame pulse.
  Airborne taps now release normally instead of waiting for a grounded transition.
- Make simulated Tap use the same one-frame pulse for grounded jumps, airborne
  orb contacts, Ball gravity flips and Spider input.
- Require simulated Jump input to be down before a standard RingObject can be
  activated; respect per-player used flags to reduce false positives.
- Add a dedicated Ball planner that compares gravity-flip pulse timing rather
  than reusing Cube's jump controller.
- Add denser, shorter Robot hold durations for more precise jump-height choices,
  and re-plan immediately when a timed hold expires to avoid skipping a landing window.
- Replace fixed wave intervals with frame-aligned one/two-correction patterns,
  extend the planning horizon to 192 substeps. The planner remains opt-in due
  to its higher CPU cost and inconsistent randomized survival gains.
- Expand offline checks for airborne tap release, orb input gating and Ball edge pulses.
- Pad handling is unchanged.

# v0.5.2

- Check later tap windows before selecting an immediate Cube jump, reducing avoidable early jumps.
- Force a fresh press edge when Tap is selected while the bot still holds Jump, keeping live input aligned with the released-then-pressed simulation and allowing orb taps after a hold.
- Admit standard RingObject activation in the hidden clone even when the base layer rejects the unregistered dummy player; respect per-player orb-used flags and restore object state afterward.
- Treat teleport orbs as simulation boundaries rather than attempting to predict their linked destination.
- Keep timed variable holds limited to Robot mode; bounded Cube hold tests regressed randomized layouts and were not retained.
- Expand the offline controller harness to model fresh tap edges and timed Robot hold scheduling.

# v0.5.1

- Fixed a simulator/live-input timing mismatch: tap prediction now keeps the
  input through the same postUpdate frame boundary as the real controller.
  This matters especially for robot mode, where holding for even a few more
  physics steps changes jump height.
- Added robot-specific variable-height jump planning. It tests bounded,
  frame-aligned hold durations, waits when a later jump remains safe, and
  releases at the scheduled frame boundary so the next landing can be planned
  as a separate jump rather than leaving Jump held indefinitely.
- Orb detection now also checks the runtime `RingObject` class. This avoids
  losing ring/orb collisions when No-Touch rewrites `m_objectType` to
  `Decoration`; ordinary pad detection is unchanged.
- Added offline checks for Decoration-typed orb instances, tap release cadence,
  and finite robot hold duration.
- This is still a heuristic controller. Event-linked/custom orb effects and
  arbitrary group triggers remain muted during prediction.

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


## v0.7.0
- Adaptive performance scheduler and advanced floating controls.

## v0.7.0-D (development)
- Added bounded checkpoint-segment window and segment scoring primitives.
- Added a four-checkpoint beam-search global route planner that consumes measured per-segment simulation risk (not static object density).
- Replaced the old fixed Wave correction list with Wave Planner 2.0 candidate generation: frame-aligned one- and two-switch sequences, capped to 6 candidates in light mode and 12 in deep mode; straight paths are evaluated first.
- Added offline planning regression tests.
- Segment and global route planner primitives are currently not connected to live clone simulations; the global planner must not be presented as an active in-game route recommendation until that integration is completed.
