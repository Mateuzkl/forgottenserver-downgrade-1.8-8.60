#pragma once

#include "../outputmessage.h"
#include "../protocolgame.h"

// Only the operations used by baseline-compatible red tests. New candidate
// cache/metrics helpers must not require members absent from baseline headers.
struct ProtocolGameAuditTestAccess
{
	static void bind(ProtocolGame& protocol, const std::shared_ptr<Player>& player)
	{
		protocol.player = player;
		protocol.getCurrentBuffer() = std::make_shared<OutputMessage>();
	}
	static bool canSee(const ProtocolGame& protocol, const Position& position)
	{
		return protocol.canSee(position);
	}
};
