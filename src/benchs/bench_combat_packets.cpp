// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"
#include "../outputmessage.h"

#include <benchmark/benchmark.h>

namespace {

// Same payload and output append order as ProtocolGame::sendMagicEffect.
// No clients, encryption, socket I/O or game simulation: this isolates serialization.
// Direct is an experimental comparison only, NOT the production implementation.
// Do not force the temporary buffer to escape: let Release/LTO eliminate dead stores.
template <bool Direct>
void bench_effect_fanout(benchmark::State& state)
{
	OutputMessage output;
	const Position position{1030, 1015, 7};
	const auto recipients = state.range(0);
	for (auto _ : state) {
		output.reset();
		for (int64_t recipient = 0; recipient < recipients; ++recipient) {
			if constexpr (Direct) {
				output.addByte(0x83);
				output.addPosition(position);
				output.add<uint16_t>(38);
			} else {
				NetworkMessage packet;
				packet.addByte(0x83);
				packet.addPosition(position);
				packet.add<uint16_t>(38);
				output.append(packet);
			}
			benchmark::ClobberMemory();
		}
		benchmark::DoNotOptimize(output.getLength());
		benchmark::DoNotOptimize(output.getBuffer());
	}
	state.SetItemsProcessed(state.iterations() * recipients);
	state.SetBytesProcessed(state.iterations() * recipients * 8);
}

BENCHMARK_TEMPLATE(bench_effect_fanout, false)->Arg(1)->Arg(100)->Arg(400);
BENCHMARK_TEMPLATE(bench_effect_fanout, true)->Arg(1)->Arg(100)->Arg(400);

} // namespace

BENCHMARK_MAIN();
