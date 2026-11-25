-- Nebel Main Lua Entry Script (MIT License)
-- Manipulate this file and its modules beside the binary without recompiling C.

local tools    = require("tools")
local presets  = require("presets")
local ui       = require("ui")
local pipeline = require("pipeline")

function on_init()
    -- Load bundled sample on first startup if no photo is loaded yet
    if nebel.layers.count() == 0 then
        local sample = pipeline.default_samples[1]
        if sample and nebel.io.load_sample(sample.path) then
            presets.apply("Morning Valley")
            nebel.layers.set(1, "density", 1.35)
            nebel.layers.set(1, "depth_min", 0.34)
            nebel.layers.set(1, "noise_amount", 0.68)
            nebel.layers.set(1, "noise_scale", 6.5)
            nebel.brush.set_tool("fog")
            nebel.brush.set_radius(0.16)
            nebel.brush.set_opacity(0.60)
            nebel.brush.stroke_begin(0.30, 0.50)
            nebel.brush.stroke_move(0.52, 0.52)
            nebel.brush.stroke_move(0.74, 0.54)
            nebel.brush.stroke_end()
            -- Protect foreground pine ridges so mist settles naturally inside the valley
            nebel.brush.set_tool("protect")
            nebel.brush.set_radius(0.20)
            nebel.brush.set_opacity(0.95)
            nebel.brush.stroke_begin(0.12, 0.62)
            nebel.brush.stroke_move(0.25, 0.72)
            nebel.brush.stroke_end()
            nebel.brush.stroke_begin(0.86, 0.64)
            nebel.brush.stroke_move(0.78, 0.78)
            nebel.brush.stroke_end()
            nebel.brush.set_tool("fog")
            nebel.brush.set_radius(0.08)
            nebel.brush.set_opacity(0.65)
            nebel.view.set_status("Paint fog (B: Fog, P: Protect, Space: Split, F5: Reload)")
        end
    end
end

function on_ui(vw, vh)
    ui.draw(vw, vh)
end

function on_key(key, ctrl, shift)
    tools.handle_key(key, ctrl, shift)
end

function apply_preset(name)
    return presets.apply(name)
end
