// Standalone bounds stress for the production small outgoing writer.
// Compile with AddressSanitizer + UndefinedBehaviorSanitizer; no server or DB.
#include "../src/smallnetworkmessage.h"

#include <cassert>
#include <iostream>
#include <random>

int main()
{
	std::mt19937 random{20261026};
	for (size_t iteration = 0; iteration < 50000; ++iteration) {
		std::string input(random() % 301, '\0');
		for (char& byte : input) byte = static_cast<char>(random() % 256);
		std::string latin1;
		if (iteration % 2 != 0) {
			latin1 = input;
			input.resize(latin1.size() * 2);
			input.resize(simdutf::convert_latin1_to_utf8(latin1.data(), latin1.size(), input.data()));
		}
		SmallNetworkMessage<265> message;
		message.addByte(0x84);
		message.addPosition(Position{1234,5678,7});
		message.addByte(180);
		message.addString(input);
		const auto bytes = message.bytes();
		assert(bytes.size() >= 9 && bytes.size() <= 265);
		const size_t stringLength = bytes[7] | (static_cast<size_t>(bytes[8]) << 8);
		assert(bytes.size() == 9 + stringLength);
		if (iteration % 2 != 0 && input.size() <= 256) {
			assert(stringLength == latin1.size());
			assert(std::memcmp(bytes.data() + 9, latin1.data(), latin1.size()) == 0);
		}
		SmallNetworkMessage<2> tiny;
		tiny.addString(input);
		assert(tiny.bytes().size() == 2);
		tiny.addByte(1);
		assert(tiny.bytes().empty());
		SmallNetworkMessage<1> tooSmall;
		tooSmall.addString(input);
		assert(tooSmall.bytes().empty());
	}
	std::cout << "PASS: 50000 random UTF-8/invalid/boundary inputs and fail-closed overflow\n";
}
