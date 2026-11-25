-- Nebel Photographer Fog Presets (MIT License)
-- Edit or add presets here without recompiling the C engine (press F5 to reload).

local M = {}

M.order = { "Morning Valley", "Woodland Mist", "Backlit Wildlife", "Moody Overcast" }

M.presets = {
    ["Morning Valley"] = {
        density = 1.55,
        height_falloff = 2.8,
        horizon_y = 0.44,
        depth_min = 0.14,
        depth_max = 1.0,
        airlight_r = 0.78,
        airlight_g = 0.84,
        airlight_b = 0.92,
        sun_x = 0.74,
        sun_y = 0.24,
        sun_strength = 0.50,
        sun_anisotropy = 0.66,
        noise_scale = 5.5,
        noise_amount = 0.42,
        bloom_amount = 0.30,
        grain_match = 1.0,
        edge_refine = 0.88,
    },
    ["Woodland Mist"] = {
        density = 1.25,
        height_falloff = 1.2,
        horizon_y = 0.48,
        depth_min = 0.22,
        depth_max = 1.0,
        airlight_r = 0.68,
        airlight_g = 0.76,
        airlight_b = 0.78,
        sun_x = 0.50,
        sun_y = 0.18,
        sun_strength = 0.25,
        sun_anisotropy = 0.55,
        noise_scale = 7.5,
        noise_amount = 0.48,
        bloom_amount = 0.20,
        grain_match = 1.1,
        edge_refine = 0.92,
    },
    ["Backlit Wildlife"] = {
        density = 1.10,
        height_falloff = 1.6,
        horizon_y = 0.46,
        depth_min = 0.28,
        depth_max = 1.0,
        airlight_r = 0.94,
        airlight_g = 0.80,
        airlight_b = 0.62,
        sun_x = 0.68,
        sun_y = 0.28,
        sun_strength = 0.95,
        sun_anisotropy = 0.76,
        noise_scale = 4.5,
        noise_amount = 0.30,
        bloom_amount = 0.55,
        grain_match = 1.0,
        edge_refine = 0.95,
    },
    ["Moody Overcast"] = {
        density = 1.90,
        height_falloff = 0.9,
        horizon_y = 0.40,
        depth_min = 0.10,
        depth_max = 1.0,
        airlight_r = 0.64,
        airlight_g = 0.69,
        airlight_b = 0.75,
        sun_x = 0.50,
        sun_y = 0.15,
        sun_strength = 0.10,
        sun_anisotropy = 0.35,
        noise_scale = 6.0,
        noise_amount = 0.35,
        bloom_amount = 0.12,
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
