/***************************************************************************
 * Shanling M3X simulator controls. GPL version 2 or later.
 ****************************************************************************/
#include "config.h"
#include <SDL.h>
#include "button.h"
#include "buttonmap.h"

int key_to_button(int keyboard_button)
{
    switch (keyboard_button)
    {
        case SDLK_ESCAPE: return BUTTON_POWER;
        case SDLK_SPACE: return BUTTON_PLAY;
        case SDLK_LEFT: return BUTTON_LEFT;
        case SDLK_RIGHT: return BUTTON_RIGHT;
        case SDLK_UP:
        case SDLK_EQUALS:
        case SDLK_KP_PLUS: return BUTTON_VOL_UP;
        case SDLK_DOWN:
        case SDLK_MINUS:
        case SDLK_KP_MINUS: return BUTTON_VOL_DOWN;
        default: return BUTTON_NONE;
    }
}

struct button_map bm[] = {
    { SDLK_ESCAPE, 0, 0, 0, "Power" },
    { SDLK_SPACE, 0, 0, 0, "Play / Pause" },
    { SDLK_LEFT, 0, 0, 0, "Previous" },
    { SDLK_RIGHT, 0, 0, 0, "Next" },
    { SDLK_UP, 0, 0, 0, "Volume Up" },
    { SDLK_DOWN, 0, 0, 0, "Volume Down" },
    { 0, 0, 0, 0, "" }
};
