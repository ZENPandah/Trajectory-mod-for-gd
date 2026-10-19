# Trajectory Assist (Geode mod)

- **Trajectory preview**: grey line = where you go if you don't click, green line = where you go if you click right now.
- **Early-click autocorrect**: when you press jump, the mod simulates clicking now and clicking up to *Max Correction Delay* ticks later. If clicking now would die but a later tick survives, your click is held back until that tick. If you release before the delayed press fires, the release is shifted by the same amount.

By default autocorrect only works in **practice mode** so it can't touch normal runs or leaderboard attempts.

## Safe mode

With **Auto Safe Mode** on (default), safe mode engages as soon as autocorrect is active in a level, or the first time it delays a click. It stays on until you leave the level. While on, `GJGameLevel::savePercentage` is blocked (no best %, practice % or attempts saved) and `PlayLayer::levelComplete` runs in test mode so the completion isn't saved or submitted.

This is the mod's own safe mode. It can't toggle Mega Hack's, since Mega Hack has no public API.

## Build

1. Install the Geode SDK and CLI: https://docs.geode-sdk.org/getting-started/
2. `geode build` inside this folder (needs `GEODE_SDK` set).
3. The `.geode` file lands in `build/`; drop it into your Geode `mods` folder.

## Settings

| Setting | Meaning |
| --- | --- |
| Show Trajectory | draw the prediction lines |
| Autocorrect Early Clicks | enable the click delay |
| Practice Mode Only | restrict autocorrect to practice mode (default on) |
| Prediction Length | ticks simulated ahead (240 = 1s) |
| Max Correction Delay | largest delay autocorrect may add (12 ticks = 50ms) |
| Assumed Hold Length | how long the sim assumes you hold a click |

## Known limitations (read before tuning)

- The prediction runs a ghost `PlayerObject` through `checkCollisions`. Orbs, pads, portals and triggers can flip "already activated" flags on level objects, so the ghost can slightly desync on tricky sections. Saving/restoring those flags is the next improvement.
- The sim assumes you hold for a fixed length and then release; for ship/wave/ball sections where you hold or tap differently, tune *Assumed Hold Length*.
- Only player 1 is predicted (no dual-mode player 2 yet).
- Function signatures (`destroyPlayer`, `checkCollisions`, `copyAttributes`) follow the 2.2074 Geode bindings; if the compiler complains, check the current bindings in the SDK.
