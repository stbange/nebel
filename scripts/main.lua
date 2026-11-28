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
