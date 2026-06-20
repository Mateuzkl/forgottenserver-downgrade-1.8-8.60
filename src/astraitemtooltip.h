// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_ASTRAITEMTOOLTIP_H
#define FS_ASTRAITEMTOOLTIP_H

#include <cstdint>

// Binary, server-authoritative item tooltip protocol for the AstraClient.
//
// This is a *normal* ProtocolGame packet (NOT an extended opcode, NOT JSON,
// NOT a textual payload). The client sends a small fixed request; the server
// validates ownership/visibility and answers with a flag-gated binary payload.
//
// Wire contract (shared with the AstraClient side):
//   Client -> Server: ClientOpcodeRequest, then the request fields below.
//   Server -> Client: ServerOpcodeResponse, requestId(u8), status(u8); when the
//                     status is STATUS_OK a length-prefixed payload follows so
//                     the client can always re-align even on a parse mismatch.
namespace AstraItemTooltip {

// Opcode bytes chosen from the free 0x33-0x3F band (verified free on both the
// server parsePacket switch and the AstraClient GameServerOpcodes/ClientOpcodes
// enums). Keep these in sync with the client's protocolcodes.h.
inline constexpr uint8_t ClientOpcodeRequest = 0x35;  // client -> server
inline constexpr uint8_t ServerOpcodeResponse = 0x36; // server -> client

// Response status. When status != STATUS_OK the client stops reading the packet.
enum Status : uint8_t
{
	STATUS_OK = 0,
	STATUS_NOT_FOUND = 1,
	STATUS_DISABLED = 2,
	STATUS_DENIED = 3,
	STATUS_INVALID = 4,
};

// How the client located the hovered item; decides how the server resolves and
// permission-checks it.
enum SourceType : uint8_t
{
	SOURCE_INVENTORY = 1, // an equipment slot owned by the player
	SOURCE_CONTAINER = 2, // a slot inside a container the player has open
	SOURCE_ACTIONBAR = 3, // an actionbar button (resolved by client id, best effort)
	SOURCE_GENERIC = 4,   // a bare client id; static ItemType data only
	SOURCE_GROUND = 5,    // reserved for a future safe tile lookup; not served yet
};

// Section bitmask sent as a uint32 right after the header fields. The client
// reads each optional section only when its flag is set.
enum Flags : uint32_t
{
	FLAG_HAS_INSTANCE_DATA = 1u << 0, // payload carries real per-item data, not just the type
	FLAG_HAS_STATS = 1u << 1,         // attack/defense/extraDefense/armor/hitChance/shootRange
	FLAG_HAS_REQUIREMENTS = 1u << 2,  // required level/magic level + vocation string
	FLAG_HAS_IMBUEMENTS = 1u << 3,    // imbuement slot list
	FLAG_HAS_AUGMENTS = 1u << 4,      // augment list
	FLAG_HAS_IMPLICITS = 1u << 5,     // pre-formatted ability lines
	FLAG_HAS_CHARGES = 1u << 6,       // charges (u32)
	FLAG_HAS_DURATION = 1u << 7,      // duration seconds (u32) + paused flag (u8)
	FLAG_IS_CONTAINER = 1u << 8,      // container capacity (u16)
	FLAG_IS_WEAPON = 1u << 9,         // visual hint: weapon
	FLAG_IS_ARMOR = 1u << 10,         // visual hint: armor/wearable
};

// Item category sent as a single byte (port of the reference formatItemType).
enum Category : uint8_t
{
	CATEGORY_COMMON = 0,
	CATEGORY_SWORD = 1,
	CATEGORY_CLUB = 2,
	CATEGORY_AXE = 3,
	CATEGORY_TWO_HANDED_SWORD = 4,
	CATEGORY_TWO_HANDED_CLUB = 5,
	CATEGORY_TWO_HANDED_AXE = 6,
	CATEGORY_DISTANCE = 7,
	CATEGORY_WAND = 8,
	CATEGORY_SHIELD = 9,
	CATEGORY_HELMET = 10,
	CATEGORY_NECKLACE = 11,
	CATEGORY_ARMOR = 12,
	CATEGORY_LEGS = 13,
	CATEGORY_BOOTS = 14,
	CATEGORY_RING = 15,
	CATEGORY_AMMUNITION = 16,
	CATEGORY_RUNE = 17,
	CATEGORY_CONTAINER = 18,
	CATEGORY_POTION = 19,
	CATEGORY_USABLE = 20,
};

// Server-side flood protection: ignore tooltip requests from the same player
// that arrive faster than this. The client also throttles via a hover delay.
inline constexpr int64_t REQUEST_COOLDOWN_MS = 120;

// A request parsed on the network thread and handed to the game thread. Only
// trivially-copyable scalars so it can be captured by value into a task.
struct Request
{
	uint8_t requestId = 0;
	uint8_t sourceType = 0;
	uint16_t clientId = 0;
	uint8_t inventorySlot = 0;
	uint8_t containerId = 0;
	uint16_t slotIndex = 0;
	uint8_t actionbarId = 0;
	uint8_t actionbarSlot = 0;
	uint16_t fallbackClientId = 0;
	uint8_t fallbackTier = 0;
	uint8_t tier = 0;
};

} // namespace AstraItemTooltip

#endif // FS_ASTRAITEMTOOLTIP_H
