-- Nebel Photographer Fog Presets (MIT License)
-- Edit or add presets here without recompiling the C engine (press F5 to reload).

local M = {}

M.order = { "Morning Valley", "Woodland Mist", "Backlit Wildlife", "Moody Overcast" }

M.presets = {
    ["Morning Valley"] = {
        density = 1.65,
        height_falloff = 2.6,
        horizon_y = 0.44,
        depth_min = 0.40,
        depth_max = 0.96,
        airlight_r = 0.80,
        airlight_g = 0.86,
        airlight_b = 0.94,
        sun_x = 0.74,
        sun_y = 0.24,
        sun_strength = 0.55,
        sun_anisotropy = 0.68,
        noise_scale = 5.5,
        noise_amount = 0.78,
        bloom_amount = 0.40,
        god_rays = 0.45,
        shadow_pool = 0.55,
        grain_match = 1.0,
        edge_refine = 0.88,
    },
    ["Woodland Mist"] = {
        density = 1.45,
        height_falloff = 1.1,
        horizon_y = 0.36,
        depth_min = 0.42,
        depth_max = 1.0,
        airlight_r = 0.70,
        airlight_g = 0.79,
        airlight_b = 0.80,
        sun_x = 0.28,
        sun_y = 0.22,
        sun_strength = 0.45,
        sun_anisotropy = 0.62,
        noise_scale = 6.5,
        noise_amount = 0.82,
        bloom_amount = 0.38,
        god_rays = 0.55,
        shadow_pool = 0.50,
        grain_match = 1.1,
        edge_refine = 0.92,
    },
    ["Backlit Wildlife"] = {
        density = 1.55,
        height_falloff = 0.75,
        horizon_y = 0.24,
        depth_min = 0.44,
        depth_max = 1.0,
        airlight_r = 0.96,
        airlight_g = 0.82,
        airlight_b = 0.60,
        sun_x = 0.22,
        sun_y = 0.24,
        sun_strength = 1.00,
        sun_anisotropy = 0.76,
        noise_scale = 4.8,
        noise_amount = 0.76,
        bloom_amount = 0.65,
        god_rays = 0.85,
        shadow_pool = 0.45,
        grain_match = 1.0,
        edge_refine = 0.95,
    },
    ["Moody Overcast"] = {
        density = 2.00,
        height_falloff = 0.85,
        horizon_y = 0.40,
        depth_min = 0.34,
        depth_max = 1.0,
        airlight_r = 0.64,
        airlight_g = 0.70,
        airlight_b = 0.78,
        sun_x = 0.50,
        sun_y = 0.15,
        sun_strength = 0.12,
        sun_anisotropy = 0.35,
        noise_scale = 5.2,
        noise_amount = 0.70,
        bloom_amount = 0.18,
        god_rays = 0.05,
        shadow_pool = 0.40,
        grain_match = 1.15,
        edge_refine = 0.85,
    },
}

function M.apply(name)
    local p = M.presets[name]
    if not p or nebel.layers.count() < 1 then
        return false
    end
    local idx = nebel.layers.active()
    for k, v in pairs(p) do
        nebel.layers.set(idx, k, v)
    end
    nebel.view.set_status("Preset applied: " .. name)
    nebel.audio.play("assets/sounds/ui_click.wav", 0.4)
    return true
end

return M
