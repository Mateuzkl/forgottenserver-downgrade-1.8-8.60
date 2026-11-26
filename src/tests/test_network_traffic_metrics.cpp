#include "../otpch.h"
#include "../performance_metrics.h"
#include "test_support.h"

#include <thread>

TEST_CASE(traffic_metrics_are_opt_in_and_keep_directions_separate)
{
	PerformanceMetrics metrics;
	metrics.recordIncomingGamePacket(0x65, 1);
	metrics.recordOutgoingGameChunk(0x65, 20);
	metrics.recordNetworkWrite(24, false);
	metrics.recordPendingWrite(128, true);
	metrics.recordPacketBacklogRejection();
	metrics.recordPacketRateRejection();
	auto disabled = metrics.takeNetworkTrafficSnapshot();
	CHECK(disabled.incoming[0x65].messages == 0);
	CHECK(disabled.outgoing[0x65].messages == 0);
	CHECK(disabled.writeCompletions == 0);
	CHECK(disabled.pendingWriteMaximum == 0);
	CHECK(disabled.pendingWriteRejections == 0);
	CHECK(disabled.packetBacklogRejections == 0);
	CHECK(disabled.packetRateRejections == 0);

	metrics.setEnabled(true);
	metrics.recordIncomingGamePacket(0x65, 1);
	metrics.recordIncomingGamePacket(0x65, 2);
	metrics.recordOutgoingGameChunk(0x65, 20);
	metrics.recordOutgoingGameChunk(0xA0, 50);
	const auto enabled = metrics.takeNetworkTrafficSnapshot();
	CHECK(enabled.incoming[0x65].messages == 2);
	CHECK(enabled.incoming[0x65].bytes == 3);
	CHECK(enabled.outgoing[0x65].messages == 1);
	CHECK(enabled.outgoing[0x65].bytes == 20);
	CHECK(enabled.outgoing[0xA0].messages == 1);
	CHECK(enabled.outgoing[0xA0].bytes == 50);
	CHECK(enabled.incoming[0xA0].messages == 0);
	CHECK(metrics.takeNetworkTrafficSnapshot().outgoing[0x65].messages == 0);
}

TEST_CASE(traffic_metrics_count_partial_writes_errors_and_queue_peaks)
{
	PerformanceMetrics metrics;
	metrics.setEnabled(true);
	metrics.recordNetworkWrite(100, false);
	metrics.recordNetworkWrite(7, true);
	metrics.recordPendingWrite(32);
	metrics.recordPendingWrite(4);
	metrics.recordPendingWrite(128, true);
	metrics.recordPacketBacklogRejection();
	metrics.recordPacketRateRejection();
	const auto first = metrics.takeNetworkTrafficSnapshot();
	CHECK(first.writeCompletions == 2);
	CHECK(first.writtenBytes == 107);
	CHECK(first.writeErrors == 1);
	CHECK(first.pendingWriteMaximum == 128);
	CHECK(first.pendingWriteRejections == 1);
	CHECK(first.packetBacklogRejections == 1);
	CHECK(first.packetRateRejections == 1);
	const auto next = metrics.takeNetworkTrafficSnapshot();
	CHECK(next.writeCompletions == 0);
	CHECK(next.writtenBytes == 0);
	CHECK(next.pendingWriteMaximum == 0);
	CHECK(next.pendingWriteRejections == 0);
	CHECK(next.packetBacklogRejections == 0);
	CHECK(next.packetRateRejections == 0);
}

TEST_CASE(traffic_metrics_do_not_lose_counts_from_parallel_io_threads)
{
	PerformanceMetrics metrics;
	metrics.setEnabled(true);
	std::array<std::thread, 4> workers;
	for (auto& worker : workers) {
		worker = std::thread([&] {
			for (size_t i = 0; i < 10000; ++i) {
				metrics.recordIncomingGamePacket(0x65, 3);
				metrics.recordOutgoingGameChunk(0x83, 8);
				metrics.recordNetworkWrite(14, false);
			}
		});
	}
	for (auto& worker : workers) worker.join();
	const auto result = metrics.takeNetworkTrafficSnapshot();
	CHECK(result.incoming[0x65].messages == 40000);
	CHECK(result.incoming[0x65].bytes == 120000);
	CHECK(result.outgoing[0x83].messages == 40000);
	CHECK(result.outgoing[0x83].bytes == 320000);
	CHECK(result.writeCompletions == 40000);
	CHECK(result.writtenBytes == 560000);
}

TFS_TEST_MAIN()
