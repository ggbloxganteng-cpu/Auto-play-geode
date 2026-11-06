# Auto Trajectory

Geode mod for Geometry Dash **2.2081** (Geode **v5.11.2**) — **v0.8.0-beta.1**.

## v0.8.0-beta.1 — per-level learning and interaction fixes

- **Dash Orb hold timing** — the hold-duration clock now starts only after the hidden/live player is confirmed in `m_isDashing`; a short activation watchdog prevents an indefinite hold when the orb did not activate. The timer is no longer consumed while waiting for the Dash Orb to start.
- **Spider Orb interaction path** — Spider Orb (object ID `3004`) is tracked separately from normal jump orbs. The simulator records its contact window and confirms activation through the Spider-specific dash/teleport effect path (or the normal ring activation hook when the game uses it). The actual destination and collision result still depend on the game's live spider physics; offline tests cannot prove full runtime parity.
- **Per-level strategy profile** — each level independently samples Ultra Low, Low, Balanced and High planning depth, records progress and prediction misses, then prefers the profile that has performed best on that level. Profiles are persisted by level ID/name.
- **Device Budget stays separate** — Device Budget and Adaptive Budget continue to control background workload/frame-time behavior. The level learner controls planning horizon; a level's best strategy is not inferred from device capability.
- **Death learning** — after an Auto Play death, the mod stores progress, selected profile, last prediction snapshot and nearby interaction evidence. Logs classify likely missed orb/Spider Orb/Dash Orb, prediction mismatch, or no safe route, with a suggested next diagnostic step. This is evidence-based heuristic feedback, not guaranteed identification of every physics cause.
- **Tests** — added portable tests for Spider Orb evidence and per-level profile persistence/selection.

## v0.7.0-beta.1 — Dash Orb support

- **Dash Orb planning** — recognizes the green Dash Orb (1704) and pink Gravity Dash Orb (1751), distinguishes them from ordinary jump orbs, and schedules a frame-aligned hold at the predicted contact window.
- **Real activation confirmation** — a Dash Orb candidate is accepted only when the simulated player enters the game's `m_isDashing` state. The planner tests finite hold durations and continuously re-plans while a dash is active, releasing when that is the safer path or when the dash ends.
- **Separate diagnostics** — debug logging now reports Dash Orb contact, activation and active-dash state separately from ordinary orb contacts.
- **Version metadata** — uses the valid Geode prerelease form `v0.7.0-beta.1`; the old `v0.7.0-D` suffix was not a valid prerelease label.


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
- **Orbs and pads** — ordinary jump orbs use frame-aligned tap planning; Dash Orbs use a sustained hold whose timer starts after confirmed dash activation; Spider Orbs are detected by runtime type or object ID `3004` and tracked through their teleport effect. Common pads are included in the predictive collision pass. Exact nearest-surface Spider collision still needs live in-game verification.
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
- Dash Orb planning relies on the clone reaching the game's real dash state. S-block timing, custom/event-linked behavior and unusual trigger chains may still differ from live gameplay.
- Spider Orb planning recognizes the interaction and listens for the Spider-specific teleport effect, but predicting the exact nearest valid surface requires the game's full live collision environment. Runtime verification is still required.
- Per-level death learning uses progress and recent event telemetry. It cannot yet infer every collision/trigger cause, and a higher progress score on one attempt does not prove the selected preset alone caused the improvement.
- The floating panel is a lightweight touch-drag panel, not a full QOLMod/Eclipse-style tabbed settings framework. Detailed numeric tuning remains in Geode settings.
- The included offline physics is a toy harness. In-game behavior must still be tested on actual Geometry Dash 2.2081.


## v0.7.0 performance update

The floating panel now includes a device frame-budget preset and adaptive-budget controls. Per-level planning depth is learned separately.
Device Budget and Adaptive Budget tune bounded background work, UI refresh and jump/frame-hitch deferral. The level-specific strategy profile selects the planning horizon independently. Auto Play's live decision path remains active. The current full-level object scan is still synchronous, so this is not yet a complete incremental scanner/global planner release.
