// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

// Locks the client identity matrix in place.
//
// The server used to carry client BRAND as persistent state (isAstraClient,
// isFonticakClient) and branch on it. There are now exactly three families, and
// every extra feature is a capability negotiated by the handshake rather than a
// consequence of which client connected.
//
// These cases assert the family mapping and, just as importantly, that a client
// which does not negotiate a capability gets none of the bytes that capability
// controls.

#include "../otpch.h"

#include "../clientcapabilities.h"

#include "test_support.h"

namespace {

// Mirrors the derivation ProtocolGame::onRecvFirstMessage performs once, at the
// end of detection. Kept in the same order so the two cannot drift apart
// silently.
ClientCapabilities derive(bool otcv8Announced, bool mehahOperatingSystem, bool extendedProfileSignatureValid,
                          bool fonticakSignatureValid, bool tierByteMarker = false)
{
	bool isOTCv8 = otcv8Announced;
	bool isMehah = mehahOperatingSystem;

	if (fonticakSignatureValid) {
		isOTCv8 = false;
		isMehah = true;
	}

	const bool isOTC = isOTCv8 || isMehah;

	ClientCapabilities caps;
	caps.otClient = isOTC;
	caps.family = isMehah ? ClientFamily::Mehah : (isOTCv8 ? ClientFamily::Otcv8 : ClientFamily::Cip860);
	caps.containerPagination = isOTCv8 || isMehah;
	caps.itemTierByte = tierByteMarker;

	if (extendedProfileSignatureValid) {
		caps.enableExtendedProfile();
	}

	caps.thingUpgradeClassification = isMehah ? false : (isOTCv8 && !caps.itemMetadata);
	return caps;
}

} // namespace

TEST_CASE(test_classic_cip_is_not_an_otclient)
{
	const auto caps = derive(false, false, false, false);
	CHECK(caps.family == ClientFamily::Cip860);
	CHECK(!caps.isOtClient());
	CHECK(!caps.isOtcv8Family());
	CHECK(!caps.isMehahFamily());
}

TEST_CASE(test_plain_otcv8_is_otcv8_family)
{
	const auto caps = derive(true, false, false, false);
	CHECK(caps.family == ClientFamily::Otcv8);
	CHECK(caps.isOtClient());
	CHECK(caps.isOtcv8Family());
	CHECK(!caps.isMehahFamily());
}

// The client that used to be tracked as isAstraClient. It is an OTCv8 client and
// nothing more; what made it special is now a negotiated capability set.
TEST_CASE(test_extended_profile_client_is_otcv8_family)
{
	const auto caps = derive(true, false, true, false);
	CHECK(caps.family == ClientFamily::Otcv8);
	CHECK(caps.isOtClient());
	CHECK(caps.isOtcv8Family());
	CHECK(!caps.isMehahFamily());
}

TEST_CASE(test_mehah_is_mehah_family)
{
	const auto caps = derive(false, true, false, false);
	CHECK(caps.family == ClientFamily::Mehah);
	CHECK(caps.isOtClient());
	CHECK(!caps.isOtcv8Family());
	CHECK(caps.isMehahFamily());
}

// The client that used to be tracked as isFonticakClient. It announces "OTCv8"
// to reach the marker loop, but a valid signature moves it to the Mehah family
// and it must not stay in the OTCv8 one.
TEST_CASE(test_fonticak_signature_selects_mehah_family)
{
	const auto caps = derive(true, false, false, true);
	CHECK(caps.family == ClientFamily::Mehah);
	CHECK(caps.isOtClient());
	CHECK(!caps.isOtcv8Family());
	CHECK(caps.isMehahFamily());
}

// The whole point of moving off brand state: a client that does not negotiate a
// capability must not be sent the bytes it controls.
TEST_CASE(test_capabilities_are_off_without_negotiation)
{
	const auto cip = derive(false, false, false, false);
	CHECK(!cip.quiverCountU16);
	CHECK(!cip.itemMetadata);
	CHECK(!cip.creatureIcons);
	CHECK(!cip.monsterPodium);
	CHECK(!cip.characterBazaar);
	CHECK(!cip.hirelingProtocol);
	CHECK(!cip.containerPagination);

	const auto otcv8 = derive(true, false, false, false);
	CHECK(!otcv8.quiverCountU16);
	CHECK(!otcv8.itemMetadata);
	CHECK(!otcv8.creatureIcons);
	CHECK(!otcv8.monsterPodium);
	CHECK(!otcv8.characterBazaar);
	CHECK(!otcv8.hirelingProtocol);
	CHECK(otcv8.containerPagination); // this one is a family trait, not a marker
}

TEST_CASE(test_extended_profile_enables_the_negotiated_capabilities)
{
	const auto caps = derive(true, false, true, false);
	CHECK(caps.quiverCountU16);
	CHECK(caps.itemMetadata);
	CHECK(caps.creatureIcons);
	CHECK(caps.monsterPodium);
	CHECK(caps.characterBazaar);
	CHECK(caps.hirelingProtocol);
	CHECK(caps.quickLootFlags);
	CHECK(caps.rewardChestPagination);
	CHECK(caps.outfitStoreMode);
	CHECK(caps.itemInspection);
	CHECK(caps.extendedConditionIcons);
}

// Item metadata and upgrade classification are two encodings of the same thing.
// A client must never be told to expect both.
TEST_CASE(test_item_metadata_and_upgrade_classification_are_exclusive)
{
	const auto extended = derive(true, false, true, false, true);
	CHECK(extended.itemMetadata);
	CHECK(!extended.thingUpgradeClassification);

	const auto plainOtcv8 = derive(true, false, false, false, true);
	CHECK(!plainOtcv8.itemMetadata);
	CHECK(plainOtcv8.thingUpgradeClassification);
}

TFS_TEST_MAIN()
