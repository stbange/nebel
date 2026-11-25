-- Nebel Darkroom UI Layout (Desktop Sidebar + Mobile Touch Bottom Drawer) (MIT License)

local tools    = require("tools")
local presets  = require("presets")
local pipeline = require("pipeline")

local M = {
    mobile_tab = "tools", -- "tools" | "fog" | "layers"
    drawer_open = true,
}

local function draw_brush_cursor()
    if nebel.ui.pointer_over_ui() or nebel.ui.is_touch_mode() or nebel.view.show_depth() then return end
    local cx, cy, cw, ch = nebel.view.canvas_rect()
    local mx, my = nebel.ui.pointer_pos()
    if mx < cx or mx > cx + cw or my < cy or my > cy + ch then return end
    local t = nebel.brush.get_tool()
    local r = nebel.brush.get_radius() * math.min(cw, ch)
    if t == "sun" then
        nebel.ui.ring(mx, my, 12, 1.0, 0.86, 0.45, 0.9)
    elseif t == "protect" then
        nebel.ui.ring(mx, my, r, 0.35, 0.90, 0.55, 0.85)
    elseif t == "eraser" then
        nebel.ui.ring(mx, my, r, 0.95, 0.45, 0.45, 0.85)
    else
        nebel.ui.ring(mx, my, r, 0.82, 0.92, 1.00, 0.80)
    end
end

local function draw_top_bar(vw, touch)
    local h = touch and 52 or 44
    nebel.ui.panel(0, 0, vw, h, 0.08, 0.09, 0.11, 0.96)
    local x = 8
    local bh = touch and 38 or 30
    local by = (h - bh) * 0.5

    if nebel.ui.button(10, x, by, touch and 66 or 68, bh, "Open", false, 4) then
        nebel.io.open_photo()
    end
    x = x + (touch and 72 or 74)

    for i, s in ipairs(pipeline.default_samples) do
        local w = touch and 62 or 66
        if x + w < vw - 220 then
            if nebel.ui.button(20 + i, x, by, w, bh, s.label, false, -1) then
                if nebel.io.load_sample(s.path) then
                    if i == 2 then
                        presets.apply("Backlit Wildlife")
                        nebel.brush.set_tool("fog")
                        nebel.brush.set_radius(0.42)
                        nebel.brush.set_opacity(0.80)
                        nebel.brush.set_softness(0.95)
                        nebel.brush.stroke_begin(0.15, 0.44)
                        nebel.brush.stroke_move(0.85, 0.44)
                        nebel.brush.stroke_end()
                        nebel.brush.set_radius(0.10)
                        nebel.brush.set_softness(0.75)
                    else
                        presets.apply("Morning Valley")
                    end
                end
            end
            x = x + w + 6
        end
    end

    local depth_lbl = nebel.view.is_neural_depth() and "3D:AI" or "Auto3D"
    if x + 76 < vw - 180 then
        if nebel.ui.button(30, x, by, 74, bh, depth_lbl, nebel.view.is_neural_depth(), -1) then
            nebel.view.trigger_auto_depth()
        end
        x = x + 80
    end

    if x + 64 < vw - 160 then
        if nebel.ui.button(32, x, by, 62, bh, "Depth", nebel.view.show_depth(), -1) then
            nebel.view.set_show_depth(not nebel.view.show_depth())
        end
        x = x + 68
    end

    if x + 56 < vw - 140 then
        if nebel.ui.button(31, x, by, 54, bh, "Split", nebel.view.split_enabled(), -1) then
            nebel.view.set_split(not nebel.view.split_enabled())
        end
        x = x + 60
    end

    -- Right side: Undo, Redo, Export, Mute
    local rx = vw - 8
    local mw = touch and 44 or 42
    rx = rx - mw
    if nebel.ui.button(40, rx, by, mw, bh, nebel.audio.is_muted() and "Off" or "Sfx", not nebel.audio.is_muted(), -1) then
        nebel.audio.set_muted(not nebel.audio.is_muted())
    end

    rx = rx - (touch and 68 or 64) - 6
    if nebel.ui.button(41, rx, by, touch and 68 or 64, bh, "PNG", false, 5) then
        nebel.io.export_png()
    end

    if not touch then
        rx = rx - 58 - 6
        if nebel.ui.button(42, rx, by, 58, bh, "JPG", false, -1) then
            nebel.io.export_jpg(92)
        end
    end

    rx = rx - 46 - 6
    if nebel.ui.button(43, rx, by, 46, bh, "Redo", nebel.view.can_redo(), -1) then
        nebel.view.redo()
    end
    rx = rx - 48 - 6
    if nebel.ui.button(44, rx, by, 48, bh, "Undo", nebel.view.can_undo(), -1) then
        nebel.view.undo()
    end
