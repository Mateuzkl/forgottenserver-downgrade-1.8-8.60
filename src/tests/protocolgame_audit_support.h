#pragma once

#include "../outputmessage.h"
#include "../protocolgame.h"

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
