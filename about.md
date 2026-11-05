# Auto Trajectory v0.5.4

A Geometry Dash 2.2081 / Geode v5.11.2 mod with predictive trajectory and
experimental auto-play controls. This version fixes a Cube planner that could defer a safe jump indefinitely,
keeps Tap to one rendered frame, makes Ball and Robot check viable immediate
actions before delaying, tightens orb activation to simulated Jump input, and
adds earlier one/two-correction patterns to the optional Wave planner.

The bundled offline tests use a simplified physics harness. They do not replace
an Android Geode SDK build or in-game verification. The predictive Wave planner
is opt-in because its wider search is several times more expensive than the
classic controller and still costs substantially more simulation work and can fail other corridor types.
