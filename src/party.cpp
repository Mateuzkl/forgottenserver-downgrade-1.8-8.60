// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "party.h"

#include "configmanager.h"
#include "events.h"
#include "game.h"
#include "scriptmanager.h"

extern Game g_game;

Party::Party(Player* leader) : leader(g_game.getPlayerWeakRef(leader)) {}

std::shared_ptr<Party> Party::create(Player* leader)
{
	auto party = std::make_shared<Party>(leader);
	party->self = party;
	leader->setParty(party.get());
	return party;
}

void Party::disband()
{
	if (!g_events->eventPartyOnDisband(this)) {
		return;
	}

	auto currentLeader = leader.lock();
	leader.reset();

	if (currentLeader) {
		currentLeader->setParty(nullptr);
		currentLeader->sendClosePrivate(CHANNEL_PARTY);
		g_game.updatePlayerShield(currentLeader.get());
		currentLeader->sendCreatureSkull(currentLeader.get());
		currentLeader->sendTextMessage(MESSAGE_INFO_DESCR, "Your party has been disbanded.");
	}

	for (auto& inviteeWeak : inviteList) {
		if (const auto inviteeRef = inviteeWeak.lock(); Player* invitee = inviteeRef.get()) {
			invitee->removePartyInvitation(this);
			if (currentLeader) {
				currentLeader->sendCreatureShield(invitee);
			}
		}
	}
	inviteList.clear();

	for (auto& memberWeak : memberList) {
		if (const auto memberRef = memberWeak.lock(); Player* member = memberRef.get()) {
			member->setParty(nullptr);
			member->sendClosePrivate(CHANNEL_PARTY);
			member->sendTextMessage(MESSAGE_INFO_DESCR, "Your party has been disbanded.");
		}
	}

	for (auto& memberWeak : memberList) {
		auto memberRef = memberWeak.lock();
		Player* member = memberRef.get();
		if (!member) continue;
		g_game.updatePlayerShield(member);

		for (auto& otherMemberWeak : memberList) {
			if (const auto otherMemberRef = otherMemberWeak.lock(); Player* otherMember = otherMemberRef.get()) {
				otherMember->sendCreatureSkull(member);
			}
		}

		if (currentLeader) {
			member->sendCreatureSkull(currentLeader.get());
			currentLeader->sendCreatureSkull(member);
		}
	}
	memberList.clear();
	self.reset();
}

bool Party::leaveParty(Player* player, bool forceRemove /* = false */)
{
	if (!player) {
		return false;
	}

	auto currentLeader = leader.lock();

	if (player->getParty() != this && currentLeader.get() != player) {
		return false;
	}

	bool canRemove = g_events->eventPartyOnLeave(this, player);
	if (!forceRemove && !canRemove) {
		return false;
	}

	bool missingLeader = false;
	if (currentLeader.get() == player || !currentLeader) {
		if (!memberList.empty()) {
			std::shared_ptr<Player> nextLeader;
			size_t validMembersCount = 0;
			
			for (auto& memberWeak : memberList) {
				if (const auto validMemberRef = memberWeak.lock(); Player* validMember = validMemberRef.get()) {
					if (validMember != player) {
						if (!nextLeader) nextLeader = validMemberRef;
						validMembersCount++;
					}
				}
			}

			if (validMembersCount == 0 || (validMembersCount == 1 && inviteList.empty() && currentLeader.get() == player)) {
				missingLeader = true;
			} else if (nextLeader) {
				passPartyLeadership(nextLeader.get(), true);
				currentLeader = leader.lock();
			} else {
				missingLeader = true;
			}
		} else {
			missingLeader = true;
		}
	}

	auto it = std::remove_if(memberList.begin(), memberList.end(),
		[player](const std::weak_ptr<Player>& weakPlayer) {
			return weakPlayer.expired() || weakPlayer.lock().get() == player;
		});
	if (it != memberList.end()) {
		memberList.erase(it, memberList.end());
	}

	player->setParty(nullptr);
	player->sendClosePrivate(CHANNEL_PARTY);
	g_game.updatePlayerShield(player);

	for (auto& memberWeak : memberList) {
		if (const auto memberRef = memberWeak.lock(); Player* member = memberRef.get()) {
			member->sendCreatureSkull(player);
			player->sendPlayerPartyIcons(member);
		}
	}

	if (currentLeader) {
		currentLeader->sendCreatureSkull(player);
		player->sendPlayerPartyIcons(currentLeader.get());
	}
	
	player->sendCreatureSkull(player);

	for (auto& inviteeWeak : inviteList) {
		if (const auto inviteeRef = inviteeWeak.lock(); Player* invitee = inviteeRef.get()) {
			player->sendCreatureShield(invitee);
		}
	}

	player->sendTextMessage(MESSAGE_INFO_DESCR, "You have left the party.");

	updateSharedExperience();

	clearPlayerPoints(player);

	broadcastPartyMessage(MESSAGE_INFO_DESCR, fmt::format("{:s} has left the party.", player->getName()));

	if (missingLeader || empty()) {
		disband();
	}

	return true;
}

