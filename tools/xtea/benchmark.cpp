// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in LICENSE.
#include "../../src/xtea_simd.h"
#include "reference.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

int main()
{
	struct Kernel
	{
		const char* name;
		xtea::detail::Function encrypt;
		xtea::detail::Function decrypt;
	};
	std::vector<Kernel> kernels{{"baseline", xtea_reference::encrypt, xtea_reference::decrypt},
	                            {"scalar", xtea::detail::encryptScalar, xtea::detail::decryptScalar},
	                            {"auto", xtea::encrypt, xtea::decrypt}};
	if (xtea::detail::supportsBackend(xtea::Backend::SSE2))
		kernels.push_back({"sse2", xtea::detail::encryptSse2, xtea::detail::decryptSse2});
	if (xtea::detail::supportsBackend(xtea::Backend::AVX2))
		kernels.push_back({"avx2", xtea::detail::encryptAvx2, xtea::detail::decryptAvx2});
	std::fprintf(stderr, "Selected backend: %.*s\n", int(xtea::backendName().size()), xtea::backendName().data());
	std::puts("direction,bytes,backend,ns_op,ns_byte,MiB_s,speedup_baseline");
	const auto key = xtea::expand_key({1, 2, 3, 4});
	for (bool decrypt : {false, true}) {
		for (size_t length : {8, 32, 64, 128, 256, 512, 1024, 4096}) {
			std::vector<std::vector<double>> samples(kernels.size());
			std::vector<uint8_t> data(length, 0x33);
			// Rotate order over five matched samples; avoid one cold baseline.
			for (size_t repetition = 0; repetition < 5; ++repetition) {
				for (size_t order = 0; order < kernels.size(); ++order) {
					const size_t index = (order + repetition) % kernels.size();
					auto function = decrypt ? kernels[index].decrypt : kernels[index].encrypt;
					for (size_t i = 0; i < 1000; ++i) function(data.data(), length, key);
					const size_t iterations = 30000;
					auto started = std::chrono::steady_clock::now();
					for (size_t i = 0; i < iterations; ++i) function(data.data(), length, key);
					samples[index].push_back(
					    std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started).count() /
					    iterations);
					std::fprintf(stderr, "%u ", unsigned(data[0])); // Observable after every timed batch.
				}
			}
			for (auto& sample : samples) std::sort(sample.begin(), sample.end());
			for (size_t i = 0; i < kernels.size(); ++i) {
				const double ns = samples[i][2];
				std::printf("%s,%zu,%s,%.3f,%.3f,%.3f,%.3f\n", decrypt ? "decrypt" : "encrypt", length, kernels[i].name,
				            ns, ns / length, length * 1e9 / ns / 1048576.0, samples[0][2] / ns);
			}
		}
	}
}
