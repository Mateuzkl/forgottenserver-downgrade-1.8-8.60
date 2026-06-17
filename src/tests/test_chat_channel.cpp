#include "../otpch.h"

#include "../chat.h"

#include "test_support.h"

TEST_CASE(chat_channel_construction)
{
	ChatChannel channel(1, "Help");
	CHECK(channel.getId() == 1);
	CHECK(channel.getName() == "Help");
	CHECK(!channel.isPublicChannel());
}

TEST_CASE(chat_channel_public_flag)
{
	ChatChannel channel(3, "Trade");
	CHECK(!channel.isPublicChannel());
	channel.setPublicChannel(true);
	CHECK(channel.isPublicChannel());
	channel.setPublicChannel(false);
	CHECK(!channel.isPublicChannel());
}

TEST_CASE(chat_channel_event_setters)
{
	ChatChannel channel(4, "Test");
	channel.setCanJoinEvent(100);
	channel.setOnJoinEvent(200);
	channel.setOnLeaveEvent(300);
	channel.setOnSpeakEvent(400);
	CHECK(channel.getCanJoinEvent() == 100);
	CHECK(channel.getOnJoinEvent() == 200);
	CHECK(channel.getOnLeaveEvent() == 300);
	CHECK(channel.getOnSpeakEvent() == 400);
}

TFS_TEST_MAIN()
