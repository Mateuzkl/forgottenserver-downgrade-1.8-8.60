#include "../otpch.h"

#include "../tools.h"

#include <benchmark/benchmark.h>

static void bench_caseInsensitiveEqual(benchmark::State& state)
{
	std::string a(state.range(0), 'a');
	std::string b(state.range(0), 'A');
	for ([[maybe_unused]] auto _ : state) {
		auto result = caseInsensitiveEqual(a, b);
		benchmark::DoNotOptimize(result);
	}
}
BENCHMARK(bench_caseInsensitiveEqual)->Range(8, 4096);

static void bench_trimString(benchmark::State& state)
{
	std::string base(state.range(0), 'x');
	for ([[maybe_unused]] auto _ : state) {
		std::string s = " \t\n" + base + " \t\n";
		trimString(s);
		benchmark::DoNotOptimize(s);
	}
}
BENCHMARK(bench_trimString)->Range(8, 4096);

BENCHMARK_MAIN();
