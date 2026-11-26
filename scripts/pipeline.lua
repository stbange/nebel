-- Nebel Render Pipeline Configuration (MIT License)
-- Defines default sample photos, layer templates, and localized atmosphere sliders.

local M = {}

M.default_samples = {
    { label_key = "btn_sample_valley",   path = "assets/samples/alpine_valley.jpg" },
    { label_key = "btn_sample_wildlife", path = "assets/samples/forest_stag.jpg" },
}

M.sliders = {
    { key = "density",          label_key = "sl_density",        tip_key = "tip_density",        min = 0.0, max = 4.0 },
    { key = "depth_min",        label_key = "sl_depth_min",      tip_key = "tip_depth_min",      min = 0.0, max = 0.95 },
    { key = "depth_max",        label_key = "sl_depth_max",      tip_key = "tip_depth_max",      min = 0.15, max = 1.0 },
    { key = "height_falloff",   label_key = "sl_height_falloff", tip_key = "tip_height_falloff", min = 0.0, max = 6.0 },
    { key = "noise_amount",     label_key = "sl_noise_amount",   tip_key = "tip_noise_amount",   min = 0.0, max = 1.0 },
    { key = "noise_scale",      label_key = "sl_noise_scale",    tip_key = "tip_noise_scale",    min = 1.5, max = 14.0 },
    { key = "fluid_flow",       label_key = "sl_fluid_flow",     tip_key = "tip_fluid_flow",     min = 0.0, max = 1.0 },
    { key = "shadow_pool",      label_key = "sl_shadow_pool",    tip_key = "tip_shadow_pool",    min = 0.0, max = 1.0 },
    { key = "delight_strength", label_key = "sl_delight",        tip_key = "tip_delight",        min = 0.0, max = 1.0 },
    { key = "sun_strength",     label_key = "sl_sun_strength",   tip_key = "tip_sun_strength",   min = 0.0, max = 1.6 },
    { key = "god_rays",         label_key = "sl_god_rays",       tip_key = "tip_god_rays",       min = 0.0, max = 1.5 },
    { key = "bloom_amount",     label_key = "sl_bloom",          tip_key = "tip_bloom",          min = 0.0, max = 1.2 },
    { key = "grain_match",      label_key = "sl_grain",          tip_key = "tip_grain",          min = 0.0, max = 2.0 },
}

function M.add_named_layer()
    local n = nebel.layers.count()
    local names = { "Valley Mist", "Ridge Haze", "Forest Veil", "Ground Drift" }
    local title = names[(n % #names) + 1]
    local idx = nebel.layers.add(title)
    if idx > 0 and idx == 2 then
        nebel.layers.set(idx, "depth_min", 0.62)
        nebel.layers.set(idx, "depth_max", 1.0)
        nebel.layers.set(idx, "height_falloff", 0.6)
        nebel.layers.set(idx, "density", 1.8)
    end
    return idx
end

return M
