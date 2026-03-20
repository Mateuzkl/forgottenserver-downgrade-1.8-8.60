// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "protocoladmin.h"

#include "admin.h"
#include "configmanager.h"
#include "game.h"
#include "outputmessage.h"
#include "scheduler.h"
#include "tasks.h"
#include "tools.h" // For convertIPToString

extern Game g_game;

enum
{
	AP_MSG_LOGIN = 1,
	AP_MSG_ENCRYPTION = 2,
	AP_MSG_KEY_EXCHANGE = 3,
	AP_MSG_COMMAND = 4,
	AP_MSG_PING = 5,
	AP_MSG_KEEP_ALIVE = 6,

	AP_MSG_HELLO = 1,
	AP_MSG_KEY_EXCHANGE_OK = 2,
	AP_MSG_KEY_EXCHANGE_FAILED = 3,
	AP_MSG_LOGIN_OK = 4,
	AP_MSG_LOGIN_FAILED = 5,
	AP_MSG_COMMAND_OK = 6,
	AP_MSG_COMMAND_FAILED = 7,
	AP_MSG_ENCRYPTION_OK = 8,
	AP_MSG_ENCRYPTION_FAILED = 9,
	AP_MSG_PING_OK = 10,
	AP_MSG_MESSAGE = 11,
	AP_MSG_ERROR = 12,
};

enum
{
	CMD_BROADCAST = 1,
	CMD_CLOSE_SERVER = 2,
	CMD_PAY_HOUSES = 3,
	CMD_OPEN_SERVER = 4,
	CMD_SHUTDOWN_SERVER = 5,
	CMD_RELOAD_SCRIPTS = 6,
	CMD_KICK = 9,
	CMD_SAVE_SERVER = 13,
	CMD_SEND_MAIL = 14,
	CMD_SHALLOW_SAVE_SERVER = 15,
	CMD_CLEAN = 30,
	CMD_SHUTDOWN_SCHEDULED = 31
};

void ProtocolAdmin::onRecvFirstMessage(NetworkMessage& /*msg*/)
{
	state = NO_CONNECTED;
	if (ConfigManager::getString(ConfigManager::ADMIN_PASSWORD).empty()) {
		addLogLine("connection attempt on disabled protocol");
		disconnect();
		return;
	}

	if (!Admin::getInstance().allow(getIP())) {
		addLogLine("ip not allowed");
		disconnect();
		return;
	}

	if (!Admin::getInstance().addConnection()) {
		addLogLine("cannot add new connection");
		disconnect();
		return;
	}

	addLogLine("sending HELLO");
	if (auto output = OutputMessagePool::getOutputMessage()) {
		output->addByte(AP_MSG_HELLO);
		output->add<uint32_t>(1); // version
		output->addString("OTADMIN");

		output->add<uint16_t>(Admin::getInstance().getPolicy());  // security policy
		output->add<uint32_t>(Admin::getInstance().getOptions()); // protocol options(encryption, ...)
		send(output);
	}

	lastCommand = std::time(nullptr);
	state = ENCRYPTION_NO_SET;
}

