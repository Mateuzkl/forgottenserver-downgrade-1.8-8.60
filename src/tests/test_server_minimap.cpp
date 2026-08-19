#include "../otpch.h"

#include "../server_minimap.h"
#include "test_support.h"

#include <algorithm>
#include <cctype>
#include <string>

TEST_CASE(server_minimap_content_version_is_deterministic_and_anonymous)
{
	constexpr std::string_view privateLookingInput = "data/world/private-map.otbm";
	const std::string first = ServerMinimap::contentVersion(privateLookingInput);
	const std::string second = ServerMinimap::contentVersion(privateLookingInput);
	const std::string changed = ServerMinimap::contentVersion("data/world/other-map.otbm");

	CHECK(first == second);
	CHECK(first != changed);
	CHECK(first.find("private-map") == std::string::npos);
	CHECK(first.size() > 17);
	CHECK(first[16] == '-');
	CHECK(
	    std::all_of(first.begin(), first.begin() + 16, [](unsigned char value) { return std::isxdigit(value) != 0; }));
}

TEST_CASE(server_minimap_empty_payload_has_no_chunks) { CHECK(ServerMinimap::splitForTransfer({}).empty()); }

TEST_CASE(server_minimap_single_chunk_uses_one_shot_marker)
{
	const std::string payload(ServerMinimap::CHUNK_PAYLOAD_SIZE, 'a');
	const auto chunks = ServerMinimap::splitForTransfer(payload);

	CHECK(chunks.size() == 1);
	CHECK(chunks.front().marker == 'O');
	CHECK(chunks.front().payload == payload);
}

TEST_CASE(server_minimap_multi_chunk_round_trip_preserves_binary_data)
{
	std::string payload(ServerMinimap::CHUNK_PAYLOAD_SIZE * 2 + 37, '\0');
	for (size_t i = 0; i < payload.size(); ++i) {
		payload[i] = static_cast<char>(i & 0xFF);
	}

	const auto chunks = ServerMinimap::splitForTransfer(payload);
	CHECK(chunks.size() == 3);
	CHECK(chunks[0].marker == 'S');
	CHECK(chunks[1].marker == 'P');
	CHECK(chunks[2].marker == 'E');
	CHECK(chunks[0].payload.size() == ServerMinimap::CHUNK_PAYLOAD_SIZE);
	CHECK(chunks[1].payload.size() == ServerMinimap::CHUNK_PAYLOAD_SIZE);
	CHECK(chunks[2].payload.size() == 37);

	std::string reconstructed;
	for (const auto& chunk : chunks) {
		reconstructed.append(chunk.payload);
	}
	CHECK(reconstructed == payload);
}

TFS_TEST_MAIN()