end

local function draw_desktop_sidebar(vw, vh)
    local sw = 284
    local sx = vw - sw
    nebel.ui.panel(sx, 44, sw, vh - 44, 0.09, 0.10, 0.12, 0.96)
    local y = 54
    local pad = 12
    local iw = sw - pad * 2

    -- Tools row
    nebel.ui.label(sx + pad, y, "TOOLS", 0.60, 0.66, 0.74, 1.0)
    y = y + 18
    local cur_tool = nebel.brush.get_tool()
    local tw = (iw - 12) / 4
    for i, t in ipairs(tools.tools) do
        if nebel.ui.button(100 + i, sx + pad + (i - 1) * (tw + 4), y, tw, 28, t.label, cur_tool == t.id, -1) then
            nebel.brush.set_tool(t.id)
        end
    end
    y = y + 34

    -- Brush size & opacity
    local rad, c1 = nebel.ui.slider(120, sx + pad, y, iw, 26, "Brush Size", nebel.brush.get_radius(), 0.01, 0.40)
    if c1 then nebel.brush.set_radius(rad) end
    y = y + 30
    local op, c2 = nebel.ui.slider(121, sx + pad, y, iw, 26, "Opacity", nebel.brush.get_opacity(), 0.05, 1.0)
    if c2 then nebel.brush.set_opacity(op) end
    y = y + 32

    -- Layers section
    nebel.ui.label(sx + pad, y, "FOG LAYERS", 0.60, 0.66, 0.74, 1.0)
    if nebel.ui.button(140, sx + sw - pad - 54, y - 4, 54, 22, "+ Add", false, -1) then
        pipeline.add_named_layer()
    end
    y = y + 22

    local lcount = nebel.layers.count()
    local active = nebel.layers.active()
    for i = 1, math.min(lcount, 4) do
        local name = nebel.layers.name(i) or ("Layer " .. i)
        local vis = (nebel.layers.get(i, "visible") or 1) >= 0.5
        if nebel.ui.button(150 + i, sx + pad, y, iw - 86, 24, name, i == active, -1) then
            nebel.layers.select(i)
        end
        if nebel.ui.button(155 + i, sx + pad + iw - 82, y, 24, 24, vis and "V" or "-", vis, -1) then
            nebel.layers.set(i, "visible", vis and 0 or 1)
        end
        if nebel.ui.button(160 + i, sx + pad + iw - 54, y, 24, 24, "C", false, -1) then
            nebel.layers.clear(i)
        end
        if lcount > 1 and nebel.ui.button(170 + i, sx + pad + iw - 26, y, 24, 24, "X", false, -1) then
            nebel.layers.remove(i)
        end
        y = y + 27
    end
    y = y + 6

    -- Physics sliders for active layer
    if lcount > 0 then
        nebel.ui.label(sx + pad, y, "ATMOSPHERE", 0.60, 0.66, 0.74, 1.0)
        if nebel.ui.button(184, sx + sw - pad - 122, y - 4, 52, 22, "Seed", false, -1) then
            local cur = nebel.layers.get(active, "noise_seed") or 1337
            nebel.layers.set(active, "noise_seed", ((cur * 1664525 + 1013904223) % 9000) + 1)
            nebel.view.set_status("Randomized fog wisp pattern")
        end
        if nebel.ui.button(185, sx + sw - pad - 66, y - 4, 66, 22, "SkyTint", false, -1) then
            nebel.view.sample_airlight(-1, -1)
        end
        y = y + 22
        for i, sl in ipairs(pipeline.sliders) do
            if y + 30 > vh - 128 then break end
            local val = nebel.layers.get(active, sl.key) or sl.min
            local nv, ch = nebel.ui.slider(200 + i, sx + pad, y, iw, 26, sl.label, val, sl.min, sl.max)
            if ch then nebel.layers.set(active, sl.key, nv) end
            y = y + 30
        end
        -- Fog Warmth (cool mist blue <-> warm golden haze)
        if y + 28 <= vh - 98 then
            local ar = nebel.layers.get(active, "airlight_r") or 0.78
            local ab = nebel.layers.get(active, "airlight_b") or 0.88
            local warmth = math.max(0.0, math.min(1.0, (ar - ab + 0.35) / 0.70))
            local nw, cw = nebel.ui.slider(240, sx + pad, y, iw, 26, "Fog Warmth", warmth, 0.0, 1.0)
            if cw then
                nebel.layers.set(active, "airlight_r", 0.68 + 0.30 * nw)
                nebel.layers.set(active, "airlight_g", 0.78 + 0.08 * nw)
                nebel.layers.set(active, "airlight_b", 0.94 - 0.34 * nw)
            end
        end
    end

    -- Presets at bottom of sidebar
    local py = math.max(y + 8, vh - 96)
    nebel.ui.label(sx + pad, py, "PRESETS", 0.60, 0.66, 0.74, 1.0)
    py = py + 18
    local pw = (iw - 6) / 2
    for i, pname in ipairs(presets.order) do
        local col = (i - 1) % 2
        local row = math.floor((i - 1) / 2)
        if nebel.ui.button(300 + i, sx + pad + col * (pw + 6), py + row * 29, pw, 25, pname, false, -1) then
            presets.apply(pname)
        end
    end

    -- Status bar
    nebel.ui.label(12, vh - 20, nebel.view.status(), 0.62, 0.68, 0.75, 1.0)
