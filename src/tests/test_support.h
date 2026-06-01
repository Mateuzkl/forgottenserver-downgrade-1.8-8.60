#ifndef FS_TEST_SUPPORT_H
#define FS_TEST_SUPPORT_H

#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace tfs::tests {

struct TestCase
{
	std::string_view name;
	void (*function)();
};

inline std::vector<TestCase>& registry()
{
	static std::vector<TestCase> tests;
	return tests;
}

struct Registrar
{
	Registrar(std::string_view name, void (*function)()) { registry().push_back({name, function}); }
};

inline void check(bool condition, std::string_view expression, std::string_view file, int line)
{
	if (condition) {
		return;
	}

	std::ostringstream message;
	message << file << ':' << line << ": check failed: " << expression;
	throw std::runtime_error(message.str());
}

inline int run()
{
	int failures = 0;
	for (const TestCase& test : registry()) {
		try {
			test.function();
		} catch (const std::exception& e) {
			++failures;
			std::cerr << "[FAIL] " << test.name << ": " << e.what() << '\n';
		} catch (...) {
			++failures;
			std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
		}
	}

	if (failures == 0) {
		std::cout << registry().size() << " test(s) passed\n";
		return EXIT_SUCCESS;
	}

	std::cerr << failures << " test(s) failed\n";
	return EXIT_FAILURE;
}

} // namespace tfs::tests

#define TFS_TEST_JOIN_IMPL(a, b) a##b
#define TFS_TEST_JOIN(a, b) TFS_TEST_JOIN_IMPL(a, b)
#define TEST_CASE(name) \
	static void name(); \
	static ::tfs::tests::Registrar TFS_TEST_JOIN(test_registrar_, name){#name, &name}; \
	static void name()
#define CHECK(expression) ::tfs::tests::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define TFS_TEST_MAIN() int main() { return ::tfs::tests::run(); }

#endif // FS_TEST_SUPPORT_H
