-- Nebel v3 Darkroom UI Layout (EN / DE / PL, Mark -> Render, Tooltips, Tutorial) (MIT License)

local i18n     = require("i18n")
local tools    = require("tools")
local presets  = require("presets")
local pipeline = require("pipeline")
local tutorial = require("tutorial")

local M = {
    mobile_tab = "tools", -- "tools" | "fog" | "layers"
    drawer_open = true,
    last_phase = "mark",
}

local function tip(key)
    if key and nebel.ui.last_hovered() and not nebel.ui.is_touch_mode() then
        nebel.ui.tooltip(i18n.t(key))
    end
end

local function is_rendering_phase(ph)
    return ph == "depth" or ph == "fluid" or ph == "rendering"
end

local function draw_brush_cursor()
    if nebel.ui.pointer_over_ui() or nebel.ui.is_touch_mode() then return end
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
    elseif t == "depth_near" then
        nebel.ui.ring(mx, my, r, 0.30, 0.65, 1.00, 0.85)
    elseif t == "depth_far" then
        nebel.ui.ring(mx, my, r, 1.00, 0.82, 0.40, 0.85)
    else
        nebel.ui.ring(mx, my, r, 0.55, 0.88, 1.00, 0.82)
    end
end

local function draw_top_bar(vw, touch)
    local h = touch and 52 or 44
    nebel.ui.panel(0, 0, vw, h, 0.08, 0.09, 0.11, 0.96)
    local x = 8
    local bh = touch and 38 or 30
    local by = (h - bh) * 0.5
    local ph = nebel.render.phase()
    local busy = is_rendering_phase(ph)
    local dirty = nebel.render.is_dirty()

    -- Open photo
    local open_w = touch and 80 or 78
    if nebel.ui.button(10, x, by, open_w, bh, i18n.t("btn_open"), false, 4) then
        nebel.io.open_photo()
    end
    tip("tip_open")
    x = x + open_w + 6

    -- Prominent Render / Cancel button right next to Open
    local r_w = touch and 84 or 92
    local r_lbl = busy and i18n.t("btn_cancel") or (dirty and i18n.t("btn_render_dirty") or i18n.t("btn_render"))
    if nebel.ui.accent_button(15, x, by, r_w, bh, r_lbl, dirty or busy, -1) then
        if busy then
            nebel.render.cancel()
            nebel.view.set_status(i18n.t("status_cancelled"))
        else
            nebel.render.start()
        end
    end
    tip(busy and "tip_cancel" or "tip_render")
    x = x + r_w + 6

    local rguard = touch and 172 or 488

    -- Sample photos
    for i, s in ipairs(pipeline.default_samples) do
        local w = touch and 58 or 64
        if x + w < vw - rguard then
            if nebel.ui.button(20 + i, x, by, w, bh, i18n.t(s.label_key), false, -1) then
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
                    nebel.view.set_status(i18n.t("status_ready"))
                end
            end
            tip("tip_sample")
            x = x + w + 5
        end
    end

    -- Mask Overlay toggle
    if x + 62 < vw - rguard then
        if nebel.ui.button(29, x, by, 60, bh, i18n.t("btn_mask"), nebel.view.show_mask(), -1) then
            nebel.view.set_show_mask(not nebel.view.show_mask())
        end
        tip("tip_mask")
        x = x + 65
    end

    -- HD 3D Depth trigger
    local depth_lbl = nebel.view.is_hd_depth() and i18n.t("btn_auto3d_done") or i18n.t("btn_auto3d")
    if x + 66 < vw - rguard then
        if nebel.ui.button(30, x, by, 64, bh, depth_lbl, nebel.view.is_neural_depth(), -1) then
            nebel.view.trigger_auto_depth()
        end
        tip("tip_auto3d")
        x = x + 69
    end

    -- Depth View toggle
    if x + 62 < vw - rguard then
        if nebel.ui.button(32, x, by, 60, bh, i18n.t("btn_depth"), nebel.view.show_depth(), -1) then
            nebel.view.set_show_depth(not nebel.view.show_depth())
        end
        tip("tip_depth")
        x = x + 65
    end

    -- Split View toggle
    if x + 60 < vw - rguard then
        if nebel.ui.button(31, x, by, 58, bh, i18n.t("btn_split"), nebel.view.split_enabled(), -1) then
            nebel.view.set_split(not nebel.view.split_enabled())
        end
        tip("tip_split")
        x = x + 63
    end

    -- Right side: Undo, Redo, Exports, Lang, Help(?), Sfx
    local rx = vw - 8

    -- Sfx toggle
    local mw = touch and 42 or 40
    rx = rx - mw
    if nebel.ui.button(40, rx, by, mw, bh, nebel.audio.is_muted() and i18n.t("btn_sfx_off") or i18n.t("btn_sfx_on"), not nebel.audio.is_muted(), -1) then
        nebel.audio.set_muted(not nebel.audio.is_muted())
    end
    tip("tip_sfx")

    -- Help / Tutorial (?)
    rx = rx - 30 - 5
    if nebel.ui.button(47, rx, by, 30, bh, i18n.t("btn_help"), tutorial.open, -1) then
        tutorial.toggle()
    end
    tip("tip_help")

    -- Language Switcher (EN / DE / PL)
    rx = rx - 36 - 5
    if nebel.ui.button(48, rx, by, 36, bh, i18n.t("lang_code"), false, -1) then
        i18n.cycle()
        nebel.view.set_status(i18n.t("status_ready"))
    end
    tip("tip_lang")

    -- Exports
    rx = rx - (touch and 64 or 48) - 5
    if nebel.ui.button(41, rx, by, touch and 64 or 48, bh, "PNG", false, touch and 5 or -1) then
        nebel.io.export_png()
    end
    tip("tip_export_png")

    if not touch then
        rx = rx - 58 - 5
        if nebel.ui.button(45, rx, by, 58, bh, "PNG16", false, -1) then
            nebel.io.export_png16()
        end
        tip("tip_export_png16")

        rx = rx - 52 - 5
        if nebel.ui.button(46, rx, by, 52, bh, "TIFF", false, -1) then
            nebel.io.export_tiff16()
        end
        tip("tip_export_tiff")

        rx = rx - 46 - 5
        if nebel.ui.button(42, rx, by, 46, bh, "JPG", false, -1) then
            nebel.io.export_jpg(92)
        end
        tip("tip_export_jpg")

        rx = rx - 56 - 5
        if nebel.ui.button(43, rx, by, 56, bh, i18n.t("btn_redo"), nebel.view.can_redo(), -1) then
            nebel.view.redo()
        end
        tip("tip_redo")

        rx = rx - 62 - 5
        if nebel.ui.button(44, rx, by, 62, bh, i18n.t("btn_undo"), nebel.view.can_undo(), -1) then
            nebel.view.undo()
        end
        tip("tip_undo")
    end
