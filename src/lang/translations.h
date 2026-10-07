#ifndef _INCLUDE_MENU_TRANSLATIONS_H_
#define _INCLUDE_MENU_TRANSLATIONS_H_

#include "utils/translations.h"

// SourceMod-style phrase tables for the built-in menu chrome labels
// (Exit / page nav / footer hints), the preferences menu and its command replies, resolved per client language.
// Of a consumer's menu only the labels are translated.
// Item text and titles are passed through verbatim, the consumer localizes those itself.
// Color tags stay literal, menu text is rendered as HTML rather than chat lines.
extern mmu::Translations g_Translations;

#endif // _INCLUDE_MENU_TRANSLATIONS_H_
