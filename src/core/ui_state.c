#include "core/ui_state.h"

#include <stdio.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool pt_in_rect(float px, float py, float x, float y, float w, float h) {
    return px >= x && px <= (x + w) && py >= y && py <= (y + h);
}

static void push_cmd(NebelUiState *ui, const NebelUiDrawCmd *cmd) {
    if (!ui || !cmd || ui->cmd_count >= NEBEL_UI_MAX_CMDS) return;
    ui->cmds[ui->cmd_count++] = *cmd;
}

void nebel_ui_init(NebelUiState *ui) {
    if (!ui) return;
    memset(ui, 0, sizeof(*ui));
}

void nebel_ui_begin_frame(NebelUiState *ui, int vw, int vh,
                          float mx, float my, bool down, bool pressed, bool released) {
    if (!ui) return;
    ui->viewport_w = vw;
    ui->viewport_h = vh;
    ui->mouse_x = mx;
    ui->mouse_y = my;
    ui->mouse_down = down;
    ui->mouse_pressed = pressed;
    ui->mouse_released = released;
    ui->pointer_over_ui = false;
    ui->cmd_count = 0;
}

void nebel_ui_end_frame(NebelUiState *ui) {
    if (!ui) return;
    if (ui->mouse_released || !ui->mouse_down) {
        ui->active_widget = 0;
    }
}

void nebel_ui_panel(NebelUiState *ui, float x, float y, float w, float h,
                    float r, float g, float b, float a) {
    if (!ui) return;
    if (pt_in_rect(ui->mouse_x, ui->mouse_y, x, y, w, h)) {
        ui->pointer_over_ui = true;
    }
    NebelUiDrawCmd cmd = {0};
    cmd.type = NEBEL_UI_CMD_RECT;
    cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
    cmd.radius = 6.0f;
    cmd.color[0] = r; cmd.color[1] = g; cmd.color[2] = b; cmd.color[3] = a;
    push_cmd(ui, &cmd);
}

void nebel_ui_label(NebelUiState *ui, float x, float y, const char *text,
                    float r, float g, float b, float a) {
    if (!ui || !text) return;
    NebelUiDrawCmd cmd = {0};
    cmd.type = NEBEL_UI_CMD_TEXT;
    cmd.x = x; cmd.y = y;
    cmd.color[0] = r; cmd.color[1] = g; cmd.color[2] = b; cmd.color[3] = a;
    snprintf(cmd.text, sizeof(cmd.text), "%s", text);
    push_cmd(ui, &cmd);
}

void nebel_ui_ring(NebelUiState *ui, float cx, float cy, float radius,
                   float r, float g, float b, float a) {
    if (!ui || radius <= 0.0f) return;
    NebelUiDrawCmd cmd = {0};
    cmd.type = NEBEL_UI_CMD_RING;
    cmd.x = cx; cmd.y = cy; cmd.radius = radius;
    cmd.color[0] = r; cmd.color[1] = g; cmd.color[2] = b; cmd.color[3] = a;
    push_cmd(ui, &cmd);
}