end

local function draw_bottom_status_bar(vw, vh, touch)
    local ph = nebel.render.phase()
    local busy = is_rendering_phase(ph)
    local tier = nebel.platform.tier()
    local tier_lbl = i18n.t("tier_" .. tier)
    local backend = nebel.platform.gpu_backend()

    if ph == "done" and M.last_phase ~= "done" then
        nebel.view.set_status(i18n.t("status_rendered"))
    end
    M.last_phase = ph

    if touch then return end

    local bar_w = vw - 294
    local by = vh - 26
    if busy then
        local done, total = nebel.render.samples()
        local p = nebel.render.progress()
        local txt = i18n.t("phase_rendering") .. " (" .. done .. "/" .. total .. ") [" .. tier_lbl .. " - " .. backend .. "]"
        if ph == "depth" then
            txt = i18n.t("phase_depth") .. " [" .. tier_lbl .. "]"
        elseif ph == "fluid" then
            txt = i18n.t("phase_fluid") .. " [" .. tier_lbl .. "]"
        end
        nebel.ui.progress_bar(10, by, bar_w - 78, 20, p, txt)
        if nebel.ui.button(890, 10 + bar_w - 72, by, 68, 20, i18n.t("btn_cancel"), false, -1) then
            nebel.render.cancel()
            nebel.view.set_status(i18n.t("status_cancelled"))
        end
        tip("tip_cancel")
    else
        local info = nebel.view.status() .. "   [" .. tier_lbl .. " | " .. backend .. "]"
        nebel.ui.label(12, vh - 22, info, 0.64, 0.72, 0.80, 1.0)
    end
