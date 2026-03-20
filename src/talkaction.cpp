// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "talkaction.h"

#include "logger.h"
#include "player.h"
#include "pugicast.h"

TalkActions::TalkActions() : scriptInterface("TalkAction Interface") { scriptInterface.initState(); }

TalkActions::~TalkActions() { clear(false); }

void TalkActions::clear(bool fromLua)
{
	for (auto it = talkActions.begin(); it != talkActions.end();) {
		if (fromLua == it->second.fromLua) {
			it = talkActions.erase(it);
		} else {
			++it;
		}
	}

	reInitState(fromLua);
}

LuaScriptInterface& TalkActions::getScriptInterface() { return scriptInterface; }

Event_ptr TalkActions::getEvent(std::string_view nodeName)
{
	if (!caseInsensitiveEqual(nodeName, "talkaction")) {
		return nullptr;
	}
	return Event_ptr(new TalkAction(&scriptInterface));
}

bool TalkActions::registerEvent(Event_ptr event, const pugi::xml_node&)
{
	TalkAction_ptr talkAction{static_cast<TalkAction*>(event.release())}; // event is guaranteed to be a TalkAction
	const auto& words = talkAction->stealWordsMap();

	for (const auto& word : words) {
		talkActions.emplace(word, *talkAction);
	}
	return true;
}

bool TalkActions::registerLuaEvent(TalkAction* event)
{
	TalkAction_ptr talkAction{event};
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
			boost::algorithm::trim_left(param);

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

		if (it->second.executeSay(player, words, param, type)) {
			return TalkActionResult::CONTINUE;
		}
		return TalkActionResult::BREAK;
	}
	return TalkActionResult::CONTINUE;
}

bool TalkAction::configureEvent(const pugi::xml_node& node)
{
	pugi::xml_attribute wordsAttribute = node.attribute("words");
	if (!wordsAttribute) {
		LOG_ERROR("[Error - TalkAction::configureEvent] Missing words for talk action or spell");
		return false;
	}

	pugi::xml_attribute separatorAttribute = node.attribute("separator");
	if (separatorAttribute) {
		separator = separatorAttribute.as_string();
	}

	pugi::xml_attribute accessAttribute = node.attribute("access");
	if (accessAttribute) {
		needAccess = accessAttribute.as_bool();
	}

	pugi::xml_attribute accountTypeAttribute = node.attribute("accountType");
	if (accountTypeAttribute) {
		requiredAccountType = static_cast<AccountType_t>(pugi::cast<int32_t>(accountTypeAttribute.value()));
	}

	for (std::string_view word : explodeString(wordsAttribute.as_string(), ";")) {
		setWords(word);
	}
	return true;
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
