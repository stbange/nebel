-- Nebel Tool Definitions & Keyboard Shortcuts (MIT License)

local i18n     = require("i18n")
local tutorial = require("tutorial")

local M = {}

M.tools = {
    { id = "fog",     label_key = "tool_fog",     tip_key = "tip_tool_fog",     key = "B", icon = 0 },
    { id = "eraser",  label_key = "tool_eraser",  tip_key = "tip_tool_eraser",  key = "E", icon = 1 },
    { id = "protect", label_key = "tool_protect", tip_key = "tip_tool_protect", key = "P", icon = 2 },
    { id = "sun",     label_key = "tool_sun",     tip_key = "tip_tool_sun",     key = "S", icon = 3 },
}

M.depth_tools = {
    { id = "depth_near", label_key = "tool_depth_near", tip_key = "tip_tool_dnear", key = "N" },
    { id = "depth_far",  label_key = "tool_depth_far",  tip_key = "tip_tool_dfar",  key = "F" },
}

function M.handle_key(key, ctrl, shift)
    if ctrl and key == "Z" then
        if shift then nebel.view.redo() else nebel.view.undo() end
        return
    end
    if ctrl and key == "Y" then nebel.view.redo(); return end

    if key == "ENTER" then
        nebel.render.start()
        return
    elseif key == "ESC" then
        local ph = nebel.render.phase()
        if ph == "depth" or ph == "fluid" or ph == "rendering" then
            nebel.render.cancel()
            nebel.view.set_status(i18n.t("status_cancelled"))
        elseif tutorial.open then
            tutorial.close()
        end
        return
    elseif key == "F1" then
        nebel.platform.open_help(i18n.lang())
        return
    elseif key == "M" then
        nebel.view.set_show_mask(not nebel.view.show_mask())
        return
    elseif key == "A" then
        local on = not nebel.brush.get_auto_depth()
        nebel.brush.set_auto_depth(on)
        nebel.view.set_status(on and i18n.t("status_autolock_on") or i18n.t("status_autolock_off"))
    elseif key == "B" then nebel.brush.set_tool("fog")
    elseif key == "D" then nebel.view.set_show_depth(not nebel.view.show_depth())
    elseif key == "E" then nebel.brush.set_tool("eraser")
    elseif key == "P" then nebel.brush.set_tool("protect")
    elseif key == "S" then nebel.brush.set_tool("sun")
    elseif key == "N" then
        nebel.view.set_show_depth(true)
        nebel.brush.set_tool("depth_near")
    elseif key == "F" then
        nebel.view.set_show_depth(true)
        nebel.brush.set_tool("depth_far")
    elseif key == "R" and nebel.layers.count() > 0 then
        local idx = nebel.layers.active()
        local cur = nebel.layers.get(idx, "noise_seed") or 1337
        nebel.layers.set(idx, "noise_seed", ((cur * 1664525 + 1013904223) % 9000) + 1)
        nebel.view.set_status(i18n.t("status_seed"))
    elseif key == "[" then nebel.brush.set_radius(nebel.brush.get_radius() * 0.82)
    elseif key == "]" then nebel.brush.set_radius(nebel.brush.get_radius() * 1.22)
    elseif key == "SPACE" then
        nebel.view.set_split(not nebel.view.split_enabled())
    elseif key == "0" then nebel.view.reset()
    elseif key == "F5" then nebel.io.reload_scripts()
    end
end

return M
