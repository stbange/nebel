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

int nebel_utf8_char_count(const char *s) {
    if (!s) return 0;
    int count = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p & 0xC0u) != 0x80u) count++;
    }
    return count;
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
    ui->modal_active = false;
    ui->last_hovered = false;
    ui->hovered_widget = 0;
    ui->has_tooltip = false;
    ui->tooltip_text[0] = '\0';
    ui->cmd_count = 0;
}

static void emit_deferred_tooltip(NebelUiState *ui) {
    if (!ui->has_tooltip || !ui->tooltip_text[0] || ui->viewport_w <= 40 || ui->viewport_h <= 40) {
        return;
    }

    /* Word-wrap UTF-8 text into up to 4 readable lines (~44 codepoints each) */
    char lines[4][NEBEL_UI_TEXT_LEN];
    int line_count = 0;
    int max_chars = 0;
    const char *p = ui->tooltip_text;

    while (*p && line_count < 4) {
        while (*p == ' ') p++;
        if (!*p) break;

        const char *line_start = p;
        const char *last_space = NULL;
        int chars = 0;
        int chars_at_space = 0;

        while (*p && *p != '\n' && chars < 44) {
            if (*p == ' ') {
                last_space = p;
                chars_at_space = chars;
            }
            unsigned char c = (unsigned char)*p;
            if ((c & 0x80u) == 0u) p += 1;
            else if ((c & 0xE0u) == 0xC0u && p[1]) p += 2;
            else if ((c & 0xF0u) == 0xE0u && p[1] && p[2]) p += 3;
            else if ((c & 0xF8u) == 0xF0u && p[1] && p[2] && p[3]) p += 4;
            else p += 1;
            chars++;
        }

        const char *line_end = p;
        int line_chars = chars;
        if (*p && *p != '\n' && last_space && last_space > line_start) {
            line_end = last_space;
            line_chars = chars_at_space;
            p = last_space + 1;
        } else if (*p == '\n') {
            p++;
        }

        size_t len = (size_t)(line_end - line_start);
        if (len >= NEBEL_UI_TEXT_LEN) len = NEBEL_UI_TEXT_LEN - 1u;
        memcpy(lines[line_count], line_start, len);
        lines[line_count][len] = '\0';
        if (line_chars > max_chars) max_chars = line_chars;
        line_count++;
    }

    if (line_count == 0) return;

    float bw = (float)max_chars * 7.6f + 22.0f;
    if (bw < 110.0f) bw = 110.0f;
    if (bw > (float)ui->viewport_w - 16.0f) bw = (float)ui->viewport_w - 16.0f;
    float bh = (float)line_count * 18.0f + 14.0f;

    float tx = ui->mouse_x + 14.0f;
    float ty = ui->mouse_y + 20.0f;
    if (tx + bw > (float)ui->viewport_w - 8.0f) tx = (float)ui->viewport_w - bw - 8.0f;
    if (ty + bh > (float)ui->viewport_h - 8.0f) ty = ui->mouse_y - bh - 10.0f;
    tx = clampf(tx, 8.0f, (float)ui->viewport_w - bw - 8.0f);
    ty = clampf(ty, 8.0f, (float)ui->viewport_h - bh - 8.0f);

    /* Border + card background */
    NebelUiDrawCmd border = {0};
    border.type = NEBEL_UI_CMD_RECT;
    border.x = tx - 1.0f; border.y = ty - 1.0f;
    border.w = bw + 2.0f; border.h = bh + 2.0f;
    border.radius = 7.0f;
    border.color[0] = 0.34f; border.color[1] = 0.50f; border.color[2] = 0.68f; border.color[3] = 0.90f;
    push_cmd(ui, &border);

    NebelUiDrawCmd bg = {0};
    bg.type = NEBEL_UI_CMD_RECT;
    bg.x = tx; bg.y = ty; bg.w = bw; bg.h = bh;
    bg.radius = 6.0f;
    bg.color[0] = 0.06f; bg.color[1] = 0.08f; bg.color[2] = 0.11f; bg.color[3] = 0.97f;
    push_cmd(ui, &bg);

    for (int i = 0; i < line_count; i++) {
        nebel_ui_label(ui, tx + 10.0f, ty + 8.0f + (float)i * 18.0f,
                       lines[i], 0.93f, 0.96f, 0.99f, 1.0f);
    }
}

