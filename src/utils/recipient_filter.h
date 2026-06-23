#ifndef _INCLUDE_MENU_RECIPIENT_FILTER_H_
#define _INCLUDE_MENU_RECIPIENT_FILTER_H_

#include <irecipientfilter.h>

// Reliable recipient filter targeting exactly one player slot.
class CSingleRecipientFilter : public IRecipientFilter
{
public:
	explicit CSingleRecipientFilter(int slot)
	{
		m_recipients.Set(slot);
	}

	~CSingleRecipientFilter() override {}

	NetChannelBufType_t GetNetworkBufType() const override
	{
		return BUF_RELIABLE;
	}

	bool IsInitMessage() const override
	{
		return false;
	}

	const CPlayerBitVec &GetRecipients() const override
	{
		return m_recipients;
	}

	CPlayerSlot GetPredictedPlayerSlot() const override
	{
		return CPlayerSlot(-1);
	}

private:
	CPlayerBitVec m_recipients;
};

#endif // _INCLUDE_MENU_RECIPIENT_FILTER_H_
