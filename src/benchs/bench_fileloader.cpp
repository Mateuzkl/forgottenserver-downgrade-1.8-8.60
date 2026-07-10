#include "../otpch.h"

#include "../fileloader.h"

#include <benchmark/benchmark.h>

#include <filesystem>
#include <fstream>

namespace {

// OTB wire format (see OTB::Loader::parseTree):
//   [4-byte identifier][0xFE][root type][root props]
//     ([0xFE][type][props...][0xFF])*   <- children
//   [0xFF]
// 0xFD escapes the next byte inside props. An all-zero identifier is the
// wildcard and is accepted for any requested identifier.
//
// escapeEvery: 0 = no escapes (fast path), 1 = every logical byte escaped
// (worst case for unescaping), N = one escaped byte per N logical bytes.
std::filesystem::path makeFixture(size_t escapeEvery, size_t nodes, size_t propBytes)
{
	auto path = std::filesystem::temp_directory_path() /
	            fmt::format("tfs_bench_otb_e{}_{}x{}.otb", escapeEvery, nodes, propBytes);
	if (std::filesystem::exists(path)) {
		return path;
	}

	std::ofstream out{path, std::ios::binary};
	const char zeroId[4] = {'\0', '\0', '\0', '\0'};
	out.write(zeroId, sizeof(zeroId));

	const char START = static_cast<char>(OTB::Node::START);
	const char END = static_cast<char>(OTB::Node::END);
	const char ESC = static_cast<char>(OTB::Node::ESCAPE);

	out.put(START);
	out.put('\0'); // root type, empty root props (children follow immediately)
	for (size_t n = 0; n < nodes; ++n) {
		out.put(START);
		out.put('\1');
		for (size_t i = 0; i < propBytes; ++i) {
			if (escapeEvery != 0 && i % escapeEvery == 0) {
				out.put(ESC);   // escaped 0xFE: two bytes on disk, one logical byte
				out.put(START);
			} else {
				out.put('\x42'); // < 0xFD: never triggers the escape handling
			}
		}
		out.put(END);
	}
	out.put(END);
	return path;
}

void runGetProps(benchmark::State& state, size_t escapeEvery)
{
	const auto nodes = static_cast<size_t>(state.range(0));
	const auto propBytes = static_cast<size_t>(state.range(1));
	const auto path = makeFixture(escapeEvery, nodes, propBytes);

	OTB::Loader loader{path.string(), OTB::Identifier{{'\0', '\0', '\0', '\0'}}};
	const auto& root = loader.parseTree();

	for ([[maybe_unused]] auto _ : state) {
		size_t total = 0;
		PropStream props;
		for (const auto& node : root.children) {
			if (loader.getProps(node, props)) {
				total += props.size();
			}
		}
		benchmark::DoNotOptimize(total);
	}
	// logical (unescaped) bytes; escaped fixtures are larger on disk
	state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * nodes * propBytes));
}

void bench_getProps_clean(benchmark::State& state) { runGetProps(state, 0); }
void bench_getProps_sparse_escapes(benchmark::State& state) { runGetProps(state, 32); }
void bench_getProps_dense_escapes(benchmark::State& state) { runGetProps(state, 1); }

// {nodes, bytes/node}: many small nodes (OTBM tile shape) and few huge nodes
BENCHMARK(bench_getProps_clean)->Args({4096, 32})->Args({4096, 256})->Args({64, 65536});
BENCHMARK(bench_getProps_sparse_escapes)->Args({4096, 32})->Args({4096, 256})->Args({64, 65536});
BENCHMARK(bench_getProps_dense_escapes)->Args({4096, 32})->Args({4096, 256})->Args({64, 65536});

} // namespace

BENCHMARK_MAIN();
