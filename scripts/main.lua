-- Nebel Main Lua Entry Script (MIT License)
-- Manipulate this file and its modules beside the binary without recompiling C.

local i18n     = require("i18n")
local tools    = require("tools")
local presets  = require("presets")
local ui       = require("ui")
local pipeline = require("pipeline")

function on_init()
    if nebel.layers.count() == 0 then
        local sample = pipeline.default_samples[1]
        if sample and nebel.io.load_sample(sample.path) then
            presets.apply("Morning Valley")
            -- Pre-mark valley fog zone and run initial render so startup showcases volumetric fog
            nebel.brush.set_tool("fog")
            nebel.brush.set_radius(0.24)
            nebel.brush.set_opacity(0.75)
            nebel.brush.stroke_begin(0.18, 0.56)
            nebel.brush.stroke_move(0.50, 0.52)
            nebel.brush.stroke_move(0.84, 0.56)
            nebel.brush.stroke_end()
            nebel.brush.set_radius(0.10)
            nebel.brush.set_opacity(0.70)
            nebel.render.start()
            nebel.view.set_status(i18n.t("status_ready"))
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
