// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "talkaction.h"

#include "condition.h"
#include "player.h"
#include "pugicast.h"
#include "logger.h"

TalkActions::TalkActions() : scriptInterface("TalkAction Interface") { scriptInterface.initState(); }

TalkActions::~TalkActions() { clear(false); }

void TalkActions::clear(bool fromLua)
{
	std::erase_if(talkActions, [fromLua](const auto& entry) { return fromLua == entry.second.fromLua; });

	reInitState(fromLua);
}

LuaScriptInterface& TalkActions::getScriptInterface() { return scriptInterface; }

bool TalkActions::registerLuaEvent(TalkAction_ptr talkAction)
{
	const auto& words = talkAction->stealWordsMap();

	if (words.empty()) {
		LOG_WARN("[Warning - TalkActions::registerLuaEvent] Missing words for talk action.");
		return false;
	}

	for (const auto& word : words) {
		talkActions.emplace(word, *talkAction);
	}
	return true;
}

TalkActionResult TalkActions::playerSaySpell(Player* player, SpeakClasses type, std::string_view words) const
{
	size_t wordsLength = words.length();
	for (auto it = talkActions.begin(); it != talkActions.end();) {
		std::string_view talkactionWords = it->first;
		if (!caseInsensitiveStartsWith(words, talkactionWords)) {
			++it;
			continue;
		}

		std::string param;
		if (wordsLength != talkactionWords.size()) {
			param = words.substr(talkactionWords.size());
			if (param.front() != ' ') {
				++it;
				continue;
			}
			trimLeftString(param);

			auto separator = it->second.getSeparator();
			if (separator != " ") {
				if (!param.empty()) {
					if (param != separator) {
						++it;
						continue;
					} else {
						param.erase(param.begin());
					}
				}
			}
		}

		if (it->second.getNeedAccess() && !player->isAccessPlayer()) {
			return TalkActionResult::CONTINUE;
		}

		if (player->getAccountType() < it->second.getRequiredAccountType()) {
			return TalkActionResult::CONTINUE;
		}

		int32_t exhaustTime = it->second.getExhaustion();
		if (exhaustTime == -1) {
			exhaustTime = TALK_ACTION_EXHAUST_MS;
		}

		if (exhaustTime > 0) {
			if (player->hasCondition(CONDITION_EXHAUST_WEAPON, EXHAUST_TALKACTION)) {
				Condition* condition = player->getCondition(CONDITION_EXHAUST_WEAPON, CONDITIONID_DEFAULT, EXHAUST_TALKACTION);
				if (!it->second.getExhaustionMessage().empty()) {
					std::string msg = it->second.getExhaustionMessage();
					size_t pos = msg.find("{time}");
					if (pos != std::string::npos) {
						double sec = condition ? (condition->getTicks() / 1000.0) : 0.0;
						msg.replace(pos, 6, fmt::format("{:.1f}", sec));
					}
					player->sendTextMessage(it->second.getExhaustionMessageType(), msg);
				} else {
					if (condition) {
						player->sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("Please wait {:.1f}s before using this command again.", condition->getTicks() / 1000.0));
					} else {
						player->sendTextMessage(MESSAGE_STATUS_SMALL, "Please wait a few seconds before using this command again.");
					}
				}
				return TalkActionResult::BREAK;
			}

			if (!player->hasFlag(PlayerFlag_HasNoExhaustion)) {
				if (auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_EXHAUST_WEAPON, exhaustTime, 0, false, EXHAUST_TALKACTION)) {
					player->addCondition(std::move(condition));
				}
			}
		}

		if (it->second.executeSay(player, words, param, type)) {
			return TalkActionResult::CONTINUE;
		}
			return TalkActionResult::BREAK;
	}
	return TalkActionResult::CONTINUE;
}

bool TalkAction::executeSay(Player* player, std::string_view words, std::string_view param, SpeakClasses type) const
{
	// onSay(player, words, param, type)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - TalkAction::executeSay] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");

	Lua::pushString(L, words);
	Lua::pushString(L, param);
	lua_pushinteger(L, type);

	return scriptInterface->callFunction(4);
}
