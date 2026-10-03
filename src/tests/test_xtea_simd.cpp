// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in LICENSE.
#include "../../tools/xtea/reference.h"
#include "../xtea_simd.h"
#include "test_support.h"

#include <algorithm>
#include <memory>
#include <random>
#include <thread>

namespace {
struct Kernel
{
	const char* name;
	xtea::detail::Function encrypt;
	xtea::detail::Function decrypt;
};
std::vector<Kernel> kernels()
{
	std::vector<Kernel> result{{"public", xtea::encrypt, xtea::decrypt},
	                           {"Scalar", xtea::detail::encryptScalar, xtea::detail::decryptScalar}};
	if (xtea::detail::supportsBackend(xtea::Backend::SSE2))
		result.push_back({"SSE2", xtea::detail::encryptSse2, xtea::detail::decryptSse2});
	if (xtea::detail::supportsBackend(xtea::Backend::AVX2))
		result.push_back({"AVX2", xtea::detail::encryptAvx2, xtea::detail::decryptAvx2});
	return result;
}
} // namespace

TEST_CASE(backend_first_use_is_thread_safe)
{
	std::array<xtea::Backend, 16> selected;
	std::vector<std::jthread> workers;
	for (size_t i = 0; i < selected.size(); ++i)
		workers.emplace_back([&, i] { selected[i] = xtea::selectedBackend(); });
	workers.clear();
	for (auto backend : selected) CHECK(backend == selected[0]);
#ifdef TFS_XTEA_FORCE_SCALAR
	CHECK(selected[0] == xtea::Backend::Scalar);
#endif
	CHECK(xtea::detail::supportsBackend(selected[0]));
	std::cout << "Selected XTEA backend: " << xtea::backendName() << '\n';
}

TEST_CASE(all_backends_match_frozen_pr318_and_roundtrip)
{
	std::vector<xtea::key> keys{{0, 0, 0, 0},
	                            {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu},
	                            {0, 1, 2, 3},
	                            {0x01020304, 0x05060708, 0x090A0B0C, 0x0D0E0F10}};
	std::mt19937 random(180252);
	for (size_t i = 0; i < 16; ++i) {
		keys.push_back({static_cast<uint32_t>(random()), static_cast<uint32_t>(random()),
		                static_cast<uint32_t>(random()), static_cast<uint32_t>(random())});
	}
	for (const auto& key : keys) {
		const auto expanded = xtea::expand_key(key);
		CHECK(expanded == xtea_reference::expand_key(key));
		for (size_t length : {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 96, 120, 128, 136, 256, 512, 1024, 4096, 65528}) {
			for (size_t offset : {0, 1, 3, 7}) {
				// Exact allocation ending at the last block: ASan catches vector overreads.
				auto plain = std::make_unique<uint8_t[]>(length + offset + 1);
				for (size_t i = 0; i < length + offset + 1; ++i) plain[i] = static_cast<uint8_t>(random());
				std::vector<uint8_t> expected(plain.get(), plain.get() + length + offset + 1);
				xtea_reference::encrypt(expected.data() + offset, length, expanded);
				for (const auto& kernel : kernels()) {
					auto data = std::make_unique<uint8_t[]>(length + offset + 1);
					std::copy_n(plain.get(), length + offset + 1, data.get());
					kernel.encrypt(data.get() + offset, length, expanded);
					CHECK(std::equal(expected.begin(), expected.end(), data.get()));
					kernel.decrypt(data.get() + offset, length, expanded);
					CHECK(std::equal(plain.get(), plain.get() + length + offset + 1, data.get()));
				}
			}
		}
	}
	for (const auto& kernel : kernels()) {
		kernel.encrypt(nullptr, 0, {});
		kernel.decrypt(nullptr, 0, {});
	}
}

TEST_CASE(ciphertext_golden_vector_stays_identical)
{
	const auto key = xtea::expand_key({0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef});
	for (const auto& kernel : kernels()) {
		std::array<uint8_t, 8> data{0xef, 0xbe, 0xad, 0xde, 0xef, 0xbe, 0xad, 0xde};
		kernel.encrypt(data.data(), data.size(), key);
		CHECK((data == std::array<uint8_t, 8>{0xb5, 0x8c, 0xf2, 0xfa, 0xe0, 0xc0, 0x40, 0x09}));
	}
}

TFS_TEST_MAIN()
