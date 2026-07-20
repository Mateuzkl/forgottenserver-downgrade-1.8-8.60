#include "otpch.h"

#include "configmanager.h"
#include "logger.h"
#include "otserv.h"
#include "outputmessage.h"
#include "tools.h"

#include <cerrno>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
bool shouldWaitForInteractiveConsole()
{
	if (const char* ci = std::getenv("CI"); ci && *ci != '\0') {
		return false;
	}
	if (_isatty(_fileno(stdin)) == 0 || _isatty(_fileno(stdout)) == 0) {
		return false;
	}

	const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
	DWORD consoleMode = 0;
	if (input == nullptr || input == INVALID_HANDLE_VALUE || GetConsoleMode(input, &consoleMode) == 0) {
		return false;
	}

	DWORD processIds[2]{};
	return GetConsoleProcessList(processIds, static_cast<DWORD>(std::size(processIds))) == 1;
}

void waitForInteractiveConsoleOnStartupFailure()
{
	if (!shouldWaitForInteractiveConsole()) {
		return;
	}

	fmt::print("Press ENTER to close the server...");
	std::fflush(stdout);
	std::string line;
	std::getline(std::cin, line);
}
#else
void waitForInteractiveConsoleOnStartupFailure() {}
#endif

bool restartCurrentProcess(char* const* argv)
{
	fmt::print("Restarting server process...\n");
	std::fflush(stdout);
	shutdownLogger();

#ifdef _WIN32
	_execvp(argv[0], argv);
#else
	execvp(argv[0], argv);
#endif

	fmt::print(stderr, "Failed to restart the server process: {}\n", std::strerror(errno));
	return false;
}

} // namespace

static bool argumentsHandler(const std::vector<std::string_view>& args)
{
	for (const auto& arg : args) {
		if (arg == "--help") {
			std::clog << "Usage:\n"
			             "\n"
			             "\t--config=$1\t\tAlternate configuration file path.\n"
			             "\t--ip=$1\t\t\tIP address of the server.\n"
			             "\t\t\t\tShould be equal to the global IP.\n"
			             "\t--login-port=$1\tPort for login server to listen on.\n"
			             "\t--game-port=$1\tPort for game server to listen on.\n";
			std::clog << "\t--rebuild-map-cache\tIgnore and rebuild the persistent map cache.\n";
			return false;
		} else if (arg == "--version") {
			printServerVersion();
			return false;
		} else if (arg == "--rebuild-map-cache") {
			ConfigManager::setBoolean(ConfigManager::REBUILD_MAP_CACHE, true);
			continue;
		}

		auto tmp = explodeString(arg, "=");

		if (tmp[0] == "--config")
			ConfigManager::setString(ConfigManager::CONFIG_FILE, tmp[1]);
		else if (tmp[0] == "--ip")
			ConfigManager::setString(ConfigManager::IP, tmp[1]);
		else if (tmp[0] == "--login-port")
			ConfigManager::setInteger(ConfigManager::LOGIN_PORT, std::stoi(tmp[1].data()));
		else if (tmp[0] == "--game-port")
			ConfigManager::setInteger(ConfigManager::GAME_PORT, std::stoi(tmp[1].data()));
	}

	return true;
}

int main(int argc, char** argv)
{
	std::vector<std::string_view> args(argv, argv + argc);
	if (!argumentsHandler(args)) {
		return 1;
	}

	const int exitCode = startServer();
	OutputMessagePool::drainPool();
	if (exitCode == SERVER_RESTART_EXIT_CODE) {
		if (!restartCurrentProcess(argv)) {
			waitForInteractiveConsoleOnStartupFailure();
			return EXIT_FAILURE;
		}
		return SERVER_RESTART_EXIT_CODE;
	}
	if (exitCode != EXIT_SUCCESS) {
		waitForInteractiveConsoleOnStartupFailure();
	}
	return exitCode;
}
