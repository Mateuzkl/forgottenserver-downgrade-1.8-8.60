#include "otpch.h"
#include "protocolspectator.h"
#include "iologindata.h"
#include "chat.h"
#include "game.h"

extern Chat* g_chat;
extern Game g_game;

void ProtocolSpectator::setBroadcast(bool value)
{
    if (broadcast == value)
        return;

    if (!value)
        return clear();

    sendCastChannel();
    broadcast = true;
}

void ProtocolSpectator::addSpectator(ProtocolGame_ptr spectator)
{
    spectators.insert(spectator);
    setUpdateStatus(true); // update spectators count
}

void ProtocolSpectator::removeSpectator(ProtocolGame_ptr spectator)
{
    spectators.erase(spectator);
    setUpdateStatus(true); // update spectators count
}

void ProtocolSpectator::kick(const DataList& _kicks)
{
    for (auto& it : _kicks)
        for (auto& spectator : spectators)
            if (it.second == spectator->getIP())
                spectator->disconnect();
}

void ProtocolSpectator::ban(const DataList& _bans)
{
    bans = _bans;
    for (auto& it : bans)
        for (auto& spectator : spectators)
            if (it.second == spectator->getIP())
                spectator->disconnect();
}

bool ProtocolSpectator::isBanned(uint32_t ip) const
{
    for (auto& it : bans)
        if (it.second == ip)
            return true;
    return false;
}

void ProtocolSpectator::spectatorSay(ProtocolGame_ptr spectator, std::string_view text)
{
    if (text[0] == '/') {
        auto sv = explodeString(text.substr(1, text.length()), " ", 1);
        // Convert string_view elements to std::string for manipulation
        std::string cmd = sv.empty() ? "" : std::string(sv[0]);
        toLowerCaseString(cmd);
        if (cmd == "list" || cmd == "show" || cmd == "spectators")
        {
            std::stringstream s;
            s << spectators.size() << " spectators.";
            for (const auto& it : spectators)
                s << " " << it->getSpectatorName() << ",";
            s.seekp(-1, s.cur);
            s << ".";
            sendCastMessage("", s.str().substr(0, 255), TALKTYPE_CHANNEL_O, spectator);
        }
        else if (cmd == "name" || cmd == "nick")
        {
            if (sv.size() == 1)
                return sendCastMessage("", "Usage: /nick new name.", TALKTYPE_CHANNEL_O, spectator);

            std::string nickname(sv[1]);
            // trim leading/trailing spaces
            size_t start = nickname.find_first_not_of(' ');
            size_t end = nickname.find_last_not_of(' ');
            if (start != std::string::npos) {
                nickname = nickname.substr(start, end - start + 1);
            }
            if (nickname.size() < 3 || nickname.size() > 30)
                return sendCastMessage("", "Wrong name.", TALKTYPE_CHANNEL_O, spectator);

            if (g_game.getPlayerByName(nickname) ||
                ProtocolGame::spectatorNames.find(asLowerCaseString(nickname)) != ProtocolGame::spectatorNames.end())
                return sendCastMessage("", "This name is already being used.", TALKTYPE_CHANNEL_O, spectator);

            ProtocolGame::spectatorNames.erase(asLowerCaseString(spectator->getSpectatorName()));
            sendCastMessage("", spectator->getSpectatorName() + " has changed name to " + nickname + ".", TALKTYPE_CHANNEL_O);
            spectator->setSpectatorName(nickname);
            ProtocolGame::spectatorNames.insert(asLowerCaseString(spectator->getSpectatorName()));

        }
        else if (cmd == "help") {
            sendCastMessage("", "Commands list:", TALKTYPE_CHANNEL_O, spectator);
            sendCastMessage("", "/help - print this message", TALKTYPE_CHANNEL_O, spectator);
            sendCastMessage("", "/list - print spectators list", TALKTYPE_CHANNEL_O, spectator);
            sendCastMessage("", "/name - change your name", TALKTYPE_CHANNEL_O, spectator);
        }
        else
            sendCastMessage("", "Command not found. Use /help for commands list.", TALKTYPE_CHANNEL_O, spectator);

        return;
    }

    bool muted = false;
    for (auto& it : mutes)
        if (it.second == spectator->getIP())
            muted = true;

    if (muted) {
        sendCastMessage("", "You are muted.", TALKTYPE_CHANNEL_O, spectator);
        return;
    }

    sendCastMessage(spectator->getSpectatorName(), text, TALKTYPE_CHANNEL_O);
}

void ProtocolSpectator::sendCastMessage(const std::string& author, std::string_view text, SpeakClasses type, ProtocolGame_ptr target /* = nullptr */)
{
    if (!target)
        return sendChannelMessage(author, text, type, CHANNEL_CAST);

    target->sendChannelMessage(author, text, type, CHANNEL_CAST);
}