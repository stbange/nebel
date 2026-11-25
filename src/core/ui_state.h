#ifndef NEBEL_CORE_UI_STATE_H
#define NEBEL_CORE_UI_STATE_H

#include <stdbool.h>
#include <stdint.h>

#define NEBEL_UI_MAX_CMDS 512
#define NEBEL_UI_TEXT_LEN 64

typedef enum {
    NEBEL_UI_CMD_RECT = 0,
    NEBEL_UI_CMD_TEXT = 1,
    NEBEL_UI_CMD_ICON = 2,
    NEBEL_UI_CMD_RING = 3
} NebelUiCmdType;

typedef struct {
    NebelUiCmdType type;
    float x, y, w, h;
    float radius;
    float color[4];
    int icon_id;
    char text[NEBEL_UI_TEXT_LEN];
} NebelUiDrawCmd;

typedef struct {
    int viewport_w;
    int viewport_h;
    float mouse_x;
    float mouse_y;
    bool mouse_down;
    bool mouse_pressed;
    bool mouse_released;
    bool pointer_over_ui;
    uint32_t active_widget;
    NebelUiDrawCmd cmds[NEBEL_UI_MAX_CMDS];
    int cmd_count;
} NebelUiState;

void nebel_ui_init(NebelUiState *ui);
void nebel_ui_begin_frame(NebelUiState *ui, int vw, int vh,
                          float mx, float my, bool down, bool pressed, bool released);
void nebel_ui_end_frame(NebelUiState *ui);

void nebel_ui_panel(NebelUiState *ui, float x, float y, float w, float h,
                    float r, float g, float b, float a);
void nebel_ui_label(NebelUiState *ui, float x, float y, const char *text,
                    float r, float g, float b, float a);
void nebel_ui_ring(NebelUiState *ui, float cx, float cy, float radius,
                   float r, float g, float b, float a);

bool nebel_ui_button(NebelUiState *ui, uint32_t id, float x, float y, float w, float h,
                     const char *label, bool active_highlight, int icon_id);

bool nebel_ui_slider(NebelUiState *ui, uint32_t id, float x, float y, float w, float h,
                     const char *label, float *value, float min_v, float max_v);

#endif /* NEBEL_CORE_UI_STATE_H */
