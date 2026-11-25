-- Nebel Tool Definitions & Keyboard Shortcuts (MIT License)

local M = {}

M.tools = {
    { id = "fog",     label = "Fog",     key = "B", icon = 0 },
    { id = "eraser",  label = "Erase",   key = "E", icon = 1 },
    { id = "protect", label = "Protect", key = "P", icon = 2 },
    { id = "sun",     label = "Sun",     key = "S", icon = 3 },
}

function M.handle_key(key, ctrl, shift)
    if ctrl and key == "Z" then
        if shift then nebel.view.redo() else nebel.view.undo() end
        return
    end
    if ctrl and key == "Y" then nebel.view.redo(); return end
    if key == "B" then nebel.brush.set_tool("fog")
    elseif key == "D" then nebel.view.set_show_depth(not nebel.view.show_depth())
    elseif key == "E" then nebel.brush.set_tool("eraser")
    elseif key == "P" then nebel.brush.set_tool("protect")
    elseif key == "S" then nebel.brush.set_tool("sun")
    elseif key == "R" and nebel.layers.count() > 0 then
        local idx = nebel.layers.active()
        local cur = nebel.layers.get(idx, "noise_seed") or 1337
        nebel.layers.set(idx, "noise_seed", ((cur * 1664525 + 1013904223) % 9000) + 1)
        nebel.view.set_status("Randomized fog wisp pattern")
    elseif key == "[" then nebel.brush.set_radius(nebel.brush.get_radius() * 0.82)
    elseif key == "]" then nebel.brush.set_radius(nebel.brush.get_radius() * 1.22)
    elseif key == "SPACE" then
        nebel.view.set_split(not nebel.view.split_enabled())
    elseif key == "0" then nebel.view.reset()
    elseif key == "F5" then nebel.io.reload_scripts()
    end
end

return M
