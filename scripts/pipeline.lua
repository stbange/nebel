-- Nebel Render Pipeline Configuration (MIT License)
-- Defines default layer templates and shader pass parameter ranges.

local M = {}

M.default_samples = {
    { label = "Valley",   path = "assets/samples/alpine_valley.jpg" },
    { label = "Wildlife", path = "assets/samples/forest_stag.jpg" },
}

M.sliders = {
    { key = "density",        label = "Density",        min = 0.0, max = 3.5 },
    { key = "height_falloff", label = "Height Falloff", min = 0.0, max = 6.0 },
    { key = "horizon_y",      label = "Horizon Y",      min = 0.05, max = 0.95 },
    { key = "depth_min",      label = "Near Depth",     min = 0.0, max = 0.9 },
    { key = "sun_strength",   label = "Sun Glow",       min = 0.0, max = 1.6 },
    { key = "noise_scale",    label = "Wisp Scale",     min = 1.0, max = 16.0 },
    { key = "noise_amount",   label = "Turbulence",     min = 0.0, max = 1.0 },
    { key = "bloom_amount",   label = "Veiling Bloom",  min = 0.0, max = 1.2 },
    { key = "grain_match",    label = "Grain Match",    min = 0.0, max = 2.0 },
    { key = "edge_refine",    label = "Edge Matting",   min = 0.0, max = 1.0 },
}

function M.add_named_layer()
    local n = nebel.layers.count()
    local names = { "Valley Mist", "Ridge Haze", "Forest Veil", "Ground Drift" }
    local title = names[(n % #names) + 1]
    local idx = nebel.layers.add(title)
    if idx > 0 and idx == 2 then
        -- Default second layer sits farther back for instant atmospheric depth
        nebel.layers.set(idx, "depth_min", 0.45)
        nebel.layers.set(idx, "height_falloff", 0.8)
        nebel.layers.set(idx, "density", 1.1)
    end
    return idx
end

return M