end

local function draw_mobile_drawer(vw, vh)
    local dh = M.drawer_open and 142 or 40
    local dy = vh - dh
    nebel.ui.panel(0, dy, vw, dh, 0.08, 0.09, 0.11, 0.96)

    -- Tab buttons across top of bottom drawer (44px+ touch targets)
    local tabs = { { "tools", "Tools" }, { "fog", "Fog" }, { "layers", "Layers" } }
    local tw = math.min(96, (vw - 80) / 3)
    for i, tb in ipairs(tabs) do
        if nebel.ui.button(400 + i, 8 + (i - 1) * (tw + 6), dy + 4, tw, 32, tb[2], M.mobile_tab == tb[1], -1) then
            M.mobile_tab = tb[1]
            M.drawer_open = true
        end
    end
    if nebel.ui.button(410, vw - 66, dy + 4, 58, 32, M.drawer_open and "Hide" or "Show", false, -1) then
        M.drawer_open = not M.drawer_open
    end

    if not M.drawer_open then return end
    local cy = dy + 44
    local active = nebel.layers.active()

    if M.mobile_tab == "tools" then
        local bw = (vw - 34) / 4
        local cur = nebel.brush.get_tool()
        for i, t in ipairs(tools.tools) do
            if nebel.ui.button(420 + i, 8 + (i - 1) * (bw + 6), cy, bw, 38, t.label, cur == t.id, -1) then
                nebel.brush.set_tool(t.id)
            end
        end
        local r, cr = nebel.ui.slider(430, 10, cy + 48, vw - 20, 36, "Brush Size", nebel.brush.get_radius(), 0.01, 0.40)
        if cr then nebel.brush.set_radius(r) end
    elseif M.mobile_tab == "fog" and nebel.layers.count() > 0 then
        local half = (vw - 30) / 2
        local d, cd = nebel.ui.slider(440, 10, cy, half, 36, "Density", nebel.layers.get(active, "density") or 1.8, 0.0, 4.0)
        if cd then nebel.layers.set(active, "density", d) end
        local n, cn = nebel.ui.slider(441, 20 + half, cy, half, 36, "Near Depth", nebel.layers.get(active, "depth_min") or 0.40, 0.0, 0.95)
        if cn then nebel.layers.set(active, "depth_min", n) end
        local s, cs = nebel.ui.slider(442, 10, cy + 44, half, 36, "Sun Glow", nebel.layers.get(active, "sun_strength") or 0.45, 0.0, 1.6)
        if cs then nebel.layers.set(active, "sun_strength", s) end
        local h, ch = nebel.ui.slider(443, 20 + half, cy + 44, half, 36, "Falloff", nebel.layers.get(active, "height_falloff") or 2.0, 0.0, 6.0)
        if ch then nebel.layers.set(active, "height_falloff", h) end
    elseif M.mobile_tab == "layers" then
        if nebel.ui.button(450, 10, cy, 86, 38, "+ Layer", false, -1) then
            pipeline.add_named_layer()
        end
        for i = 1, math.min(nebel.layers.count(), 3) do
            if nebel.ui.button(450 + i, 102 + (i - 1) * 90, cy, 84, 38, nebel.layers.name(i) or ("L" .. i), i == active, -1) then
                nebel.layers.select(i)
            end
        end
        local pw = (vw - 34) / 4
        for i, pname in ipairs(presets.order) do
            if nebel.ui.button(460 + i, 8 + (i - 1) * (pw + 6), cy + 48, pw, 36, pname:match("^%S+"), false, -1) then
                presets.apply(pname)
            end
        end
    end
end

function M.draw(vw, vh)
    local touch = nebel.ui.is_touch_mode()
    draw_top_bar(vw, touch)
    if touch then
        draw_mobile_drawer(vw, vh)
    else
        draw_desktop_sidebar(vw, vh)
    end
    draw_brush_cursor()
end

return M