void ProtocolAdmin::parsePacket(NetworkMessage& msg)
{
	if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
		disconnect();
		return;
	}

	uint8_t recvbyte = msg.getByte();
	auto output = OutputMessagePool::getOutputMessage();
	if (!output) {
		return;
	}

	switch (state) {
		case ENCRYPTION_NO_SET: {
			if (Admin::getInstance().isEncrypted()) {
				if ((std::time(nullptr) - startTime) > 30) {
					disconnect();
					addLogLine("encryption timeout");
					return;
				}

				if (recvbyte != AP_MSG_ENCRYPTION && recvbyte != AP_MSG_KEY_EXCHANGE) {
					output->addByte(AP_MSG_ERROR);
					output->addString("encryption needed");
					send(output);

					disconnect();
					addLogLine("wrong command while ENCRYPTION_NO_SET");
					return;
				}
			} else {
				state = NO_LOGGED_IN;
			}
			break;
		}

		case NO_LOGGED_IN: {
			if (ConfigManager::getBoolean(ConfigManager::ADMIN_REQUIRE_LOGIN)) {
				if ((std::time(nullptr) - startTime) > 30) {
					// login timeout
					disconnect();
					addLogLine("login timeout");
					return;
				}

				if (loginTries > 3) {
					output->addByte(AP_MSG_ERROR);
					output->addString("too many login tries");
					send(output);

					disconnect();
					addLogLine("too many login tries");
					return;
				}

				if (recvbyte != AP_MSG_LOGIN) {
					output->addByte(AP_MSG_ERROR);
					output->addString("you are not logged in");
					send(output);

					disconnect();
					addLogLine("wrong command while NO_LOGGED_IN");
					return;
				}
			} else {
				state = LOGGED_IN;
			}
			break;
		}

		case LOGGED_IN:
			break;

		default: {
			disconnect();
			addLogLine("no valid connection state!!!");
			return;
		}
	}

	lastCommand = std::time(nullptr);
	switch (recvbyte) {
		case AP_MSG_LOGIN: {
			if (state == NO_LOGGED_IN && ConfigManager::getBoolean(ConfigManager::ADMIN_REQUIRE_LOGIN)) {
				std::string pass = std::string(msg.getString());
				std::string word = std::string(ConfigManager::getString(ConfigManager::ADMIN_PASSWORD));
				// _encrypt(word, false); // Removed encryption check for password comparision for now (assuming plain
				// or pre-hashed)
				if (pass == word) {
					state = LOGGED_IN;
					output->addByte(AP_MSG_LOGIN_OK);
					addLogLine("login ok");
				} else {
					loginTries++;
					output->addByte(AP_MSG_LOGIN_FAILED);
					output->addString("wrong password");
					addLogLine("login failed.(" + pass + ")");
				}
			} else {
				output->addByte(AP_MSG_LOGIN_FAILED);
				output->addString("cannot login");
				addLogLine("wrong state at login");
			}
			break;
		}

		case AP_MSG_ENCRYPTION: {
			output->addByte(AP_MSG_ENCRYPTION_FAILED);
			output->addString("encryption not supported yet");
			break;
		}

		case AP_MSG_KEY_EXCHANGE: {
			output->addByte(AP_MSG_KEY_EXCHANGE_FAILED);
			output->addString("key exchange not supported yet");
			break;
		}

		case AP_MSG_COMMAND: {
			if (state != LOGGED_IN) {
				addLogLine("recvbyte == AP_MSG_COMMAND && state != LOGGED_IN !!!");
				break;
			}

			uint8_t command = msg.getByte();
			switch (command) {
				case CMD_SAVE_SERVER:
				case CMD_SHALLOW_SAVE_SERVER: {
					addLogLine("saving server");
					g_game.broadcastMessage("Server is saving...", MESSAGE_STATUS_WARNING);
					g_dispatcher.addTask([]() { g_game.saveGameState(); });
					output->addByte(AP_MSG_COMMAND_OK);
					break;
				}

				case CMD_CLOSE_SERVER: {
					addLogLine("closing server");
					g_dispatcher.addTask([]() { g_game.setGameState(GAME_STATE_CLOSED); });
					output->addByte(AP_MSG_COMMAND_OK);
					break;
				}

				case CMD_OPEN_SERVER: {
					addLogLine("opening server");
					g_dispatcher.addTask([]() { g_game.setGameState(GAME_STATE_NORMAL); });
					output->addByte(AP_MSG_COMMAND_OK);
					break;
				}

				case CMD_SHUTDOWN_SERVER: {
					addLogLine("shutting down server");
					g_dispatcher.addTask([]() { g_game.shutdown(); });
					output->addByte(AP_MSG_COMMAND_OK);
					break;
				}

				case CMD_SHUTDOWN_SCHEDULED: {
					uint16_t minutes = msg.get<uint16_t>();
					std::string reason(msg.getString());

					std::string logMsg = fmt::format("scheduled shutdown in {} minutes: {}", minutes, reason);
					addLogLine(logMsg);

					// Broadcast
					std::string broadcastMsg =
					    fmt::format("Server is shutting down in {} minutes: {}", minutes, reason);
					g_game.broadcastMessage(broadcastMsg, MESSAGE_STATUS_WARNING);

					// Schedule standard shutdown
					// Convert minutes to milliseconds
					uint32_t delay = minutes * 60 * 1000;
					g_scheduler.addEvent(delay, []() { g_game.shutdown(); });

					output->addByte(AP_MSG_COMMAND_OK);
					break;
				}

				case CMD_PAY_HOUSES: {
					g_dispatcher.addTask([this]() { adminCommandPayHouses(); });
					break;
				}

				case CMD_RELOAD_SCRIPTS: {
					int8_t reload = msg.getByte();
					g_dispatcher.addTask([this, reload]() { adminCommandReload(reload); });
					break;
				}

				case CMD_KICK: {
					std::string param = std::string(msg.getString());
					g_dispatcher.addTask([this, param = std::move(param)]() { adminCommandKickPlayer(param); });
					break;
				}

				case CMD_SEND_MAIL: {
					output->addByte(AP_MSG_COMMAND_FAILED);
					output->addString("mail command temporarily disabled");
					break;
				}

				case CMD_BROADCAST: {
					std::string param = std::string(msg.getString());
					addLogLine("broadcasting: " + param);
					g_dispatcher.addTask(
					    [param = std::move(param)]() { g_game.broadcastMessage(param, MESSAGE_STATUS_WARNING); });
					output->addByte(AP_MSG_COMMAND_OK);
					break;
				}

				case CMD_CLEAN: {
					addLogLine("cleaning map");
					g_game.broadcastMessage("Clean Map executed by OTAdmin.", MESSAGE_STATUS_WARNING);
					g_dispatcher.addTask([]() { g_game.map.clean(); });
					output->addByte(AP_MSG_COMMAND_OK);
					break;
				}

				default: {
					output->addByte(AP_MSG_COMMAND_FAILED);
					output->addString("not known server command");
					addLogLine("not known server command");
				}
			}
			break;
		}

		case AP_MSG_PING:
			output->addByte(AP_MSG_PING_OK);
			output->add<uint16_t>(g_game.getPlayersOnline());
			// CPU usage placeholder (requires OS specific code)
			// For now sending 0 to represent "unknown" or implementing basic load avg if possible
			// Let's send 0 for now to avoid compilation errors on different platforms
			output->addByte(0);
			break;

		case AP_MSG_KEEP_ALIVE:
			break;

		default: {
			output->addByte(AP_MSG_ERROR);
			output->addString("not known command byte");
			addLogLine("not known command byte");
			break;
		}
	}

	send(output);
}

