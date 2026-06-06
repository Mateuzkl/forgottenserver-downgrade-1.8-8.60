// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "mailbox.h"

#include "game.h"
#include "iologindata.h"
#include "save_manager.h"

extern Game g_game;

namespace {

bool parseMailAddress(std::string_view text, std::string& receiver, std::string& townName)
{
	receiver.clear();
	townName.clear();

	size_t lineStart = 0;
	bool firstLine = true;
	while (lineStart <= text.size()) {
		const size_t lineEnd = text.find('\n', lineStart);
		const size_t count = lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart;

		std::string line{text.substr(lineStart, count)};
		trimString(line);

		if (firstLine) {
			// Keep legacy mailbox semantics: the first address line must contain the receiver.
			// A blank first line is invalid instead of treating the town line as a player name.
			if (line.empty()) {
				return false;
			}
			receiver = std::move(line);
			firstLine = false;
		} else if (!line.empty()) {
			townName = std::move(line);
			break;
		}

		if (lineEnd == std::string_view::npos) {
			break;
		}
		lineStart = lineEnd + 1;
	}

	return !receiver.empty();
}

Inbox* getDestinationInbox(Player& player, const Town* town)
{
	return town ? player.getInbox(town->getID()) : player.getInbox();
}

} // namespace

ReturnValue Mailbox::queryAdd(int32_t, const Thing& thing, uint32_t, uint32_t, Creature*) const
{
	const Item* item = thing.getItem();
	if (item && Mailbox::canSend(item)) {
		return RETURNVALUE_NOERROR;
	}
	return RETURNVALUE_NOTPOSSIBLE;
}

ReturnValue Mailbox::queryMaxCount(int32_t, const Thing&, uint32_t count, uint32_t& maxQueryCount, uint32_t) const
{
	maxQueryCount = std::max<uint32_t>(1, count);
	return RETURNVALUE_NOERROR;
}

ReturnValue Mailbox::queryRemove(const Thing&, uint32_t, uint32_t, Creature* /*= nullptr */) const
{
	return RETURNVALUE_NOTPOSSIBLE;
}

Cylinder* Mailbox::queryDestination(int32_t&, const Thing&, Item**, uint32_t&) { return this; }

void Mailbox::addThing(Thing* thing) { return addThing(0, thing); }

void Mailbox::addThing(int32_t, Thing* thing)
{
	Item* item = thing->getItem();
	if (item && Mailbox::canSend(item)) {
		sendItem(item);
	}
}

void Mailbox::updateThing(Thing*, uint16_t, uint32_t)
{
	//
}

void Mailbox::replaceThing(uint32_t, Thing*)
{
	//
}

void Mailbox::removeThing(Thing*, uint32_t)
{
	//
}

void Mailbox::postAddNotification(Thing* thing, const Cylinder* oldParent, int32_t index, cylinderlink_t)
{
	getParent()->postAddNotification(thing, oldParent, index, LINK_PARENT);
}

void Mailbox::postRemoveNotification(Thing* thing, const Cylinder* newParent, int32_t index, cylinderlink_t)
{
	getParent()->postRemoveNotification(thing, newParent, index, LINK_PARENT);
}

bool Mailbox::sendItem(Item* item) const
{
	std::string receiver;
	std::string townName;
	if (!getReceiver(item, receiver, townName)) {
		return false;
	}

	/**No need to continue if its still empty**/
	if (receiver.empty()) {
		return false;
	}

	Town* town = nullptr;
	if (!townName.empty()) {
		town = g_game.map.towns.getTown(townName);
		if (!town) {
			return false;
		}
	}

	auto player = g_game.getPlayerByName(receiver);
	if (player) {
		Inbox* inbox = getDestinationInbox(*player, town);
		if (!inbox) {
			return false;
		}

		if (g_game.internalMoveItem(item->getParent(), inbox, INDEX_WHEREEVER, item, item->getItemCount(),
		                            nullptr, FLAG_NOLIMIT) == RETURNVALUE_NOERROR) {
			g_game.transformItem(item, item->getID() + 1);
			player->onReceiveMail();
			return true;
		}
	} else {
		// Player is final and offline loads elsewhere in the codebase use this short-lived stack instance.
		Player tmpPlayer(nullptr);
		if (!IOLoginData::loadPlayerByName(&tmpPlayer, receiver)) {
			return false;
		}

		Inbox* inbox = getDestinationInbox(tmpPlayer, town);
		if (!inbox) {
			return false;
		}

	auto* originalParent = item->getParent();
	auto originalIndex = originalParent ? originalParent->getThingIndex(item) : -1;
	if (g_game.internalMoveItem(originalParent, inbox, INDEX_WHEREEVER, item, item->getItemCount(), nullptr,
	                            FLAG_NOLIMIT) == RETURNVALUE_NOERROR) {
		if (g_saveManager.savePlayerSync(&tmpPlayer)) {
			g_game.transformItem(item, item->getID() + 1);
			return true;
		}
		g_game.internalMoveItem(inbox, originalParent, originalIndex, item, item->getItemCount(), nullptr,
		                        FLAG_NOLIMIT);
		return false;
	}
	}
	return false;
}

bool Mailbox::getReceiver(Item* item, std::string& receiver, std::string& townName) const
{
	const Container* container = item->getContainer();
	if (container) {
		for (const auto& containerItem : container->getItemList()) {
			if (containerItem->getID() == ITEM_LABEL && getReceiver(containerItem.get(), receiver, townName)) {
				return true;
			}
		}
		return false;
	}

	auto text = item->getText();
	if (text.empty()) {
		return false;
	}

	return parseMailAddress(text, receiver, townName);
}

bool Mailbox::canSend(const Item* item) { return item->getID() == ITEM_PARCEL || item->getID() == ITEM_LETTER; }
