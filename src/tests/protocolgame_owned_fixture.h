#pragma once

#include "../outputmessage.h"
#include "../protocolgame.h"

// Match production ownership: requesting output can register the protocol for
// autosend, including when an existing buffer needs its timer retried.
struct ProtocolGameOwnedFixture
{
	std::shared_ptr<ProtocolGame> owner = std::make_shared<ProtocolGame>(nullptr);
	~ProtocolGameOwnedFixture() { OutputMessagePool::getInstance().removeProtocolFromAutosend(owner); }
};
