// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../protocolgame.h"

#include "test_support.h"

namespace {

constexpr std::string_view TEST_KEY =
    "000102030405060708090a0b0c0d0e0f"
    "101112131415161718191a1b1c1d1e1f";
constexpr std::string_view CHALLENGE_FIXTURE =
    "BDFAAAgHBgUEAwIBGBcWFRQTEhGJaMN4JCMiITQzMjHDmsDWwZyBL2x0JkeLnfZK"
    "uh5cquN3PjXdZF9jFhODjw==";
constexpr std::string_view RESPONSE_FIXTURE =
    "BHJAAAgHBgUEAwIBGBcWFRQTEhGJaMP4JCMiITQzMjFUzlGlLxZziNzvjm35FsDk"
    "EQML/nLhCLkPmDgXlG6RaA==";

constexpr uint64_t TIMESTAMP = 0x0102030405060708;
constexpr uint64_t SESSION_ID = 0x1112131415161718;
constexpr uint32_t SEQUENCE = 0x21222324;
constexpr uint32_t NONCE = 0x31323334;

void clearBytes(DllCheckProtocol::Payload& payload, std::size_t offset, std::size_t count)
{
	std::fill_n(payload.begin() + offset, count, 0);
}

bool resignAndParse(const DllCheckProtocol::HmacKey& key,
                    DllCheckProtocol::Payload payload)
{
	CHECK(DllCheckProtocol::signPayload(key, payload));
	DllCheckProtocol::Fields fields;
	return DllCheckProtocol::parseResponse(key, payload, fields);
}

} // namespace

TEST_CASE(dllcheck_v4_known_fixtures_match_wire_format)
{
	DllCheckProtocol::HmacKey key = {};
	CHECK(DllCheckProtocol::decodeHmacKey(TEST_KEY, key));

	DllCheckProtocol::Payload challenge = {};
	CHECK(DllCheckProtocol::buildChallenge(
	    key, TIMESTAMP, SESSION_ID, SEQUENCE, NONCE, challenge));
	CHECK(DllCheckProtocol::encodePayload(challenge) == CHALLENGE_FIXTURE);

	DllCheckProtocol::Payload response = {};
	CHECK(DllCheckProtocol::decodePayload(RESPONSE_FIXTURE, response));
	CHECK(DllCheckProtocol::encodePayload(response) == RESPONSE_FIXTURE);

	DllCheckProtocol::Fields fields;
	CHECK(DllCheckProtocol::parseResponse(key, response, fields));
	CHECK(fields.timestamp == TIMESTAMP);
	CHECK(fields.sessionId == SESSION_ID);
	CHECK(fields.build == (DllCheckProtocol::BUILD_ID | DllCheckProtocol::INTEGRITY_FLAG));
	CHECK(fields.sequence == SEQUENCE);
	CHECK(fields.nonce == NONCE);
}

TEST_CASE(dllcheck_v4_rejects_invalid_key_and_base64)
{
	DllCheckProtocol::HmacKey key = {};
	CHECK(DllCheckProtocol::decodeHmacKey(TEST_KEY, key));
	CHECK(!DllCheckProtocol::decodeHmacKey(TEST_KEY.substr(2), key));
	CHECK(!DllCheckProtocol::decodeHmacKey(
	    "g00102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key));

	DllCheckProtocol::Payload payload = {};
	CHECK(!DllCheckProtocol::decodePayload("", payload));
	CHECK(!DllCheckProtocol::decodePayload("AAAA", payload));
	std::string malformed(RESPONSE_FIXTURE);
	malformed[5] = '!';
	CHECK(!DllCheckProtocol::decodePayload(malformed, payload));
	CHECK(!DllCheckProtocol::decodePayload(RESPONSE_FIXTURE.substr(0, 84), payload));
}

TEST_CASE(dllcheck_v4_rejects_each_authenticated_field_error)
{
	DllCheckProtocol::HmacKey key = {};
	CHECK(DllCheckProtocol::decodeHmacKey(TEST_KEY, key));
	DllCheckProtocol::Payload valid = {};
	CHECK(DllCheckProtocol::decodePayload(RESPONSE_FIXTURE, valid));

	auto payload = valid;
	payload[0] = 3;
	CHECK(!resignAndParse(key, payload));

	payload = valid;
	payload[1] = DllCheckProtocol::CHALLENGE_KIND;
	CHECK(!resignAndParse(key, payload));

	payload = valid;
	payload[2] = 63;
	CHECK(!resignAndParse(key, payload));

	payload = valid;
	clearBytes(payload, 4, 8);
	CHECK(!resignAndParse(key, payload));

	payload = valid;
	clearBytes(payload, 12, 8);
	CHECK(!resignAndParse(key, payload));

	payload = valid;
	payload[20] ^= 0x01;
	CHECK(!resignAndParse(key, payload));

	payload = valid;
	payload[23] &= 0x7F;
	CHECK(!resignAndParse(key, payload));

	payload = valid;
	clearBytes(payload, 24, 4);
	CHECK(!resignAndParse(key, payload));

	payload = valid;
	payload.back() ^= 0x01;
	DllCheckProtocol::Fields fields;
	CHECK(!DllCheckProtocol::parseResponse(key, payload, fields));

	DllCheckProtocol::HmacKey otherKey = key;
	otherKey[0] ^= 0xFF;
	CHECK(!DllCheckProtocol::parseResponse(otherKey, valid, fields));
}

TEST_CASE(dllcheck_v4_rejects_wrong_session_and_response_window)
{
	DllCheckProtocol::Fields expected;
	expected.timestamp = TIMESTAMP;
	expected.sessionId = SESSION_ID;
	expected.sequence = SEQUENCE;
	expected.nonce = NONCE;
	DllCheckProtocol::Fields received = expected;
	CHECK(DllCheckProtocol::matchesExpectedResponse(received, expected));

	++received.timestamp;
	CHECK(!DllCheckProtocol::matchesExpectedResponse(received, expected));
	received = expected;
	++received.sessionId;
	CHECK(!DllCheckProtocol::matchesExpectedResponse(received, expected));
	received = expected;
	++received.sequence;
	CHECK(!DllCheckProtocol::matchesExpectedResponse(received, expected));
	received = expected;
	++received.nonce;
	CHECK(!DllCheckProtocol::matchesExpectedResponse(received, expected));

	CHECK(DllCheckProtocol::isResponseWindowValid(true, 1000, 1000));
	CHECK(DllCheckProtocol::isResponseWindowValid(true, 1000, 5999));
	CHECK(!DllCheckProtocol::isResponseWindowValid(false, 1000, 1000));
	CHECK(!DllCheckProtocol::isResponseWindowValid(true, 1000, 999));
	CHECK(!DllCheckProtocol::isResponseWindowValid(true, 1000, 6000));
}

TFS_TEST_MAIN()
