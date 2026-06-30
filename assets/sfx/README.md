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

Music goes in `../music/` (e.g. `music/theme.mp3`, looped/streamed).

Supported decode formats out of the box: WAV, FLAC, MP3. OGG/Vorbis requires the
optional miniaudio decoder extras and is not enabled here, so prefer WAV/FLAC/MP3.
Short mono clips work best for the 3D-spatialised enemy cues.