void nebel_ui_end_frame(NebelUiState *ui) {
    if (!ui) return;
    emit_deferred_tooltip(ui);
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

static bool button_internal(NebelUiState *ui, uint32_t id, float x, float y, float w, float h,
                            const char *label, bool active_highlight, bool accent, int icon_id) {
    if (!ui || id == 0) return false;
    bool hover = pt_in_rect(ui->mouse_x, ui->mouse_y, x, y, w, h);
    ui->last_hovered = hover;
    if (hover) {
        ui->pointer_over_ui = true;
        ui->hovered_widget = id;
    }

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
    if (accent) {
        if (active_highlight) {
            bg.color[0] = hover ? 0.92f : 0.84f;
            bg.color[1] = hover ? 0.56f : 0.48f;
            bg.color[2] = hover ? 0.16f : 0.12f;
            bg.color[3] = 0.98f;
        } else {
            bg.color[0] = hover ? 0.24f : 0.18f;
            bg.color[1] = hover ? 0.58f : 0.48f;
            bg.color[2] = hover ? 0.42f : 0.34f;
            bg.color[3] = 0.96f;
        }
    } else if (active_highlight) {
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
        ic.color[0] = 0.95f; ic.color[1] = 0.97f; ic.color[2] = 0.99f; ic.color[3] = 1.0f;
        push_cmd(ui, &ic);
    }

    if (label && label[0]) {
        float tx = icon_id >= 0 ? (x + h * 0.65f + 10.0f) : (x + 8.0f);
        if (icon_id < 0) {
            float est_w = (float)nebel_utf8_char_count(label) * 7.2f;
            if (est_w + 12.0f < w) {
                tx = x + (w - est_w) * 0.5f;
            }
        }
        nebel_ui_label(ui, tx, y + h * 0.5f - 6.0f, label, 0.95f, 0.97f, 0.99f, 1.0f);
    }
    return clicked;
}

bool nebel_ui_button(NebelUiState *ui, uint32_t id, float x, float y, float w, float h,
                     const char *label, bool active_highlight, int icon_id) {
    return button_internal(ui, id, x, y, w, h, label, active_highlight, false, icon_id);
}

bool nebel_ui_accent_button(NebelUiState *ui, uint32_t id, float x, float y, float w, float h,
                            const char *label, bool pulsing, int icon_id) {
    return button_internal(ui, id, x, y, w, h, label, pulsing, true, icon_id);
}

bool nebel_ui_slider(NebelUiState *ui, uint32_t id, float x, float y, float w, float h,
                     const char *label, float *value, float min_v, float max_v) {
    if (!ui || !value || id == 0 || max_v <= min_v) return false;
    bool hover = pt_in_rect(ui->mouse_x, ui->mouse_y, x, y, w, h);
    ui->last_hovered = hover;
    if (hover) {
        ui->pointer_over_ui = true;
        ui->hovered_widget = id;
    }

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

    NebelUiDrawCmd track = {0};
    track.type = NEBEL_UI_CMD_RECT;
    track.x = x; track.y = y + h * 0.68f; track.w = w; track.h = 5.0f;
    track.radius = 2.5f;
    track.color[0] = 0.22f; track.color[1] = 0.25f; track.color[2] = 0.29f; track.color[3] = 1.0f;
    push_cmd(ui, &track);

    NebelUiDrawCmd fill = track;
    fill.w = w * norm;
    fill.color[0] = 0.36f; fill.color[1] = 0.65f; fill.color[2] = 0.88f; fill.color[3] = 1.0f;
    push_cmd(ui, &fill);

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

void nebel_ui_progress_bar(NebelUiState *ui, float x, float y, float w, float h,
                           float progress, const char *label) {
    if (!ui || w <= 2.0f || h <= 2.0f) return;
    float p = clampf(progress, 0.0f, 1.0f);

    NebelUiDrawCmd bg = {0};
    bg.type = NEBEL_UI_CMD_RECT;
    bg.x = x; bg.y = y; bg.w = w; bg.h = h;
    bg.radius = 5.0f;
    bg.color[0] = 0.12f; bg.color[1] = 0.15f; bg.color[2] = 0.19f; bg.color[3] = 0.96f;
    push_cmd(ui, &bg);

    if (p > 0.005f) {
        NebelUiDrawCmd fill = bg;
        fill.w = w * p;
        fill.color[0] = 0.24f; fill.color[1] = 0.60f; fill.color[2] = 0.86f; fill.color[3] = 0.95f;
        push_cmd(ui, &fill);
    }

    if (label && label[0]) {
        nebel_ui_label(ui, x + 10.0f, y + h * 0.5f - 6.0f, label, 0.95f, 0.97f, 1.0f, 1.0f);
    }
}

void nebel_ui_tooltip(NebelUiState *ui, const char *text) {
    if (!ui || !text || !text[0]) return;
    snprintf(ui->tooltip_text, sizeof(ui->tooltip_text), "%s", text);
    ui->has_tooltip = true;
}
