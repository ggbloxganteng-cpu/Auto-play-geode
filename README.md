# Auto Trajectory

Geode mod for Geometry Dash **2.2081** (Geode **v5.11.2**) — **v0.6.0**.

## v0.6.0 highlights

- **Level pre-scan** — when a level is loaded, the mod indexes its loaded `GameObject` collection and divides the horizontal route into four 25% zones. Each zone keeps counts for hazards, orbs, pads and portals. The floating panel shows the current percentage, zone and object count; entering a new zone logs its summary.
- **Checkpoint telemetry** — the panel and Geode log show the current 25% zone and its static object counts. The controller continues its normal live-state receding-horizon prediction every frame; the zone counts are currently informational and do not change planner scoring. The displayed route percentage is estimated from the player's horizontal position between the scan start and the furthest loaded object, not the game's official completion percentage.
- **Draggable / collapsible floating panel** — drag the header to move the panel; tap the header to collapse or expand it. Auto Play, Trajectory and Wave Planner remain reachable in-game. The existing `show-ui` setting hides the panel entirely.
- **Lighter Wave Experimental** — light profile caps the horizon at 108 physics substeps and four candidates (two straight paths plus two correction paths). An optional **Wave: Deeper Search** setting restores a six-candidate, 144-step search for harder corridors. Both profiles remain opt-in.
- **Source structure** — portable predictive logic is separated into `src/core/Simulation.hpp`; static zone scanning is in `src/core/LevelScan.hpp`; legacy Cocos2d-x touch-drag handling is in `src/ui/FloatingPanelTouch.hpp`; `src/main.cpp` contains Geode hooks and live integration.

## Important limitation: what the 25% scan does

The mod does **not** create four complete copies of the Geometry Dash level and run all of their physics in parallel. It scans the loaded object layout once, makes four static route summaries, and then continues simulating the hidden player from the actual live state as the player advances through each zone. Zone summaries are telemetry for now, not a future route optimizer.

That distinction is intentional. Orb activation, pads, moving objects, triggers, dual-player state, portal transitions and other effects can depend on what happened earlier. Running four clones against the same mutable object instances would let their activation state contaminate one another. A true parallel full-level simulator needs isolated copies of the dynamic game state and trigger graph, which is not implemented here.

## Existing gameplay systems

- **Auto Play** — hidden-player prediction for cube, robot, ball, ship, UFO, swing, wave and dual mode. Platformer levels are not supported.
- **Orbs and pads** — common ring/orb instances and pads are included in the predictive collision pass. Orb contact and real `ringJump()` activation are tracked separately. Immediate and short delayed click candidates are supported; custom/event-linked behavior is not fully emulated.
- **Portals** — gravity, speed and size portals are passed through the game's collision path for the hidden player. Mode, dual and teleport portals remain prediction boundaries.
- **Trajectory overlay** — green = press & hold; red = release. The overlay uses the same simulation engine as the bot.
- **Debug Log** — logs bot decisions and zone transitions when enabled.

## Wave Experimental benchmark

The light profile runs at most four candidate simulations over a 108-step horizon. The optional deep profile runs up to six candidates over 144 steps. Both start by testing the two straight input paths and only spend correction probes if both fail.

At lookahead 132 in the included randomized toy corridor harness:

| Corridor set | Classic | Light | Deep |
|---|---:|---:|---:|
| Single gaps, tight | 12/300 deaths | 84/300 | 12/300 |
| Single gaps, very tight | 146/300 | 108/300 | 40/300 |
| Two gaps, tight + wide | 0/300 | 0/300 | 0/300 |

Light generally used fewer simulation steps than Deep, but it performed worse on the ordinary tight set. Deep retained more search breadth and performed better in these synthetic layouts, at greater CPU cost. The light and deep profiles are therefore both optional; choose based on observed device performance and level type. These are synthetic tests, not Geometry Dash level results.

## Offline tests

Run:

```sh
cd tests
sh run.sh
```

The tests compile the same portable simulation header used by the mod, plus the level scanner. They cover toy Cube / Ship / Wave routes, dual mode, orb interaction tracking, portal boundaries, frame timing and checkpoint-zone indexing. They do **not** replace a Geode SDK build or in-game verification.

## Build with GitHub Actions

1. Push this folder to a GitHub repository.
2. The workflow in `.github/workflows/build.yml` runs on push or via `workflow_dispatch`.
3. Download the `auto-trajectory` artifact from the **Actions** tab.

Before publishing / using it yourself, change these placeholders in `mod.json`:

```json
"id": "yourname.auto-trajectory",
"developer": "yourname",
"links": { "source": "https://github.com/yourname/auto-trajectory" }
```

## Build locally

```sh
geode sdk install
export GEODE_SDK="$HOME/.geode/sdk"
# Android64
geode build -p android64
# Windows
geode build
```

## Known limitations

- Cube and Robot remain receding-horizon controllers, not complete multi-jump script solvers.
- Ball selects a single gravity-flip pulse per re-plan; it is not a long-sequence optimizer.
- Wave Experimental can still make poor decisions in some corridors; the four-candidate reduction lowers work but also reduces search breadth.
- The 25% scanner classifies loaded objects and reports zone composition. It does not simulate all future checkpoint zones simultaneously, change planner scoring based on zone counts, or recreate independent trigger/object-state graphs.
- Orb support focuses on standard ring instances. Custom/event-linked ring behavior, arbitrary group triggers and teleport-orb destination state may still be missed.
- The floating panel is a lightweight touch-drag panel, not a full QOLMod/Eclipse-style tabbed settings framework. Detailed numeric tuning remains in Geode settings.
- The included offline physics is a toy harness. In-game behavior must still be tested on actual Geometry Dash 2.2081.


## v0.7.0 performance update

The floating panel now includes performance preset and adaptive-budget controls.
Ultra Low/Low presets reduce predictive lookahead, visual trajectory refresh is throttled,
and jump/frame-hitch grace defers non-critical preview work. Auto Play's live decision
path remains active. The current full-level object scan is still synchronous, so this is
not yet a complete incremental scanner/global planner release.
