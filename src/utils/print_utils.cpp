#include "print_utils.h"
#include "src/common.h"

#include "mmu/print.h"

#include <cstdarg>
#include <cstdio>
#include <string>

void MENU_PrintToChat(int slot, const char *fmt, ...)
{
	if (!ValidSlot(slot))
	{
		return;
	}

	// Size the buffer to the formatted length so long lines aren't truncated.
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(nullptr, 0, fmt, args);
	va_end(args);
	if (len < 0)
	{
		return;
	}

	// Leading space keeps the first color code from being eaten by the client.
	std::string text(static_cast<size_t>(len) + 2, '\0');
	text[0] = ' ';
	va_start(args, fmt);
	vsnprintf(&text[1], static_cast<size_t>(len) + 1, fmt, args);
	va_end(args);
	text.resize(static_cast<size_t>(len) + 1);

	mmu::SendChatToSlot(slot, text.c_str());
}
