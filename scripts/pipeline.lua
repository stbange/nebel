-- Nebel Render Pipeline Configuration (MIT License)
-- Defines default layer templates and shader pass parameter ranges.

local M = {}

M.default_samples = {
    { label = "Valley",   path = "assets/samples/alpine_valley.jpg" },
    { label = "Wildlife", path = "assets/samples/forest_stag.jpg" },
}

M.sliders = {
    { key = "density",        label = "Density",        min = 0.0, max = 4.0 },
    { key = "depth_min",      label = "Near Depth",     min = 0.0, max = 0.95 },
    { key = "depth_max",      label = "Far Depth",      min = 0.15, max = 1.0 },
    { key = "height_falloff", label = "Height Falloff", min = 0.0, max = 6.0 },
    { key = "horizon_y",      label = "Fog Ceiling",    min = 0.05, max = 0.95 },
    { key = "sun_strength",   label = "Sun Glow",       min = 0.0, max = 1.6 },
    { key = "noise_amount",   label = "Wisp Clouds",    min = 0.0, max = 1.0 },
    { key = "bloom_amount",   label = "Veiling Bloom",  min = 0.0, max = 1.2 },
    { key = "grain_match",    label = "Grain Match",    min = 0.0, max = 2.0 },
}

function M.add_named_layer()
    local n = nebel.layers.count()
    local names = { "Valley Mist", "Ridge Haze", "Forest Veil", "Ground Drift" }
    local title = names[(n % #names) + 1]
    local idx = nebel.layers.add(title)
    if idx > 0 and idx == 2 then
        -- Default second layer sits farther back for instant atmospheric depth
        nebel.layers.set(idx, "depth_min", 0.62)
        nebel.layers.set(idx, "depth_max", 1.0)
        nebel.layers.set(idx, "height_falloff", 0.6)
        nebel.layers.set(idx, "density", 1.8)
    end
    return idx
end

return M