bool Party::passPartyLeadership(Player* player, bool forceRemove /* = false*/)
{
	auto currentLeader = leader.lock();

	if (!player || currentLeader.get() == player || player->getParty() != this) {
		return false;
	}

	if (!g_events->eventPartyOnPassLeadership(this, player) && !forceRemove) {
		return false;
	}

	auto it = std::remove_if(memberList.begin(), memberList.end(),
		[player](const std::weak_ptr<Player>& weakPlayer) {
			return weakPlayer.expired() || weakPlayer.lock().get() == player;
		});
	if (it != memberList.end()) {
		memberList.erase(it, memberList.end());
	}

	broadcastPartyMessage(MESSAGE_INFO_DESCR, fmt::format("{:s} is now the leader of the party.", player->getName()),
	                      true);

	auto oldLeader = currentLeader;
	leader = g_game.getPlayerWeakRef(player);

	if (oldLeader) {
		memberList.insert(memberList.begin(), g_game.getPlayerWeakRef(oldLeader.get()));
	}

	updateSharedExperience();

	for (auto& memberWeak : memberList) {
		if (const auto memberRef = memberWeak.lock(); Player* member = memberRef.get()) {
			if (oldLeader) member->sendCreatureShield(oldLeader.get());
			member->sendCreatureShield(player);
		}
	}

	for (auto& inviteeWeak : inviteList) {
		if (const auto inviteeRef = inviteeWeak.lock(); Player* invitee = inviteeRef.get()) {
			if (oldLeader) invitee->sendCreatureShield(oldLeader.get());
			invitee->sendCreatureShield(player);
		}
	}

	if (oldLeader) {
		player->sendCreatureShield(oldLeader.get());
	}
	player->sendCreatureShield(player);

	player->sendTextMessage(MESSAGE_INFO_DESCR, "You are now the leader of the party.");
	return true;
}

bool Party::joinParty(Player& player)
{
	auto currentLeader = leader.lock();
	if (!currentLeader) return false;

	if (!g_events->eventPartyOnJoin(this, &player)) {
		return false;
	}

	if (memberList.empty()) {
		currentLeader->clearPartyInvitations();
	}

	memberList.push_back(g_game.getPlayerWeakRef(&player));
	player.setParty(this);
	broadcastPartyMessage(MESSAGE_INFO_DESCR, fmt::format("{:s} has joined the party.", player.getName()));

	player.clearPartyInvitations();

	g_game.updatePlayerShield(&player);

	for (auto& memberWeak : memberList) {
		if (const auto memberRef = memberWeak.lock(); Player* member = memberRef.get()) {
			member->sendCreatureSkull(&player);
			player.sendPlayerPartyIcons(member);
		}
	}

	currentLeader->sendCreatureSkull(&player);
	player.sendPlayerPartyIcons(currentLeader.get());

	player.sendCreatureSkull(&player);

	for (auto& inviteeWeak : inviteList) {
		if (const auto inviteeRef = inviteeWeak.lock(); Player* invitee = inviteeRef.get()) {
			player.sendCreatureShield(invitee);
		}
	}

	player.removePartyInvitation(this);
	updateSharedExperience();

	const std::string& leaderName = currentLeader->getName();
	player.sendTextMessage(
	    MESSAGE_INFO_DESCR,
	    fmt::format("You have joined {:s}'{:s} party. Open the party channel to communicate with your companions.",
	                leaderName, leaderName.back() == 's' ? "" : "s"));
	return true;
}

bool Party::removeInvite(Player& player, bool removeFromPlayer /* = true*/)
{
	bool found = false;
	auto remove_it = std::remove_if(inviteList.begin(), inviteList.end(),
		[&player, &found](const std::weak_ptr<Player>& weakPlayer) {
			if (weakPlayer.expired()) return true;
			if (weakPlayer.lock().get() == &player) {
				found = true;
				return true;
			}
			return false;
		});
		
	inviteList.erase(remove_it, inviteList.end());

	if (!found) return false;

	auto currentLeader = leader.lock();
	if (currentLeader) {
		currentLeader->sendCreatureShield(&player);
		player.sendCreatureShield(currentLeader.get());
	}

	if (removeFromPlayer) {
		player.removePartyInvitation(this);
	}

	if (empty()) {
		disband();
	}

	return true;
}

