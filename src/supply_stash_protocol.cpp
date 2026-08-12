// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "supply_stash_protocol.h"

#include "networkmessage.h"

namespace tfs::supply_stash {

namespace {

ParseResult fail(ParseError error) { return ParseResult{error, Request{}}; }

bool isKnownAction(uint8_t value)
{
	switch (static_cast<Action>(value)) {
		case Action::StowItem:
		case Action::StowContainer:
		case Action::StowStack:
		case Action::Withdraw:
			return true;
	}
	return false;
}

} // namespace

ParseResult parseRequest(NetworkMessage& msg)
{
	const uint8_t rawAction = msg.getByte();

	// Reads past the end return 0 rather than throwing, so the overrun flag is
	// the only honest signal that the message was short.
	if (msg.isOverrun()) {
		return fail(ParseError::Truncated);
	}

	if (!isKnownAction(rawAction)) {
		return fail(ParseError::UnknownAction);
	}

	Request request;
	request.action = static_cast<Action>(rawAction);

	switch (request.action) {
		case Action::StowItem:
			request.position = msg.getPosition();
			request.itemId = msg.get<uint16_t>();
			request.stackpos = msg.getByte();
			request.count = msg.get<uint32_t>();
			break;

		case Action::StowContainer:
		case Action::StowStack:
			request.position = msg.getPosition();
			request.itemId = msg.get<uint16_t>();
			request.stackpos = msg.getByte();
			break;

		case Action::Withdraw:
			request.itemId = msg.get<uint16_t>();
			request.count = msg.get<uint32_t>();
			request.tier = msg.getByte();
			break;
	}

	if (msg.isOverrun()) {
		return fail(ParseError::Truncated);
	}

	// Every layout is fixed size, so leftover bytes mean the client and the server
	// disagree about the contract. Rejecting here stops that disagreement being
	// read as the next opcode. Readable data ends at length + INITIAL_BUFFER_POSITION,
	// which is the same bound canRead() enforces.
	//
	// This assumes msg holds a stash request and nothing else. That holds for the
	// current transport, where the payload arrives on its own; if the request is
	// ever embedded in a larger packet, the caller must slice it out first.
	if (msg.getBufferPosition() != msg.getLength() + NetworkMessage::INITIAL_BUFFER_POSITION) {
		return fail(ParseError::TrailingBytes);
	}

	if (request.itemId == 0) {
		return fail(ParseError::InvalidItemId);
	}

	switch (request.action) {
		case Action::StowItem:
			if (request.count == 0 || request.count > MAX_STOW_COUNT) {
				return fail(ParseError::InvalidCount);
			}
			break;

		case Action::Withdraw:
			if (request.count == 0 || request.count > MAX_WITHDRAW_COUNT) {
				return fail(ParseError::InvalidCount);
			}
			if (request.tier > Stash::MAX_ITEM_TIER) {
				return fail(ParseError::InvalidTier);
			}
			break;

		case Action::StowContainer:
		case Action::StowStack:
			break;
	}

	return ParseResult{ParseError::None, request};
}

void serializeContents(NetworkMessage& msg, const std::vector<StashRecord>& rows, uint16_t freeSlots)
{
	msg.add<uint16_t>(static_cast<uint16_t>(rows.size()));
	for (const auto& row : rows) {
		msg.add<uint16_t>(row.itemId);
		msg.add<uint32_t>(row.amount);
		msg.addByte(row.tier);
	}
	msg.add<uint16_t>(freeSlots);
}

} // namespace tfs::supply_stash
