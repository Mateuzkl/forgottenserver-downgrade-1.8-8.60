#include "../otpch.h"

#include "../networkmessage.h"
#include "../outputmessage.h"
#include "small_packet_probe.h"
#include <benchmark/benchmark.h>

static void baselineEffect(benchmark::State& state)
{
	OutputMessage output;
	Position position{100, 200, 7};
	for (auto _ : state) {
		benchmark::DoNotOptimize(position);
		NetworkMessage message;
		// Model the escape into the out-of-line production sender; otherwise a
		// compiler can eliminate initialization that this microbench never reads.
		benchmark::DoNotOptimize(message.getBuffer());
		message.addByte(0x83);
		message.addPosition(position);
		message.add<uint16_t>(65535);
		output.reset();
		output.append(message);
		benchmark::DoNotOptimize(output.getBuffer());
		benchmark::ClobberMemory();
	}
	state.SetBytesProcessed(state.iterations() * 8);
}

static void smallEffectProbe(benchmark::State& state)
{
	OutputMessage output;
	Position position{100, 200, 7};
	for (auto _ : state) {
		benchmark::DoNotOptimize(position);
		const auto bytes = smallEffectPacketProbe(position, 65535);
		benchmark::DoNotOptimize(bytes.data());
		output.reset();
		output.append(std::span<const uint8_t>{bytes});
		benchmark::DoNotOptimize(output.getBuffer());
		benchmark::ClobberMemory();
	}
	state.SetBytesProcessed(state.iterations() * 8);
}

BENCHMARK(baselineEffect);
BENCHMARK(smallEffectProbe);
BENCHMARK_MAIN();
