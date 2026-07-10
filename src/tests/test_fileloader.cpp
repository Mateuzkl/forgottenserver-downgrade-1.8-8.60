#include "../otpch.h"

#include "../fileloader.h"

#include "test_support.h"

#include <filesystem>
#include <fstream>

namespace {

struct TempOtbFile
{
	std::filesystem::path path;

	TempOtbFile(const char* name, std::initializer_list<uint8_t> bytes) :
	    path{std::filesystem::temp_directory_path() / name}
	{
		std::ofstream out{path, std::ios::binary | std::ios::trunc};
		for (uint8_t b : bytes) {
			out.put(static_cast<char>(b));
		}
	}

	~TempOtbFile()
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
};

constexpr OTB::Identifier TEST_ID = {{'T', 'E', 'S', 'T'}};

std::vector<uint8_t> readAll(PropStream& props)
{
	std::vector<uint8_t> out;
	uint8_t byte;
	while (props.read<uint8_t>(byte)) {
		out.push_back(byte);
	}
	return out;
}

bool loaderThrows(const std::string& file, const OTB::Identifier& id)
{
	try {
		OTB::Loader loader{file, id};
	} catch (const OTB::InvalidOTBFormat&) {
		return true;
	}
	return false;
}

} // namespace

TEST_CASE(fast_path_no_escapes_reads_props_in_place)
{
	//                                 ident                START type  props ----------------------  END
	TempOtbFile f{"tfs_fl_fast.otb", {'T', 'E', 'S', 'T', 0xFE, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0xFF}};
	OTB::Loader loader{f.path.string(), TEST_ID};
	const auto& root = loader.parseTree();
	PropStream props;
	CHECK(loader.getProps(root, props));
	CHECK(readAll(props) == (std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04, 0x05}));
}

TEST_CASE(escape_in_middle) // raw 01 FD FE 02 -> decoded 01 FE 02
{
	TempOtbFile f{"tfs_fl_mid.otb", {'T', 'E', 'S', 'T', 0xFE, 0x00, 0x01, 0xFD, 0xFE, 0x02, 0xFF}};
	OTB::Loader loader{f.path.string(), TEST_ID};
	PropStream props;
	CHECK(loader.getProps(loader.parseTree(), props));
	CHECK(readAll(props) == (std::vector<uint8_t>{0x01, 0xFE, 0x02}));
}

TEST_CASE(escape_at_start) // raw FD FD 10 -> decoded FD 10
{
	TempOtbFile f{"tfs_fl_start.otb", {'T', 'E', 'S', 'T', 0xFE, 0x00, 0xFD, 0xFD, 0x10, 0xFF}};
	OTB::Loader loader{f.path.string(), TEST_ID};
	PropStream props;
	CHECK(loader.getProps(loader.parseTree(), props));
	CHECK(readAll(props) == (std::vector<uint8_t>{0xFD, 0x10}));
}

TEST_CASE(escape_at_end_protects_end_marker) // raw 41 FD FF -> decoded 41 FF; following FF still closes the node
{
	TempOtbFile f{"tfs_fl_end.otb", {'T', 'E', 'S', 'T', 0xFE, 0x00, 0x41, 0xFD, 0xFF, 0xFF}};
	OTB::Loader loader{f.path.string(), TEST_ID};
	PropStream props;
	CHECK(loader.getProps(loader.parseTree(), props));
	CHECK(readAll(props) == (std::vector<uint8_t>{0x41, 0xFF}));
}

TEST_CASE(consecutive_escapes) // raw FD FD FD FE -> decoded FD FE
{
	TempOtbFile f{"tfs_fl_consec.otb", {'T', 'E', 'S', 'T', 0xFE, 0x00, 0xFD, 0xFD, 0xFD, 0xFE, 0xFF}};
	OTB::Loader loader{f.path.string(), TEST_ID};
	PropStream props;
	CHECK(loader.getProps(loader.parseTree(), props));
	CHECK(readAll(props) == (std::vector<uint8_t>{0xFD, 0xFE}));
}

TEST_CASE(empty_props_and_child_framing)
{
	// root(type 00, empty props) -> child(type 07, empty props)
	TempOtbFile f{"tfs_fl_empty.otb", {'T', 'E', 'S', 'T', 0xFE, 0x00, 0xFE, 0x07, 0xFF, 0xFF}};
	OTB::Loader loader{f.path.string(), TEST_ID};
	const auto& root = loader.parseTree();
	CHECK(root.children.size() == 1);
	CHECK(root.children[0].type == 0x07);
	PropStream props;
	CHECK(!loader.getProps(root, props));             // size == 0 -> false
	CHECK(!loader.getProps(root.children[0], props)); // size == 0 -> false
}

TEST_CASE(read_string_plain_and_escaped)
{
	// props: len=3 "abc", then len=1 with escaped 0xFD payload (raw 01 00 FD FD)
	TempOtbFile f{"tfs_fl_str.otb",
	              {'T', 'E', 'S', 'T', 0xFE, 0x00, 0x03, 0x00, 'a', 'b', 'c', 0x01, 0x00, 0xFD, 0xFD, 0xFF}};
	OTB::Loader loader{f.path.string(), TEST_ID};
	PropStream props;
	CHECK(loader.getProps(loader.parseTree(), props));
	auto [s1, ok1] = props.readString();
	CHECK(ok1);
	CHECK(s1 == "abc");
	auto [s2, ok2] = props.readString();
	CHECK(ok2);
	CHECK(s2.size() == 1);
	CHECK(static_cast<uint8_t>(s2[0]) == 0xFD);
	CHECK(props.size() == 0);
}

TEST_CASE(identifier_wildcard_and_rejection)
{
	TempOtbFile wild{"tfs_fl_wild.otb", {0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x11, 0xFF}};
	{
		OTB::Loader loader{wild.path.string(), TEST_ID}; // wildcard file accepted under any identifier
		PropStream props;
		CHECK(loader.getProps(loader.parseTree(), props));
	}
	TempOtbFile bad{"tfs_fl_bad.otb", {'X', 'X', 'X', 'X', 0xFE, 0x00, 0x11, 0xFF}};
	CHECK(loaderThrows(bad.path.string(), TEST_ID));
	TempOtbFile tiny{"tfs_fl_tiny.otb", {'T', 'E', 'S', 'T', 0xFE, 0x00, 0xFF}}; // 7 bytes == minimalSize -> reject
	CHECK(loaderThrows(tiny.path.string(), TEST_ID));
}

TFS_TEST_MAIN()