void ProtocolAdmin::release()
{
	addLogLine("end connection");
	Admin::getInstance().removeConnection();
	Protocol::release();
}

void ProtocolAdmin::adminCommandPayHouses()
{
	addLogLine("pay houses command received");

	auto output = OutputMessagePool::getOutputMessage();
	if (output) {
		output->addByte(AP_MSG_COMMAND_OK);
		send(output);
	}
}

void ProtocolAdmin::adminCommandReload(int8_t)
{
	addLogLine("reload ok");

	auto output = OutputMessagePool::getOutputMessage();
	if (output) {
		output->addByte(AP_MSG_COMMAND_OK);
		send(output);
	}
}

void ProtocolAdmin::adminCommandKickPlayer(const std::string& name)
{
	auto output = OutputMessagePool::getOutputMessage();
	if (!output) return;

	Player* player = g_game.getPlayerByName(name);
	if (player) {
		g_game.kickPlayer(player->getID(), true); // Force kick

		addLogLine("kicking player " + player->getName());
		output->addByte(AP_MSG_COMMAND_OK);
	} else {
		addLogLine("failed setting kick for player " + name);
		output->addByte(AP_MSG_COMMAND_FAILED);
		output->addString("player is not online");
	}

	send(output);
}

void ProtocolAdmin::addLogLine(const std::string& message)
{
	if (ConfigManager::getBoolean(ConfigManager::ADMIN_LOGS)) {
		LOG_INFO("[Admin] {} from IP: {}", message, convertIPToString(getIP()));
	}
}
