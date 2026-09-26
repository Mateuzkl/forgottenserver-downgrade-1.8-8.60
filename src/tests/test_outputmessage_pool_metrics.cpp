// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../outputmessage.h"

#include "test_support.h"

TEST_CASE(test_outputmessage_pool_distinguishes_fresh_and_reused_blocks)
{
	// Register the pool, return one block to it, then drain so the measured
	// allocation below is guaranteed to come from operator new.
	{
		auto seed = OutputMessagePool::getOutputMessage();
	}
	OutputMessagePool::drainPool();
	(void)OutputMessagePool::takeAllocationStats();

	auto fresh = OutputMessagePool::getOutputMessage();
	auto stats = OutputMessagePool::takeAllocationStats();
	CHECK(stats.fresh == 1);
	CHECK(stats.reused == 0);

	fresh.reset();
	auto reused = OutputMessagePool::getOutputMessage();
	stats = OutputMessagePool::takeAllocationStats();
	CHECK(stats.fresh == 0);
	CHECK(stats.reused == 1);
}

TFS_TEST_MAIN()
