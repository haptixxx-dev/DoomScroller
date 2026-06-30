# Sound effects

Drop placeholder/final SFX here. The engine resolves these paths relative to
`ds::paths::assets()` (in dev builds: `<repo>/assets/`). All files are optional:
`AudioSystem` logs a warning once and silently skips any missing file, so the
game runs fine with none of them present.

Expected files (see `engine/include/engine/Engine.h`):

| Path                    | Trigger                                  |
|-------------------------|------------------------------------------|
| `sfx/weapon_fire.wav`   | Player fires the weapon                  |
| `sfx/enemy_hit.wav`     | Hitscan damages an enemy (3D, at enemy)  |
| `sfx/enemy_death.wav`   | Hitscan kills an enemy (3D, at enemy)    |
| `sfx/player_hit.wav`    | An enemy melee attack damages the player |
| `sfx/dash.wav`          | Dash burst                               |
| `sfx/slide.wav`         | Slide start                              |
| `sfx/explosion.wav`     | Rocket / splash detonation (3D)          |
| `sfx/parry.wav`         | Parry window opened (chime)              |
| `sfx/pickup.wav`        | Pickup orb collected (3D)                |
| `sfx/rank_up.wav`       | Style rank increased (UI bus)            |
| `sfx/footstep.wav`      | Grounded movement cadence                |
| `sfx/ui_click.wav`      | Menu / settings interaction (UI bus)     |

Music goes in `../music/` (e.g. `music/theme.mp3`, looped/streamed).

Audio buses (task 44): `master` scales everything; `sfx`, `music`, and `ui`
carry independent volumes driven by the settings menu. Rank-up + UI clicks route
through the UI bus; music is ducked to ~35% on the YOU DIED / Victory screens.

Supported decode formats out of the box: WAV, FLAC, MP3. OGG/Vorbis requires the
optional miniaudio decoder extras and is not enabled here, so prefer WAV/FLAC/MP3.
Short mono clips work best for the 3D-spatialised enemy cues.
