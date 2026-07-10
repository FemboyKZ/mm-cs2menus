#include "center_html.h"
#include "mmu/log.h"
#include "src/common.h"
#include "src/gamedata.h"
#include "mmu/sigscan.h"
#include "mmu/recipient_filter.h"

#include <igameevents.h>
#include <engine/igameeventsystem.h>
#include <irecipientfilter.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <gameevents.pb.h>

#include <unordered_map>

// Resolved by signature in Init().
// Declared extern in common.h.
IGameEventManager2 *g_pGameEventManager = nullptr;

bool center_html::Init()
{
	if (g_pGameEventManager)
	{
		return true;
	}

	if (!g_pServerGameDLL)
	{
		return false;
	}

	void *base = nullptr;
	size_t size = 0;
	if (!sig::GetModuleRange(g_pServerGameDLL, base, size))
	{
		MMU_LOG_WARN("Could not locate server module - HTML menus disabled.\n");
		return false;
	}

	bool multiple = false;
	void *insn = sig::FindSignatureUnique(base, size, gamedata::kGameEventManagerSig, gamedata::kGameEventManagerSigLen, multiple);
	if (!insn)
	{
		MMU_LOG_WARN("GameEventManager signature not found - HTML menus disabled.\n");
		return false;
	}
	if (multiple)
	{
		// Ambiguous match.
		MMU_LOG_WARN("GameEventManager signature matched multiple times - HTML menus disabled.\n");
		return false;
	}

	g_pGameEventManager = reinterpret_cast<IGameEventManager2 *>(sig::ResolveRipRelative(insn));
	if (!g_pGameEventManager)
	{
		return false;
	}

	MMU_LOG_INFO("GameEventManager resolved - HTML menus available.\n");
	return true;
}

bool center_html::Available()
{
	return g_pGameEventManager != nullptr;
}

// Ceiling on the loc_token payload.
// A net string field is bounded, and an oversized event can be dropped wholesale (or churn bandwidth),
//  so clamp to a generous limit and truncate rather than risk the whole menu silently failing.
static constexpr size_t kMaxHtmlBytes = 8192;

// Reusable event, mutated and re-serialized per send. Released in Shutdown.
static IGameEvent *s_pEvent = nullptr;

void center_html::Shutdown()
{
	if (s_pEvent && g_pGameEventManager)
	{
		g_pGameEventManager->FreeEvent(s_pEvent);
	}
	s_pEvent = nullptr;
}

void center_html::Send(int slot, const char *html, int durationSecs)
{
	if (!g_pGameEventManager || !g_pNetworkMessages || !g_pGameEventSystem)
	{
		return;
	}

	if (!s_pEvent)
	{
		s_pEvent = g_pGameEventManager->CreateEvent("show_survival_respawn_status");
		if (!s_pEvent)
		{
			return;
		}
	}

	// Clamp an over-long payload. Truncation can leave an unclosed tag,
	// which Panorama tolerates, far better than the event being dropped entirely.
	std::string clamped;
	if (html && std::char_traits<char>::length(html) > kMaxHtmlBytes)
	{
		clamped.assign(html, kMaxHtmlBytes);
		html = clamped.c_str();
	}

	s_pEvent->SetString("loc_token", html ? html : "");
	s_pEvent->SetInt("duration", durationSecs);
	s_pEvent->SetInt("userid", -1);

	// The message descriptor is stable for the process lifetime, resolve once.
	static INetworkMessageInternal *s_pMsg = nullptr;
	if (!s_pMsg)
	{
		s_pMsg = g_pNetworkMessages->FindNetworkMessageById(GE_Source1LegacyGameEvent);
	}
	INetworkMessageInternal *pMsg = s_pMsg;
	if (!pMsg)
	{
		return;
	}

	CNetMessagePB<CMsgSource1LegacyGameEvent> *data = pMsg->AllocateMessage()->ToPB<CMsgSource1LegacyGameEvent>();
	g_pGameEventManager->SerializeEvent(s_pEvent, data);

	CSingleRecipientFilter filter(slot);
	g_pGameEventSystem->PostEventAbstract(-1, false, &filter, pMsg, data, 0);

	g_pNetworkMessages->DeallocateNetMessageAbstract(pMsg, data);
}

// Hex for each CS2 chat color control byte, or nullptr if it isn't a color code.
static const char *ChatCodeToHex(unsigned char c)
{
	switch (c)
	{
		case 0x01:
			return "#FFFFFF"; // default
		case 0x02:
			return "#C03030"; // dark red
		case 0x03:
			return "#FF6699"; // team (?)
		case 0x04:
			return "#4CAF50"; // green
		case 0x05:
			return "#9EB825"; // olive
		case 0x06:
			return "#66CC66"; // lime
		case 0x07:
			return "#CC3030"; // red
		case 0x08:
			return "#CCCCCC"; // grey
		case 0x09:
			return "#FFFF00"; // yellow
		case 0x0A:
			return "#B0C4DE"; // blue-grey
		case 0x0B:
			return "#5070FF"; // blue
		case 0x0C:
			return "#3050C0"; // dark blue
		case 0x0D:
			return "#8090A0"; // grey
		case 0x0E:
			return "#E060C0"; // orchid
		case 0x0F:
			return "#FF5050"; // light red
		case 0x10:
			return "#FFD700"; // gold
		default:
			return nullptr;
	}
}

// Append `ch` to `out`, replacing the five markup-significant characters with their
// HTML entities and passing everything else through verbatim.
static void AppendEscaped(std::string &out, unsigned char ch)
{
	// CS2 chat color control bytes (0x01-0x10) aren't valid Panorama markup.
	// ColorizeChat consumes them before reaching here, so dropping them only affects the plain Escape path,
	// where a stray code byte would otherwise leak through raw.
	if (ch >= 0x01 && ch <= 0x10)
	{
		return;
	}
	switch (ch)
	{
		case '&':
			out += "&amp;";
			break;
		case '<':
			out += "&lt;";
			break;
		case '>':
			out += "&gt;";
			break;
		case '"':
			out += "&quot;";
			break;
		case '\'':
			out += "&apos;";
			break;
		default:
			out += static_cast<char>(ch);
			break;
	}
}

std::string center_html::ColorizeChat(const std::string &text, const char *defaultColor, const char *sizeClass)
{
	const char *fallback = defaultColor ? defaultColor : "#FFFFFF";
	std::string out;
	out.reserve(text.size() + 48);

	std::string cur = fallback;
	bool open = false;
	auto openFont = [&]()
	{
		out += "<font color='";
		out += cur;
		out += "' class='";
		out += sizeClass;
		out += "'>";
		open = true;
	};
	auto closeFont = [&]()
	{
		if (open)
		{
			out += "</font>";
			open = false;
		}
	};

	openFont();
	for (unsigned char ch : text)
	{
		if (ch >= 0x01 && ch <= 0x10)
		{
			const char *hex = ChatCodeToHex(ch);
			closeFont();
			cur = hex ? hex : fallback;
			openFont();
			continue;
		}
		AppendEscaped(out, ch);
	}
	closeFont();
	return out;
}

std::string center_html::Escape(const std::string &text)
{
	std::string out;
	out.reserve(text.size() + 16);
	for (unsigned char ch : text)
	{
		AppendEscaped(out, ch);
	}
	return out;
}
