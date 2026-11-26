-- Nebel Compact 4-Step Quick-Start Tutorial Window (EN / DE / PL) (MIT License)

local i18n = require("i18n")

local M = {
    open = true,
    step = 1,
    total_steps = 4,
}

function M.toggle()
    M.open = not M.open
    if M.open and (M.step < 1 or M.step > M.total_steps) then
        M.step = 1
    end
end

function M.close()
    M.open = false
end

function M.draw(vw, vh, touch)
    if not M.open then return end

    local cw = touch and math.min(vw - 20, 370) or 430
    local ch = touch and 196 or 184
    local cx = touch and ((vw - cw) * 0.5) or 16
    local cy = touch and math.max(58, vh - 156 - ch) or math.max(54, vh - ch - 42)

    -- Border + frosted dark card
    nebel.ui.panel(cx - 1, cy - 1, cw + 2, ch + 2, 0.30, 0.52, 0.72, 0.92)
    nebel.ui.panel(cx, cy, cw, ch, 0.07, 0.09, 0.12, 0.96)

    local pad = 12
    local title = i18n.t("tut_title") .. " (" .. M.step .. "/" .. M.total_steps .. ")"
    nebel.ui.label(cx + pad, cy + 10, title, 0.56, 0.80, 0.98, 1.0)

    if nebel.ui.button(900, cx + cw - 92, cy + 6, 82, 22, i18n.t("tut_skip"), false, -1) then
        M.open = false
        return
    end

    local sk = "tut_step" .. M.step .. "_"
    nebel.ui.label(cx + pad, cy + 38, i18n.t(sk .. "h"), 0.96, 0.97, 0.99, 1.0)
    nebel.ui.label(cx + pad, cy + 64, i18n.t(sk .. "b1"), 0.80, 0.85, 0.91, 1.0)
    nebel.ui.label(cx + pad, cy + 84, i18n.t(sk .. "b2"), 0.80, 0.85, 0.91, 1.0)
    nebel.ui.label(cx + pad, cy + 104, i18n.t(sk .. "b3"), 0.80, 0.85, 0.91, 1.0)

    -- Step dots
    for i = 1, M.total_steps do
        local dx = cx + pad + (i - 1) * 16
        local dy = cy + ch - 52
        if i == M.step then
            nebel.ui.panel(dx, dy, 10, 6, 0.36, 0.72, 0.96, 1.0)
        else
            nebel.ui.panel(dx, dy, 10, 6, 0.24, 0.28, 0.34, 0.9)
        end
    end

    -- Bottom action buttons: Online Help | Back | Next/Start
    local by = cy + ch - 36
    local bh = 26
    if nebel.ui.button(901, cx + pad, by, 134, bh, i18n.t("tut_open_help"), false, -1) then
        nebel.platform.open_help(i18n.lang())
    end
    if M.step > 1 then
        if nebel.ui.button(902, cx + cw - 156, by, 68, bh, i18n.t("tut_back"), false, -1) then
            M.step = M.step - 1
        end
    end
    local next_lbl = (M.step < M.total_steps) and i18n.t("tut_next") or i18n.t("tut_done")
    if nebel.ui.accent_button(903, cx + cw - 82, by, 72, bh, next_lbl, M.step == M.total_steps, -1) then
        if M.step < M.total_steps then
            M.step = M.step + 1
        else
            M.open = false
        end
    end
end

return M
