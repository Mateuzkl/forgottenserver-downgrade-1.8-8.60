// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_CLIENTCAPABILITIES_H
#define FS_CLIENTCAPABILITIES_H

#include <cstdint>

// Single source of truth for "what does this client understand".
//
// The server used to answer that question by asking which BRAND connected —
// isOTCv8, isMehah, isAstraClient, isFonticakClient — spread across protocol and
// gameplay code. Five independent booleans allow states that cannot exist on the
// wire (isOTCv8 && isMehah && isAstraClient), and a brand check cannot express
// "this other client happens to speak the same bytes".
//
// Brand is for DETECTION. Capabilities are for ENCODING. The login handshake
// decides a family once, derives a capability set once, and everything after
// that asks about the capability, never the brand.
//
// Protocol 8.60 is unchanged by this type: it exists to describe the conditional
// bytes that already existed, not to introduce new ones.

enum class ClientFamily : uint8_t
{
	// Classic CIP 8.60, with or without DLL patches.
	Cip860,
	// OTClientV8 and its forks. AstraClient is a member of this family: it
	// announces "OTCv8" first and only then adds its own signature marker, so it
	// is an OTCv8 client with extra capabilities, not a separate protocol.
	Otcv8,
	// Mehah-style OTClient, detected by operating system rather than by marker.
	Mehah,
};

// Plain POD. Copied by value, no allocation, no virtual dispatch, no map lookup —
// this is read on the item-serialization hot path, so it must stay trivial.
struct ClientCapabilities
{
	// --- wire layout: these change the bytes on the wire ---

	// Container packets carry pagination fields.
	bool containerPagination = false;
	// Reward chests paginate even when the container itself does not ask for it.
	bool rewardChestPagination = false;
	// Item serialization carries the tier byte (GameItemTierByte).
	bool itemTierByte = false;
	// Item serialization carries the upgrade-classification field
	// (GameThingUpgradeClassification). Distinct from itemTierByte: they are
	// negotiated separately and must never both be assumed from one flag.
	bool thingUpgradeClassification = false;
	// Quiver count is sent as uint16 instead of uint8.
	bool quiverCountU16 = false;
	// Extra per-item state/metadata block.
	bool itemMetadata = false;

	// --- negotiated features: server announces, client may use ---

	bool quickLootFlags = false;
	bool creatureIcons = false;
	bool nativeZoneWeather = false;
	bool outfitStoreMode = false;
	bool hirelingProtocol = false;
	bool monsterPodium = false;
	bool colorizedLoot = false;
	bool characterBazaar = false;

	[[nodiscard]] constexpr bool isOtClientFamily() const noexcept { return otClientFamily; }

	// True only for the OTCv8 family. Kept separate from isOtClientFamily()
	// because Mehah is an OTClient but does not share the OTCv8 marker flow.
	[[nodiscard]] constexpr bool isOtcv8Family() const noexcept { return family == ClientFamily::Otcv8; }
	[[nodiscard]] constexpr bool isMehahFamily() const noexcept { return family == ClientFamily::Mehah; }

	ClientFamily family = ClientFamily::Cip860;
	// Any OTClient-derived client, including ones identified purely by their
	// reported operating system. Broader than family == Otcv8 || family == Mehah.
	bool otClientFamily = false;
};

#endif // FS_CLIENTCAPABILITIES_H