end

local function draw_desktop_sidebar(vw, vh)
    local sw = 286
    local sx = vw - sw
    nebel.ui.panel(sx, 44, sw, vh - 44, 0.09, 0.10, 0.12, 0.96)
    local y = 50
    local pad = 12
    local iw = sw - pad * 2

    -- Hardware Tier + Quality Selector (Fast / Studio / Ultra)
    nebel.ui.label(sx + pad, y, i18n.t("sec_quality"), 0.60, 0.66, 0.74, 1.0)
    y = y + 17
    local cur_q = nebel.render.quality()
    local q_list = {
        { id = "fast",   key = "q_fast" },
        { id = "studio", key = "q_studio" },
        { id = "ultra",  key = "q_ultra" },
    }
    local qw = (iw - 8) / 3
    for i, q in ipairs(q_list) do
        if nebel.ui.button(90 + i, sx + pad + (i - 1) * (qw + 4), y, qw, 22, i18n.t(q.key), cur_q == q.id, -1) then
            nebel.render.set_quality(q.id)
        end
        tip("tip_quality")
    end
    y = y + 27

    -- Tools row + AutoLock brush depth toggle
    nebel.ui.label(sx + pad, y, i18n.t("sec_tools"), 0.60, 0.66, 0.74, 1.0)
    local auto_lock = nebel.brush.get_auto_depth()
    if nebel.ui.button(115, sx + sw - pad - 82, y - 4, 82, 20, i18n.t("btn_autolock"), auto_lock, -1) then
        nebel.brush.set_auto_depth(not auto_lock)
        nebel.view.set_status((not auto_lock) and i18n.t("status_autolock_on") or i18n.t("status_autolock_off"))
    end
    tip("tip_autolock")
    y = y + 18

    local cur_tool = nebel.brush.get_tool()
    local tw = (iw - 12) / 4
    for i, t in ipairs(tools.tools) do
        if nebel.ui.button(100 + i, sx + pad + (i - 1) * (tw + 4), y, tw, 24, i18n.t(t.label_key), cur_tool == t.id, -1) then
            nebel.brush.set_tool(t.id)
        end
        tip(t.tip_key)
    end
    y = y + 28

    -- When viewing Depth Map (D), expose Closer / Farther depth touch-up brushes!
    if nebel.view.show_depth() then
        local dtw = (iw - 6) / 2
        for i, dt in ipairs(tools.depth_tools) do
            if nebel.ui.button(110 + i, sx + pad + (i - 1) * (dtw + 6), y, dtw, 22, i18n.t(dt.label_key), cur_tool == dt.id, -1) then
                nebel.brush.set_tool(dt.id)
            end
            tip(dt.tip_key)
        end
        y = y + 26
    end

    -- Brush size & opacity
    local rad, c1 = nebel.ui.slider(120, sx + pad, y, iw, 26, i18n.t("sl_brush_size"), nebel.brush.get_radius(), 0.01, 0.40)
    tip("tip_brush_size")
    if c1 then nebel.brush.set_radius(rad) end
    y = y + 28
    local op, c2 = nebel.ui.slider(121, sx + pad, y, iw, 26, i18n.t("sl_opacity"), nebel.brush.get_opacity(), 0.05, 1.0)
    tip("tip_opacity")
    if c2 then nebel.brush.set_opacity(op) end
    y = y + 28

    -- Layers section
    nebel.ui.label(sx + pad, y, i18n.t("sec_layers"), 0.60, 0.66, 0.74, 1.0)
    if nebel.ui.button(140, sx + sw - pad - 58, y - 4, 58, 20, i18n.t("btn_add_layer"), false, -1) then
        pipeline.add_named_layer()
    end
    tip("tip_add_layer")
    y = y + 18

    local lcount = nebel.layers.count()
    local active = nebel.layers.active()
    for i = 1, math.min(lcount, 3) do
        local name = nebel.layers.name(i) or ("Layer " .. i)
        local vis = (nebel.layers.get(i, "visible") or 1) >= 0.5
        if nebel.ui.button(150 + i, sx + pad, y, iw - 86, 21, name, i == active, -1) then
            nebel.layers.select(i)
        end
        if nebel.ui.button(155 + i, sx + pad + iw - 82, y, 24, 21, vis and "V" or "-", vis, -1) then
            nebel.layers.set(i, "visible", vis and 0 or 1)
        end
        if nebel.ui.button(160 + i, sx + pad + iw - 54, y, 24, 21, "C", false, -1) then
            nebel.layers.clear(i)
        end
        if lcount > 1 and nebel.ui.button(170 + i, sx + pad + iw - 26, y, 24, 21, "X", false, -1) then
            nebel.layers.remove(i)
        end
        y = y + 24
    end
    y = y + 4

    -- Physics sliders for active layer
    if lcount > 0 then
        nebel.ui.label(sx + pad, y, i18n.t("sec_atmosphere"), 0.60, 0.66, 0.74, 1.0)
        if nebel.ui.button(184, sx + sw - pad - 126, y - 4, 56, 20, i18n.t("btn_seed"), false, -1) then
            local cur = nebel.layers.get(active, "noise_seed") or 1337
            nebel.layers.set(active, "noise_seed", ((cur * 1664525 + 1013904223) % 9000) + 1)
            nebel.view.set_status(i18n.t("status_seed"))
        end
        tip("tip_seed")
        if nebel.ui.button(185, sx + sw - pad - 66, y - 4, 66, 20, i18n.t("btn_skytint"), false, -1) then
            nebel.view.sample_airlight(-1, -1)
        end
        tip("tip_skytint")
        y = y + 18

        for i, sl in ipairs(pipeline.sliders) do
            if y + 26 > vh - 96 then break end
            local val = nebel.layers.get(active, sl.key) or sl.min
            local nv, ch = nebel.ui.slider(200 + i, sx + pad, y, iw, 26, i18n.t(sl.label_key), val, sl.min, sl.max)
            tip(sl.tip_key)
            if ch then
                nebel.layers.set(active, sl.key, nv)
                if sl.key == "depth_min" and nebel.brush.get_auto_depth() then
                    nebel.brush.set_auto_depth(false)
                end
            end
            y = y + 27
        end

        if y + 26 <= vh - 90 then
            local ar = nebel.layers.get(active, "airlight_r") or 0.78
            local ab = nebel.layers.get(active, "airlight_b") or 0.88
            local warmth = math.max(0.0, math.min(1.0, (ar - ab + 0.35) / 0.70))
            local nw, cw = nebel.ui.slider(240, sx + pad, y, iw, 26, i18n.t("sl_warmth"), warmth, 0.0, 1.0)
            tip("tip_warmth")
            if cw then
                nebel.layers.set(active, "airlight_r", 0.68 + 0.30 * nw)
                nebel.layers.set(active, "airlight_g", 0.78 + 0.08 * nw)
                nebel.layers.set(active, "airlight_b", 0.94 - 0.34 * nw)
            end
        end
    end

    -- Presets at bottom of sidebar
    local py = math.max(y + 4, vh - 88)
    nebel.ui.label(sx + pad, py, i18n.t("sec_presets"), 0.60, 0.66, 0.74, 1.0)
    py = py + 16
    local pw = (iw - 6) / 2
    for i, pname in ipairs(presets.order) do
        local col = (i - 1) % 2
        local row = math.floor((i - 1) / 2)
        local plbl = i18n.t(presets.label_keys[pname] or pname)
        if nebel.ui.button(300 + i, sx + pad + col * (pw + 6), py + row * 27, pw, 23, plbl, false, -1) then
            presets.apply(pname)
        end
        tip("tip_preset")
    end
