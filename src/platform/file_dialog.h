#ifndef NEBEL_PLATFORM_FILE_DIALOG_H
#define NEBEL_PLATFORM_FILE_DIALOG_H

#include "core/app_state.h"
#include <SDL3/SDL.h>

void nebel_file_dialog_init(NebelAppState *app);
void nebel_file_dialog_poll(NebelAppState *app, SDL_Window *window);

#endif /* NEBEL_PLATFORM_FILE_DIALOG_H */
