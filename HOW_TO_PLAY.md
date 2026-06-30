# How to Play — DoomScroller

Fast 3D arena FPS. Kill enemies, look stylish, survive all 8 waves.

---

## Controls

| Input | Action |
|---|---|
| `W A S D` | Move |
| `Space` | Jump |
| `Shift` | Dash |
| `Ctrl` | Slide (hold while moving) |
| `LMB` | Primary fire |
| `RMB` | Alt fire |
| `F` / `Middle Mouse` | Parry |
| `Scroll wheel` / `1–9` | Switch weapon |
| `Escape` | Pause / menu |

---

## Weapons

You carry three weapons. Switch freely at any time — switching mid-fight earns bonus style.

| # | Weapon | Primary | Alt fire | Ammo |
|---|---|---|---|---|
| 1 | **Hitscan** | Instant ray, 25 dmg, 5 rps | Shotgun spread | Infinite |
| 2 | **Rocket** | Projectile, heavy dmg, 1 rps, splash | Buffed single shot | Limited |
| 3 | **Plasma** | Fast projectile, 8 rps | Buffed single shot | Limited |

Rockets and plasma bolts travel in a straight line and deal splash on impact.

**Upgrades:** at each wave intermission one weapon is upgraded automatically (+damage or +fire rate).

---

## Movement

**Dash** — 2 charges. Each charge regenerates in 2.5 s. Dash grants brief invulnerability frames.  
**Slide** — enter by holding `Ctrl` while moving at speed. Boosts momentum on entry; exits when you slow below 3 m/s or after 1.2 s.  
**Coyote time** — you can jump for a short window after walking off a ledge.  
**Dash-cancel** — dash can interrupt any recovery animation.

---

## Parry

Press `F` or `Middle Mouse` to open a **0.3 s parry window**.

- Negates all incoming damage that frame.
- Reflects enemy projectiles back at 1.5× speed.
- **Refunds 1 dash charge** on success.
- 0.6 s cooldown before the next parry.

Parrying is the highest-value move in the kit — it turns an enemy shot into a free dash and 60 style points.

---

## Style Meter

The HUD shows your current style rank. Points accumulate on flashy kills and bleed away at 12 pts/s if you play safe.

| Action | Points |
|---|---|
| Kill | 20 |
| Weapon-switch kill | 40 |
| Air kill | 45 |
| Dash kill | 50 |
| Parry | 60 |
| Multi-kill | 70 |

**Ranks** (ascending): D → C → B → A → S → SS → SSS

Score is tracked per-run. High score is saved between sessions.

---

## Waves

8 waves total. Wave 1 starts with 3 enemies; each subsequent wave adds 2 (cap: 24).  
Between waves there is a 3 s intermission and you receive a **weapon upgrade**.  
Each kill is worth 100 score points.

### Pickups

Enemies drop pickups on death. Walk over them to collect.

| Pickup | Effect |
|---|---|
| Health orb | Restore HP (up to 100) |
| Ammo orb | Refill weapon ammo |
| Dash orb | Restore dash charges |

### Boss (wave 8)

A large, high-health enemy spawns after wave 8 clears. It fires projectile volleys.  
**Parry its projectiles** to reflect them back. During its vulnerable window it takes **2× damage**.  
Defeat the boss to trigger Victory.

---

## HUD

- **Health bar** — bottom left.
- **Ammo / wave** — current ammo count and wave number.
- **Style rank** — displayed prominently; pulses on rank-up.
- **Floating damage numbers** — appear at hit locations.
- **Red flash** — full-screen tint when you take damage.
- **Music duck** — music drops to ~35% on death or victory screen.
