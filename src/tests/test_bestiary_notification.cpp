#include "../otpch.h"

#include "../astraclient.h"
#include "../bestiary_charm.h"
#include "../networkmessage.h"
#include "../protocolgame.h"

#include "test_support.h"

namespace {

BestiaryCreatureInfo makeCreature(uint16_t raceId, std::string name, uint16_t lookType)
{
	BestiaryCreatureInfo info;
	info.raceId = raceId;
	info.name = std::move(name);
	info.lookType = lookType;
	info.lookHead = 11;
	info.lookBody = 22;
	info.lookLegs = 33;
	info.lookFeet = 44;
	info.lookAddons = 3;
	return info;
}

void checkPacketEnd(const NetworkMessage& msg)
{
	CHECK(msg.getBufferPosition() == NetworkMessage::INITIAL_BUFFER_POSITION + msg.getLength());
}

} // namespace

TEST_CASE(bestiary_notification_feature_ids_are_stable)
{
	CHECK(static_cast<uint8_t>(AstraClient::BestiaryBannerCreatureData) == (1U << 5));
	CHECK(static_cast<uint8_t>(GameFeature::AstraBestiaryBannerCreatureData) == 151);
}

TEST_CASE(legacy_bestiary_notification_keeps_original_layout)
{
	const auto info = makeCreature(500, "Notification Test Rat", 21);
	NetworkMessage msg;
	CHECK(BestiaryNotificationProtocol::writeProgress(msg, info, 2, false, false));
	CHECK(msg.getLength() == 5);
	CHECK(msg.setBufferPosition(0));
	CHECK(msg.getByte() == 0x75);
	CHECK(msg.getByte() == SCREENSHOT_AND_BANNER_TYPE_BESTIARY_PROGRESS);
	CHECK(msg.get<uint16_t>() == 500);
	CHECK(msg.getByte() == 2);
	checkPacketEnd(msg);
}

TEST_CASE(enhanced_bestiary_notification_uses_authoritative_identity)
{
	const auto info = makeCreature(500, "Notification Test Rat", 21);
	NetworkMessage msg;
	CHECK(BestiaryNotificationProtocol::writeProgress(msg, info, 3, false, true));
	CHECK(msg.setBufferPosition(0));
	CHECK(msg.getByte() == 0x75);
	CHECK(msg.getByte() == SCREENSHOT_AND_BANNER_TYPE_BESTIARY_PROGRESS);
	CHECK(msg.get<uint16_t>() == 500);
	CHECK(msg.getByte() == 3);
	CHECK(msg.getString() == "Notification Test Rat");
	CHECK(msg.get<uint16_t>() == 21);
	CHECK(msg.getByte() == 11);
	CHECK(msg.getByte() == 22);
	CHECK(msg.getByte() == 33);
	CHECK(msg.getByte() == 44);
	CHECK(msg.getByte() == 3);
	checkPacketEnd(msg);
}

TEST_CASE(enhanced_bosstiary_notification_uses_authoritative_identity)
{
	const auto info = makeCreature(900, "Notification Test Boss", 77);
	NetworkMessage msg;
	CHECK(BestiaryNotificationProtocol::writeProgress(msg, info, 1, true, true));
	CHECK(msg.setBufferPosition(0));
	CHECK(msg.getByte() == 0x75);
	CHECK(msg.getByte() == SCREENSHOT_AND_BANNER_TYPE_BOSSTIARY_PROGRESS);
	CHECK(msg.get<uint16_t>() == 900);
	CHECK(msg.getByte() == 1);
	CHECK(msg.getString() == "Notification Test Boss");
	CHECK(msg.get<uint16_t>() == 77);
	CHECK(msg.getByte() == 11);
	CHECK(msg.getByte() == 22);
	CHECK(msg.getByte() == 33);
	CHECK(msg.getByte() == 44);
	CHECK(msg.getByte() == 3);
	checkPacketEnd(msg);
}

TEST_CASE(bestiary_notification_rejects_invalid_identity_or_progress)
{
	auto info = makeCreature(0, "Invalid", 21);
	NetworkMessage msg;
	CHECK(!BestiaryNotificationProtocol::writeProgress(msg, info, 1, false, true));
	CHECK(msg.getLength() == 0);

	info.raceId = 500;
	CHECK(!BestiaryNotificationProtocol::writeProgress(msg, info, 0, false, true));
	CHECK(msg.getLength() == 0);
}

TFS_TEST_MAIN()
