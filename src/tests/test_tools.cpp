#include "../otpch.h"

#include "../tools.h"

#include "test_support.h"

TEST_CASE(test_case_insensitive_helpers)
{
	CHECK(caseInsensitiveEqual("Forgotten", "fOrGoTtEn"));
	CHECK(!caseInsensitiveEqual("server", "servers"));
	CHECK(caseInsensitiveStartsWith("Forgotten Server", "fOrGoTtEn"));
	CHECK(!caseInsensitiveStartsWith("server", "servers"));
	CHECK(caseInsensitiveContains("Forgotten Server", "SERVER"));
	CHECK(caseInsensitiveContains("Forgotten Server", ""));
	CHECK(!caseInsensitiveContains("Forgotten Server", "client"));

	const std::string highByte(1, static_cast<char>(0xFF));
	CHECK(caseInsensitiveEqual(highByte, highByte));
	CHECK(caseInsensitiveStartsWith(highByte, highByte));
	CHECK(caseInsensitiveContains(highByte, highByte));
}

TEST_CASE(test_trim_helpers)
{
	std::string whitespace = " \t\n";
	trimString(whitespace);
	CHECK(whitespace.empty());

	std::string left = " \t value  ";
	trimLeftString(left);
	CHECK(left == "value  ");

	CHECK(asTrimmedString("  value \t") == "value");
	CHECK(asTrimmedString(" \t\n").empty());
}

TEST_CASE(test_case_conversion_helpers)
{
	CHECK(asLowerCaseString("MiXeD") == "mixed");
	CHECK(asUpperCaseString("MiXeD") == "MIXED");

	std::string lower = "MiXeD";
	toLowerCaseString(lower);
	CHECK(lower == "mixed");

	std::string upper = "MiXeD";
	toUpperCaseString(upper);
	CHECK(upper == "MIXED");

	std::string highByte = "A";
	highByte.push_back(static_cast<char>(0xFF));
	highByte.push_back('Z');
	toLowerCaseString(highByte);
	CHECK(highByte.size() == 3);
	CHECK(highByte.front() == 'a');
	CHECK(highByte.back() == 'z');
	toUpperCaseString(highByte);
	CHECK(highByte.front() == 'A');
	CHECK(highByte.back() == 'Z');
}

TEST_CASE(test_replace_string)
{
	std::string unchanged = "value";
	replaceString(unchanged, "", "ignored");
	CHECK(unchanged == "value");

	std::string nonOverlapping = "aaaa";
	replaceString(nonOverlapping, "aa", "b");
	CHECK(nonOverlapping == "bb");

	std::string overlapping = "aaa";
	replaceString(overlapping, "aa", "b");
	CHECK(overlapping == "ba");

	std::string growing = "a";
	replaceString(growing, "a", "aa");
	CHECK(growing == "aa");
}

TEST_CASE(test_explode_string)
{
	const auto withEmptyParts = explodeString(",value,", ",");
	const std::vector<std::string_view> expectedWithEmptyParts{"", "value", ""};
	CHECK(withEmptyParts == expectedWithEmptyParts);

	const auto limited = explodeString("first,second,third", ",", 1);
	const std::vector<std::string_view> expectedLimited{"first", "second,third"};
	CHECK(limited == expectedLimited);
}

TEST_CASE(test_vector_atoi)
{
	const IntegerVector expected{-2, 0, 42};
	CHECK(vectorAtoi({"-2", "0", "42"}) == expected);

	bool threw = false;
	try {
		(void)vectorAtoi({"invalid"});
	} catch (const std::invalid_argument&) {
		threw = true;
	}
	CHECK(threw);
}

TFS_TEST_MAIN()
