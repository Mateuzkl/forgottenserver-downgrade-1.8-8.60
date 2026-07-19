// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_ADMIN_CONSOLE_H
#define FS_ADMIN_CONSOLE_H

class AdminConsole
{
public:
	AdminConsole() = default;
	~AdminConsole();

	AdminConsole(const AdminConsole&) = delete;
	AdminConsole& operator=(const AdminConsole&) = delete;

	bool start();
	void stop();

private:
	void run(std::stop_token stopToken);
	void processCommand(std::string command) const;

	std::jthread inputThread;
	std::atomic<bool> running{false};
#ifdef _WIN32
	unsigned long originalInputMode = 0;
	bool restoreInputMode = false;
#endif
};

#endif // FS_ADMIN_CONSOLE_H
