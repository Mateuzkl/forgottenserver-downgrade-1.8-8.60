// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in the LICENSE file.

#include "../otpch.h"
#include "../tests/combat_fanout_fixture.h"
#include "../tasks.h"

#include <benchmark/benchmark.h>

namespace {

// Real Game/Player/ProtocolSpectator/ProtocolGame/output-copy path in memory.
// Separate output per connection, including cast/spies; no sockets/encryption,
// combat simulation, DB or live bots. The legacy oracle retains original checks.
template <bool Prepared>
void bench_effect_delivery(benchmark::State& state)
{
	const auto owners = static_cast<size_t>(state.range(0));
	const auto followers = static_cast<size_t>(state.range(1));
	CombatFanoutFixture fixture(owners, followers, followers);
	for (auto _ : state) {
		fixture.reset();
		if constexpr (Prepared) {
			g_game.addMagicEffect(fixture.spectators, fixture.position, 0x1234);
		} else {
			ProtocolGameCombatPacketsTestAccess::legacyFanout(fixture.spectators, fixture.position, 0x1234);
		}
		for (auto& protocol : fixture.protocols) {
			benchmark::DoNotOptimize(protocol->getCurrentBuffer()->getBuffer());
		}
		benchmark::ClobberMemory();
	}
	const auto recipients = static_cast<int64_t>(fixture.protocols.size());
	state.SetItemsProcessed(state.iterations() * recipients);
	state.SetBytesProcessed(state.iterations() * recipients * 8);
	state.counters["recipients"] = static_cast<double>(recipients);
}

void sizes(benchmark::internal::Benchmark* benchmark)
{
	for (int64_t owners : {0, 1, 10, 100, 300, 600}) {
		benchmark->Args({owners, 0});
		if (owners > 0) {
			benchmark->Args({owners, 1});
		}
	}
}

BENCHMARK_TEMPLATE(bench_effect_delivery, false)->Apply(sizes);
BENCHMARK_TEMPLATE(bench_effect_delivery, true)->Apply(sizes);

} // namespace

int main(int argc, char** argv)
{
	benchmark::Initialize(&argc, argv);
	if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
		return EXIT_FAILURE;
	}
	g_dispatcher.start();
	g_dispatcher.addTask([] {
		benchmark::RunSpecifiedBenchmarks();
		benchmark::Shutdown();
		g_dispatcher.shutdown();
	});
	g_reactor.runLoop();
	return EXIT_SUCCESS;
}
