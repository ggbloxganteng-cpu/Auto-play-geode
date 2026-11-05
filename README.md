# Auto Trajectory

Geode mod for Geometry Dash **2.2081** (Geode **v5.11.2**) — v0.5.4.

- **Auto Play** — every frame it simulates the player's future on a hidden clone.
  Cube/spider use one-frame tap pulses; Cube now commits to an immediate tap
  when that tap already survives the full prediction window, rather than
  indefinitely deferring because a later tap also works. Airborne taps release
  at the next frame boundary. Ball has a dedicated gravity-flip pulse planner
  that checks the immediate flip before waiting. Robot tests finite hold
  durations immediately before considering a delayed jump. Ship, UFO and swing
  try short hold/release pulses; Wave has an optional earlier-correction
  predictive planner. Dual mode uses a second
  hidden player and requires the shared input to keep both players alive.
  Platformer levels are not supported.
- **Orbs and pads** — common jump pads and ring/orb instances are included in
  the predictive collision pass. Orb detection also checks the runtime
  `RingObject` class, because No-Touch can change the object's reported type to
  Decoration. A hidden clone is only allowed to activate a ring while its
  simulated Jump input is down, and per-player used flags are checked. This
  reduces false positives, but custom/event-linked ring behavior is not fully
  emulated. Activation flags on level objects are snapshotted and restored
  after each simulation. Global level-effect triggers remain muted to avoid
  changing the actual level during prediction.
- **Portals** — gravity and speed portals are passed through the game's normal
  collision path for the hidden player. Mode, size, dual and teleport portals
  are detected using hitbox intersection and end that prediction at the portal;
  the real player crosses it in the live game, then the next frame re-syncs from
  the real player's new state. The simulator does not predict the full section
  *after* those complex transitions.
- **Trajectory overlay** — green = press & hold now; red = release now. The
  overlay uses the same simulation engine as the bot. Tap decisions check the
  current pulse first; when it is not viable, they look for later press windows.
  A Tap creates a fresh input edge and lasts exactly one rendered frame,
  including when the player is airborne near an orb.
- **In-game UI** — toggle Auto Play and Trajectory from the play screen, plus
  the normal Geode mod settings panel.
- **Wave: Predictive Path Planner** — optional and disabled by default. It
  evaluates frame-aligned patterns with zero, one or two early input
  corrections. It reduced deaths in the very-tight randomized corridor set,
  but costs roughly 6–8 times more simulation work and does not improve every
  corridor type, so it remains disabled by default.
- **Debug Log** — logs the bot's current position, mode, predicted danger and
  selected action to the Geode log.

## Simulation notes

Geometry Dash 2.2 runs player physics at **240 substeps per second**. The mod
measures the actual per-substep delta from `PlayerObject::update` and advances a
hidden clone through `update(dt)` and `checkCollisions(dt)`. It copies jump-buffer,
size, gravity, dash and interaction state from the real player before each run.

Every simulated orb, pad or portal object that may change activation flags is
snapshotted and restored after the run. Visual trails, global effect triggers and
real-player jump counters remain muted. Complex mode/size/dual/teleport portals
are a prediction boundary: the clone stops at their hitbox and prediction
re-syncs from the real player on the next frame.

## Build with GitHub Actions

1. Push this folder to a GitHub repository.
2. The workflow in `.github/workflows/build.yml` runs automatically
   (`workflow_dispatch` also works).
3. Download the `auto-trajectory` artifact from the **Actions** tab. It contains
   the combined `.geode` output for Windows and Android32/64.

Before publishing / using it yourself, change these in `mod.json`:

```json
"id": "yourname.auto-trajectory",
"developer": "yourname",
"links": { "source": "https://github.com/yourname/auto-trajectory" }
```

## Install

**Android:** copy the `.geode` file to
`/storage/emulated/0/Android/media/com.geode.launcher/game/geode/mods/`

**Windows:** put it in `Geometry Dash/geode/mods/`.

## Build locally

```sh
geode sdk install
export GEODE_SDK="$HOME/.geode/sdk"
# Android64
geode build -p android64
# Windows
geode build
```

## Limitations and tuning

- Ball planning currently selects a single gravity-flip pulse per re-plan; it
  is not yet a full sequence optimizer for long multi-flip passages.
- The bot remains a heuristic receding-horizon controller, not a complete
  general-purpose level solver. Very tight wave corridors can still fail.
  The optional Wave planner improved the very-tight randomized test set, but
  costs roughly 6–8 times as many simulated steps per decision in that harness;
  it remains disabled by default.
- Robot holds are planned in frame-aligned chunks and then re-evaluated after
  each release/landing. This is not a full multi-jump script; unusual event
  triggers or tightly chained robot sections may still need manual input.
- Cube uses single-frame taps and continuous hold fallback, not a precomputed
  multi-hop script. The controller re-evaluates after each input edge; unusually
  dense consecutive jumps may still need better sequence planning.
- The predictive wave planner uses several times more simulation work than
  the classic controller. Its candidates are frame-aligned heuristics, not a guaranteed
  path search; difficult or very narrow corridors can still fail.
- Orb/pad support covers standard ring instances and common jump, gravity and
  spider pads. Teleport orbs are prediction boundaries because their linked
  destination is not simulated. Event-linked/custom orb effects, arbitrary
  group triggers, teleport destinations and complex post-portal sections are
  not fully modeled.
- The included offline tests compile the portable decision/simulation logic
  against a toy physics model. They do **not** replace a Geode SDK build or
  in-game verification.
