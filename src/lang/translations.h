#ifndef _INCLUDE_MENU_TRANSLATIONS_H_
#define _INCLUDE_MENU_TRANSLATIONS_H_

#include "mmu/translations.h"

// SourceMod-style phrase tables for the built-in menu chrome labels
// (Exit / page nav / footer hints), resolved per client language.
// Only labels are translated.
// Item text and titles are passed through verbatim, the consumer localizes those itself.
// Color tags stay literal, menu text is rendered as HTML rather than chat lines.
extern mmu::Translations g_Translations;

#endif // _INCLUDE_MENU_TRANSLATIONS_H_
