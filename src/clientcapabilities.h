// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_CLIENTCAPABILITIES_H
#define FS_CLIENTCAPABILITIES_H

#include <cstdint>

// Single source of truth for what a connected client understands.
//
// The server used to track client BRAND as persistent state — isAstraClient,
// isFonticakClient — and branch on it across protocol and gameplay code. Brand
// state cannot express "another client speaks the same bytes", allows impossible
// combinations, and leaks client identity into gameplay.
//
// Brand belongs to DETECTION only, and only for the length of the login
// handshake. Capabilities are what the rest of the server asks about, and every
// one of them is named after the wire FORMAT it controls, never after a client.
//
// The login handshake negotiates these: a client announces the markers it
// understands, the server derives the capability set once, and it is frozen for
// the session. If the server does not enable a capability, the client must not
// expect the bytes.
//
// Protocol 8.60 is unchanged by this type. It describes the conditional bytes
// that already existed.

enum class ClientFamily : uint8_t
{
	// Classic CIP 8.60, with or without DLL patches.
	Cip860,
	// OTClientV8 and its forks.
	Otcv8,
	// Mehah-style OTClient.
	Mehah,
};

// Plain POD: copied by value, no allocation, no virtual dispatch, no lookup.
// Read on the item-serialization hot path, so it must stay trivial.
struct ClientCapabilities
{
	ClientFamily family = ClientFamily::Cip860;

	// Any OTClient-derived client. Broader than family, because some clients are
	// identified purely by the operating system they report.
	bool otClient = false;

	// --- wire layout: these change the bytes on the wire ---

	// Container packets carry pagination fields.
	bool containerPagination = false;
	// Reward chests paginate even when the container does not request it.
	bool rewardChestPagination = false;
	// Item serialization carries the tier byte (GameItemTierByte).
	bool itemTierByte = false;
	// Item serialization carries the upgrade-classification field. Negotiated
	// separately from itemTierByte and must never be inferred from it.
	bool thingUpgradeClassification = false;
	// Quiver count is sent as uint16 instead of uint8.
	bool quiverCountU16 = false;
	// Per-item extended state/metadata block.
	bool itemMetadata = false;
	// Condition icons are sent as a 64-bit field instead of the 8.60 width.
	bool extendedConditionIcons = false;
	// Creature state is drawn with the icon opcode (0x8B) rather than a skull.
	bool creatureIcons = false;

	// --- negotiated features ---

	bool quickLootFlags = false;
	bool lootContainers = false;
	bool colorizedLootText = false;
	bool nativeZoneWeather = false;
	bool outfitStoreMode = false;
	bool hirelingProtocol = false;
	bool monsterPodium = false;
	bool itemInspection = false;
	bool characterBazaar = false;
	bool monkData = false;
	bool blessingSystem = false;
	bool hotkeyEquip = false;
	bool charms = false;
	bool combatAnalyzers = false;
	bool extendedPlayerStats = false;
	bool extendedBasicData = false;
	bool creatureEmblem = false;

	[[nodiscard]] constexpr bool isOtClient() const noexcept { return otClient; }
	[[nodiscard]] constexpr bool isOtcv8Family() const noexcept { return family == ClientFamily::Otcv8; }
	[[nodiscard]] constexpr bool isMehahFamily() const noexcept { return family == ClientFamily::Mehah; }

	// Enables the extended protocol profile negotiated by the extended-profile
	// login marker. Every flag below is a wire format, not a client: any client
	// that sends the marker and a valid signature gets exactly this set, and a
	// client that does not send it gets none of it and must not expect the bytes.
	constexpr void enableExtendedProfile() noexcept
	{
		rewardChestPagination = true;
		quiverCountU16 = true;
		itemMetadata = true;
		extendedConditionIcons = true;
		creatureIcons = true;
		quickLootFlags = true;
		lootContainers = true;
		colorizedLootText = true;
		nativeZoneWeather = true;
		outfitStoreMode = true;
		hirelingProtocol = true;
		monsterPodium = true;
		itemInspection = true;
		characterBazaar = true;
		monkData = true;
		blessingSystem = true;
		hotkeyEquip = true;
		charms = true;
		combatAnalyzers = true;
		extendedPlayerStats = true;
		extendedBasicData = true;
		creatureEmblem = true;
		// Deliberately NOT thingUpgradeClassification: the extended profile carries
		// its own item metadata block and must not also receive the upgrade
		// classification field, or item bytes desynchronise.
	}
};

#endif // FS_CLIENTCAPABILITIES_H