void Party::revokeInvitation(Player& player)
{
	if (!g_events->eventPartyOnRevokeInvitation(this, &player)) {
		return;
	}

	auto currentLeader = leader.lock();
	if (currentLeader) {
		player.sendTextMessage(MESSAGE_INFO_DESCR, fmt::format("{:s} has revoked {:s} invitation.", currentLeader->getName(),
		                                                       currentLeader->getSex() == PLAYERSEX_FEMALE ? "her" : "his"));
		currentLeader->sendTextMessage(MESSAGE_INFO_DESCR, fmt::format("Invitation for {:s} has been revoked.", player.getName()));
	}
	removeInvite(player);
}

bool Party::invitePlayer(Player& player)
{
	if (isPlayerInvited(&player)) {
		return false;
	}
	
	auto currentLeader = leader.lock();
	if (!currentLeader) return false;

	if (empty()) {
		currentLeader->sendTextMessage(
		    MESSAGE_INFO_DESCR,
		    fmt::format("{:s} has been invited. Open the party channel to communicate with your members.",
		                player.getName()));
		g_game.updatePlayerShield(currentLeader.get());
		currentLeader->sendCreatureSkull(currentLeader.get());
	} else {
		currentLeader->sendTextMessage(MESSAGE_INFO_DESCR, fmt::format("{:s} has been invited.", player.getName()));
	}

	inviteList.push_back(g_game.getPlayerWeakRef(&player));
	player.addPartyInvitation(this);

	currentLeader->sendCreatureShield(&player);
	player.sendCreatureShield(currentLeader.get());

	for (auto& memberWeak : memberList) {
		if (const auto memberRef = memberWeak.lock(); Player* member = memberRef.get()) {
			member->sendCreatureShield(&player);
		}
	}

	player.sendTextMessage(MESSAGE_INFO_DESCR, fmt::format("{:s} has invited you to {:s} party.", currentLeader->getName(),
	                                                       currentLeader->getSex() == PLAYERSEX_FEMALE ? "her" : "his"));
	return true;
}

bool Party::isPlayerInvited(const Player* player) const
{
	for (const auto& inviteeWeak : inviteList) {
		if (inviteeWeak.lock().get() == player) {
			return true;
		}
	}
	return false;
}

void Party::updateAllPartyIcons()
{
	auto currentLeader = leader.lock();
	
	for (auto& memberWeak : memberList) {
		auto memberRef = memberWeak.lock();
		Player* member = memberRef.get();
		if (!member) continue;
		
		for (auto& otherMemberWeak : memberList) {
			if (const auto otherMemberRef = otherMemberWeak.lock(); Player* otherMember = otherMemberRef.get()) {
				member->sendCreatureShield(otherMember);
			}
		}

		if (currentLeader) {
			member->sendCreatureShield(currentLeader.get());
			currentLeader->sendCreatureShield(member);
		}
	}
	
	if (currentLeader) {
		currentLeader->sendCreatureShield(currentLeader.get());
	}
}

void Party::broadcastPartyMessage(MessageClasses msgClass, std::string_view msg, bool sendToInvitations /*= false*/)
{
	for (auto& memberWeak : memberList) {
		if (const auto memberRef = memberWeak.lock(); Player* member = memberRef.get()) {
			member->sendTextMessage(msgClass, msg);
		}
	}

	if (auto currentLeader = leader.lock()) {
		currentLeader->sendTextMessage(msgClass, msg);
	}

	if (sendToInvitations) {
		for (auto& inviteeWeak : inviteList) {
			if (const auto inviteeRef = inviteeWeak.lock(); Player* invitee = inviteeRef.get()) {
				invitee->sendTextMessage(msgClass, msg);
			}
		}
	}
}

void Party::updateSharedExperience()
{
	if (sharedExpActive) {
		bool result = getSharedExperienceStatus() == SHAREDEXP_OK;
		if (result != sharedExpEnabled) {
			sharedExpEnabled = result;
			updateAllPartyIcons();
		}
	}
}

namespace {

const char* getSharedExpReturnMessage(SharedExpStatus_t value)
{
	switch (value) {
		case SHAREDEXP_OK:
			return "Shared Experience is now active.";
		case SHAREDEXP_TOOFARAWAY:
			return "Shared Experience has been activated, but some members of your party are too far away.";
		case SHAREDEXP_LEVELDIFFTOOLARGE:
			return "Shared Experience has been activated, but the level spread of your party is too wide.";
		case SHAREDEXP_MEMBERINACTIVE:
			return "Shared Experience has been activated, but some members of your party are inactive.";
		case SHAREDEXP_EMPTYPARTY:
			return "Shared Experience has been activated, but you are alone in your party.";
		default:
			return "An error occured. Unable to activate shared experience.";
	}
}

}

