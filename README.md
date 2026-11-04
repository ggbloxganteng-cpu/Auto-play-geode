# Auto Trajectory

Geode mod for Geometry Dash **2.2081** (Geode **v5.11.2**).

- **Auto Play** — predictive jump bot. Every tick it simulates "do nothing" vs
  "jump now" on a hidden clone of the player and presses jump when staying on
  the ground is about to kill you.
- **Trajectory overlay** — draws two predicted paths: green = keep holding
  jump, red = release jump.
- **In-game UI** — two toggle buttons in the top-right corner plus the normal
  Geode mod settings panel.

## Build with GitHub Actions

1. Push this folder to a GitHub repository.
2. The workflow in `.github/workflows/build.yml` runs automatically
   (`workflow_dispatch` also works).
3. Download the `auto-trajectory` artifact from the **Actions** tab. It contains
   a combined `.geode` file for Windows + Android32/64.

Before publishing / using it yourself, change these in `mod.json`:

```json
"id": "yourname.auto-trajectory",
"developer": "yourname",
"links": { "source": "https://github.com/yourname/auto-trajectory" }
```

## Install

**Android:** copy the `.geode` file to
`/storage/emulated/0/Android/media/com.geode.launcher/game/geode/mods/`

**Windows:** put it in `Geometry Dash/geode/mods/`

## Build locally (optional)

```sh
geode sdk install
export GEODE_SDK="$HOME/.geode/sdk"
# Android64
geode build -p android64
# Windows
geode build
```

## Notes / limitations

- The bot is a simple receding-horizon heuristic, not a full TAS solver. It
  handles basic cube sections; ships/waves and tight timings will fail.
- Trajectory simulation only collides with solid, hazard and slope objects, so
  rings, pads, portals and triggers are **not** part of the prediction (this
  avoids activating them while simulating).
- Trajectory simulation costs `2 * trajectory-steps` physics steps per frame.
  Lower it if the game stutters on mobile.
