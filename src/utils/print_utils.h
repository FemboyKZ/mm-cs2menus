#ifndef _INCLUDE_MENU_PRINT_UTILS_H_
#define _INCLUDE_MENU_PRINT_UTILS_H_

// Send a formatted chat message (HUD_PRINTTALK) to a single player.
// Used by the chat-menu renderer to draw menu lines.
void MENU_PrintToChat(int slot, const char *fmt, ...);

#endif // _INCLUDE_MENU_PRINT_UTILS_H_
