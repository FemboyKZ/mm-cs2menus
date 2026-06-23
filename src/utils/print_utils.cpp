#include "print_utils.h"
#include "src/common.h"
#include "src/utils/recipient_filter.h"

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
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}

	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	// Leading space keeps the first color code from being eaten by the client.
	char chatBuf[600];
	snprintf(chatBuf, sizeof(chatBuf), " %s", buf);

	CSingleRecipientFilter filter(slot);
	SendChatToFilter(&filter, chatBuf);
}