bool Party::setSharedExperience(Player* player, bool sharedExpActive)
{
	auto currentLeader = leader.lock();
	if (!player || currentLeader.get() != player) {
		return false;
	}

	if (this->sharedExpActive == sharedExpActive) {
		return true;
	}

	this->sharedExpActive = sharedExpActive;

	if (sharedExpActive) {
		SharedExpStatus_t sharedExpStatus = getSharedExperienceStatus();
		this->sharedExpEnabled = sharedExpStatus == SHAREDEXP_OK;
		currentLeader->sendTextMessage(MESSAGE_INFO_DESCR, getSharedExpReturnMessage(sharedExpStatus));
	} else {
		currentLeader->sendTextMessage(MESSAGE_INFO_DESCR, "Shared Experience has been deactivated.");
	}

	updateAllPartyIcons();
	return true;
}

void Party::shareExperience(uint64_t experience, const std::shared_ptr<Creature>& source /* = nullptr*/)
{
	uint64_t shareExperience = experience;
	g_events->eventPartyOnShareExperience(this, shareExperience);

	for (auto& memberWeak : memberList) {
		if (auto member = memberWeak.lock()) {
			member->onGainSharedExperience(shareExperience, source);
		}
	}
	
	if (auto currentLeader = leader.lock()) {
		currentLeader->onGainSharedExperience(shareExperience, source);
	}
}

bool Party::canUseSharedExperience(const Player* player) const
{
	return getMemberSharedExperienceStatus(player) == SHAREDEXP_OK;
}

SharedExpStatus_t Party::getMemberSharedExperienceStatus(const Player* player) const
{
	if (memberList.empty()) {
		return SHAREDEXP_EMPTYPARTY;
	}

	auto currentLeader = leader.lock();
	if (!currentLeader) return SHAREDEXP_MEMBERINACTIVE;

	uint32_t highestLevel = currentLeader->getLevel();
	for (const auto& memberWeak : memberList) {
		if (const auto memberRef = memberWeak.lock(); Player* member = memberRef.get()) {
			if (member->getLevel() > highestLevel) {
				highestLevel = member->getLevel();
			}
		}
	}

	uint32_t minLevel = static_cast<uint32_t>(std::ceil((static_cast<float>(highestLevel) * 2) / 3));
	if (player->getLevel() < minLevel) {
		return SHAREDEXP_LEVELDIFFTOOLARGE;
	}

	const int32_t range  = getInteger(ConfigManager::EXP_SHARE_RANGE);
	const int32_t floors = getInteger(ConfigManager::EXP_SHARE_FLOORS);

	if (!currentLeader->getPosition().isInRange(player->getPosition(), range, range, floors)) {
		return SHAREDEXP_TOOFARAWAY;
	}

	if (!player->hasFlag(PlayerFlag_NotGainInFight)) {
		// check if the player has healed/attacked anything recently
		auto it = ticksMap.find(player->getID());
		if (it == ticksMap.end()) {
			return SHAREDEXP_MEMBERINACTIVE;
		}

		uint64_t timeDiff = OTSYS_TIME() - it->second;
		if (timeDiff > static_cast<uint64_t>(getInteger(ConfigManager::PZ_LOCKED))) {
			return SHAREDEXP_MEMBERINACTIVE;
		}
	}
	return SHAREDEXP_OK;
}

SharedExpStatus_t Party::getSharedExperienceStatus()
{
	auto currentLeader = leader.lock();
	if (!currentLeader) return SHAREDEXP_MEMBERINACTIVE;

	SharedExpStatus_t leaderStatus = getMemberSharedExperienceStatus(currentLeader.get());
	if (leaderStatus != SHAREDEXP_OK) {
		return leaderStatus;
	}

	for (auto& memberWeak : memberList) {
		if (const auto memberRef = memberWeak.lock(); Player* member = memberRef.get()) {
			SharedExpStatus_t memberStatus = getMemberSharedExperienceStatus(member);
			if (memberStatus != SHAREDEXP_OK) {
				return memberStatus;
			}
		}
	}
	return SHAREDEXP_OK;
}

void Party::updatePlayerTicks(Player* player, uint32_t points)
{
	if (points != 0 && !player->hasFlag(PlayerFlag_NotGainInFight)) {
		ticksMap[player->getID()] = OTSYS_TIME();
		updateSharedExperience();
	}
}

void Party::clearPlayerPoints(Player* player)
{
	auto it = ticksMap.find(player->getID());
	if (it != ticksMap.end()) {
		ticksMap.erase(it);
		updateSharedExperience();
	}
}

bool Party::canOpenCorpse(uint32_t ownerId) const
{
	if (auto player = g_game.getPlayerByID(ownerId)) {
		auto currentLeader = leader.lock();
		if (currentLeader && currentLeader->getID() == ownerId) return true;
		return player->getParty() == this;
	}
	return false;
}