bool nebel_ui_button(NebelUiState *ui, uint32_t id, float x, float y, float w, float h,
                     const char *label, bool active_highlight, int icon_id) {
    if (!ui || id == 0) return false;
    bool hover = pt_in_rect(ui->mouse_x, ui->mouse_y, x, y, w, h);
    if (hover) ui->pointer_over_ui = true;

    bool clicked = false;
    if (hover && ui->mouse_pressed) {
        ui->active_widget = id;
    }
    if (ui->active_widget == id && ui->mouse_released && hover) {
        clicked = true;
    }

    NebelUiDrawCmd bg = {0};
    bg.type = NEBEL_UI_CMD_RECT;
    bg.x = x; bg.y = y; bg.w = w; bg.h = h;
    bg.radius = 5.0f;
    if (active_highlight) {
        bg.color[0] = 0.25f; bg.color[1] = 0.48f; bg.color[2] = 0.68f; bg.color[3] = 0.95f;
    } else if (hover) {
        bg.color[0] = 0.24f; bg.color[1] = 0.27f; bg.color[2] = 0.31f; bg.color[3] = 0.95f;
    } else {
        bg.color[0] = 0.16f; bg.color[1] = 0.18f; bg.color[2] = 0.21f; bg.color[3] = 0.92f;
    }
    push_cmd(ui, &bg);

    if (icon_id >= 0) {
        NebelUiDrawCmd ic = {0};
        ic.type = NEBEL_UI_CMD_ICON;
        ic.icon_id = icon_id;
        float isz = h * 0.56f;
        ic.x = label && label[0] ? (x + 8.0f) : (x + (w - isz) * 0.5f);
        ic.y = y + (h - isz) * 0.5f;
        ic.w = isz; ic.h = isz;
        ic.color[0] = 0.92f; ic.color[1] = 0.94f; ic.color[2] = 0.96f; ic.color[3] = 1.0f;
        push_cmd(ui, &ic);
    }

    if (label && label[0]) {
        float tx = icon_id >= 0 ? (x + h * 0.65f + 10.0f) : (x + 10.0f);
        nebel_ui_label(ui, tx, y + h * 0.5f - 6.0f, label, 0.93f, 0.95f, 0.97f, 1.0f);
    }
    return clicked;
}

bool nebel_ui_slider(NebelUiState *ui, uint32_t id, float x, float y, float w, float h,
                     const char *label, float *value, float min_v, float max_v) {
    if (!ui || !value || id == 0 || max_v <= min_v) return false;
    bool hover = pt_in_rect(ui->mouse_x, ui->mouse_y, x, y, w, h);
    if (hover) ui->pointer_over_ui = true;

    if (hover && ui->mouse_pressed) {
        ui->active_widget = id;
    }

    bool changed = false;
    if (ui->active_widget == id && ui->mouse_down && w > 1.0f) {
        ui->pointer_over_ui = true;
        float t = clampf((ui->mouse_x - x) / w, 0.0f, 1.0f);
        float next = min_v + t * (max_v - min_v);
        if (next != *value) {
            *value = next;
            changed = true;
        }
    }

    float norm = clampf((*value - min_v) / (max_v - min_v), 0.0f, 1.0f);

    /* Track background */
    NebelUiDrawCmd track = {0};
    track.type = NEBEL_UI_CMD_RECT;
    track.x = x; track.y = y + h * 0.68f; track.w = w; track.h = 5.0f;
    track.radius = 2.5f;
    track.color[0] = 0.22f; track.color[1] = 0.25f; track.color[2] = 0.29f; track.color[3] = 1.0f;
    push_cmd(ui, &track);

    /* Fill */
    NebelUiDrawCmd fill = track;
    fill.w = w * norm;
    fill.color[0] = 0.36f; fill.color[1] = 0.65f; fill.color[2] = 0.88f; fill.color[3] = 1.0f;
    push_cmd(ui, &fill);

    /* Thumb */
    NebelUiDrawCmd thumb = {0};
    thumb.type = NEBEL_UI_CMD_RECT;
    thumb.w = 10.0f; thumb.h = 13.0f;
    thumb.x = x + w * norm - 5.0f;
    thumb.y = y + h * 0.68f - 4.0f;
    thumb.radius = 3.5f;
    thumb.color[0] = 0.93f; thumb.color[1] = 0.96f; thumb.color[2] = 0.99f; thumb.color[3] = 1.0f;
    push_cmd(ui, &thumb);

    if (label && label[0]) {
        char buf[NEBEL_UI_TEXT_LEN];
        snprintf(buf, sizeof(buf), "%s: %.2f", label, *value);
        nebel_ui_label(ui, x, y, buf, 0.85f, 0.88f, 0.92f, 1.0f);
    }
    return changed;
}
