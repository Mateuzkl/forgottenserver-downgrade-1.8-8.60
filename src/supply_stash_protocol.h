// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_SUPPLY_STASH_PROTOCOL_H
#define FS_SUPPLY_STASH_PROTOCOL_H

#include "position.h"
#include "stash.h"

#include <cstdint>
#include <vector>

class NetworkMessage;

namespace tfs::supply_stash {

// Opcode directions. These are top-level packet opcodes, not extended opcodes,
// and the numbers are only safe because the directions differ:
//
//     C -> S  0x28   Supply Stash actions
//     S -> C  0x28   ReLogin/Death window  (sendReLoginWindow)
//     S -> C  0x29   Supply Stash contents
//
// Never answer a stash request on S -> C 0x28. That is the death/relogin window
// in 8.60 and the client will act on it.
inline constexpr uint8_t CLIENT_REQUEST_OPCODE = 0x28;
inline constexpr uint8_t SERVER_CONTENTS_OPCODE = 0x29;

// Wire values shared with AstraClient. Do not renumber.
//
// These replace the previous Lua numbering (OPEN = 1, STOW_ALL = 2,
// WITHDRAW = 3), so 1 and 2 now mean something different than they used to and
// a client that has not been updated is speaking the old dialect.
//
// That transition is safe only because every layout here is fixed size and the
// parser rejects both short and long messages:
//
//     old OPEN      1 byte  -> this expects 8 more  -> Truncated, rejected
//     old STOW_ALL  1 byte  -> this expects 8 more  -> Truncated, rejected
//     old WITHDRAW  6 bytes -> this expects 7       -> Truncated, rejected
//     old WITHDRAW  7 bytes -> identical layout     -> accepted, same meaning
//
// So an out-of-date client is refused rather than silently misread as an
// item-moving command. test_supply_stash_protocol pins that down; if the length
// guards are ever relaxed, the rollout stops being fail-safe.
//
// Note there is deliberately no OPEN action: opening the stash comes from using
// the stash object, not from a client request.
enum class Action : uint8_t
{
	StowItem = 0,
	StowContainer = 1,
	StowStack = 2,
	Withdraw = 3,
};

enum class ParseError : uint8_t
{
	None,
	Truncated,     // the message ended before the action's fields did
	UnknownAction, // action byte outside the enum
	InvalidItemId, // itemId 0
	InvalidCount,  // count 0, or above the cap
	InvalidTier,   // tier above Stash::MAX_ITEM_TIER
	TrailingBytes, // more bytes than the action's fixed size accounts for
};

// Caps mirror the limits the current Lua implementation already enforces, so
// moving the parser to C++ does not quietly widen what a client may ask for.
inline constexpr uint32_t MAX_STOW_COUNT = 100'000;
inline constexpr uint32_t MAX_WITHDRAW_COUNT = 100'000;

struct Request
{
	Action action = Action::StowItem;
	Position position{}; // stow actions only; unset for Withdraw
	uint16_t itemId = 0;
	uint8_t stackpos = 0; // stow actions only
	uint32_t count = 0;   // StowItem and Withdraw only
	uint8_t tier = 0;     // Withdraw only
};

struct ParseResult
{
	ParseError error = ParseError::None;
	Request request{};

	[[nodiscard]] explicit operator bool() const noexcept { return error == ParseError::None; }
};

// Reads one stash request, action byte first. Never throws and never trusts the
// client: every field is range-checked and the message must end exactly where
// the action's layout says it does.
[[nodiscard]] ParseResult parseRequest(NetworkMessage& msg);

// Writes the stash contents payload, tier-aware, matching what AstraClient
// already knows how to read.
void serializeContents(NetworkMessage& msg, const std::vector<StashRecord>& rows, uint16_t freeSlots);

} // namespace tfs::supply_stash

#endif // FS_SUPPLY_STASH_PROTOCOL_H
