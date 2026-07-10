#include "print_utils.h"
#include "src/common.h"
#include "mmu/recipient_filter.h"

#include <engine/igameeventsystem.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/inetworkserializer.h>
#include <networksystem/netmessage.h>
#include <usermessages.pb.h>

#include <cstdarg>
#include <cstdio>

#define HUD_PRINTTALK 3

static INetworkMessageInternal *GetTextMsgMsg()
{
	static INetworkMessageInternal *s_pMsg = nullptr;
	if (!s_pMsg && g_pNetworkMessages)
	{
		s_pMsg = g_pNetworkMessages->FindNetworkMessagePartial("TextMsg");
	}
	return s_pMsg;
}

static void SendChatToFilter(IRecipientFilter *pFilter, const char *text)
{
	INetworkMessageInternal *pNetMsg = GetTextMsgMsg();
	if (!pNetMsg || !g_pGameEventSystem)
	{
		return;
	}

	CNetMessage *pData = pNetMsg->AllocateMessage();
	if (!pData)
	{
		return;
	}

	auto *pTextMsg = pData->ToPB<CUserMessageTextMsg>();
	pTextMsg->set_dest(HUD_PRINTTALK);
	pTextMsg->add_param(text);

	g_pGameEventSystem->PostEventAbstract(-1, false, pFilter, pNetMsg, pData, 0);
	g_pNetworkMessages->DeallocateNetMessageAbstract(pNetMsg, pData);
}

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

	CSingleRecipientFilter filter(slot);
	SendChatToFilter(&filter, text.c_str());
}
