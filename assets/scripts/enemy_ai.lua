-- Per-enemy stat overrides (Grunt only today), read back via
-- ScriptSystem::enemyStats(). The enemy AI state-machine module (ds.enemy_ai)
-- lands here in a later migration step; this file's role today is unchanged
-- from the old assets/scripts/waves.lua's ds.enemy_stats table.
ds.enemy_stats = {
    health = 100,
    speed = 3.0,
    damage = 10,
}