end

local function draw_mobile_drawer(vw, vh)
    local dh = M.drawer_open and 146 or 42
    local dy = vh - dh
    nebel.ui.panel(0, dy, vw, dh, 0.08, 0.09, 0.11, 0.96)

    -- Mobile render progress strip right above drawer if rendering
    local ph = nebel.render.phase()
    if is_rendering_phase(ph) then
        local p = nebel.render.progress()
        nebel.ui.progress_bar(8, dy - 24, vw - 16, 20, p, i18n.t("phase_rendering"))
    end

    local tabs = {
        { "tools",  i18n.t("tab_tools") },
        { "fog",    i18n.t("tab_fog") },
        { "layers", i18n.t("tab_layers") },
    }
    local tw = math.min(92, (vw - 82) / 3)
    for i, tb in ipairs(tabs) do
        if nebel.ui.button(400 + i, 8 + (i - 1) * (tw + 6), dy + 5, tw, 32, tb[2], M.mobile_tab == tb[1], -1) then
            M.mobile_tab = tb[1]
            M.drawer_open = true
        end
    end
    if nebel.ui.button(410, vw - 68, dy + 5, 60, 32, M.drawer_open and i18n.t("btn_hide") or i18n.t("btn_show"), false, -1) then
        M.drawer_open = not M.drawer_open
    end

    if not M.drawer_open then return end
    local cy = dy + 44
    local active = nebel.layers.active()

    if M.mobile_tab == "tools" then
        local bw = (vw - 34) / 4
        local cur = nebel.brush.get_tool()
        for i, t in ipairs(tools.tools) do
            if nebel.ui.button(420 + i, 8 + (i - 1) * (bw + 6), cy, bw, 38, i18n.t(t.label_key), cur == t.id, -1) then
                nebel.brush.set_tool(t.id)
            end
        end
        local r, cr = nebel.ui.slider(430, 10, cy + 48, vw - 20, 36, i18n.t("sl_brush_size"), nebel.brush.get_radius(), 0.01, 0.40)
        if cr then nebel.brush.set_radius(r) end
    elseif M.mobile_tab == "fog" and nebel.layers.count() > 0 then
        local half = (vw - 30) / 2
        local d, cd = nebel.ui.slider(440, 10, cy, half, 36, i18n.t("sl_density"), nebel.layers.get(active, "density") or 1.8, 0.0, 4.0)
        if cd then nebel.layers.set(active, "density", d) end
        local n, cn = nebel.ui.slider(441, 20 + half, cy, half, 36, i18n.t("sl_depth_min"), nebel.layers.get(active, "depth_min") or 0.40, 0.0, 0.95)
        if cn then
            nebel.layers.set(active, "depth_min", n)
            if nebel.brush.get_auto_depth() then nebel.brush.set_auto_depth(false) end
        end
        local s, cs = nebel.ui.slider(442, 10, cy + 44, half, 36, i18n.t("sl_god_rays"), nebel.layers.get(active, "god_rays") or 0.35, 0.0, 1.5)
        if cs then nebel.layers.set(active, "god_rays", s) end
        local h, ch = nebel.ui.slider(443, 20 + half, cy + 44, half, 36, i18n.t("sl_fluid_flow"), nebel.layers.get(active, "fluid_flow") or 0.65, 0.0, 1.0)
        if ch then nebel.layers.set(active, "fluid_flow", h) end
    elseif M.mobile_tab == "layers" then
        if nebel.ui.button(450, 10, cy, 86, 38, i18n.t("btn_add_layer"), false, -1) then
            pipeline.add_named_layer()
        end
        for i = 1, math.min(nebel.layers.count(), 3) do
            if nebel.ui.button(450 + i, 102 + (i - 1) * 90, cy, 84, 38, nebel.layers.name(i) or ("L" .. i), i == active, -1) then
                nebel.layers.select(i)
            end
        end
        local pw = (vw - 34) / 4
        for i, pname in ipairs(presets.order) do
            local plbl = i18n.t(presets.label_keys[pname] or pname)
            if nebel.ui.button(460 + i, 8 + (i - 1) * (pw + 6), cy + 48, pw, 36, plbl:match("^%S+") or plbl, false, -1) then
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
    draw_bottom_status_bar(vw, vh, touch)
    tutorial.draw(vw, vh, touch)
    draw_brush_cursor()
end

return M
