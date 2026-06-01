// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_SIGNALHANDLINGTHREAD_H
#define FS_SIGNALHANDLINGTHREAD_H

#include <asio.hpp>

class Signals
{
	asio::signal_set set;

public:
	explicit Signals(asio::io_context& service);

private:
	void asyncWait();
};

#endif
