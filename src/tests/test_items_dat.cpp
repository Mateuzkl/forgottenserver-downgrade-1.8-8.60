#include "../otpch.h"

#include "../items.h"
#include "dat_test_fixture.h"
#include "test_support.h"

#include <filesystem>

namespace {

using tfs::tests::dat::makeDat;
using tfs::tests::dat::TempDatFile;
using tfs::tests::dat::VisualCounts;

std::filesystem::path sourceRoot()
{
	return std::filesystem::weakly_canonical(std::filesystem::path{__FILE__}).parent_path().parent_path().parent_path();
}

class CurrentPathGuard
{
public:
	explicit CurrentPathGuard(const std::filesystem::path& path) : previous(std::filesystem::current_path())
	{
		std::filesystem::current_path(path);
	}

	~CurrentPathGuard() { std::filesystem::current_path(previous); }

private:
	std::filesystem::path previous;
};

} // namespace

TEST_CASE(loads_uint16_sprite_ids)
{
	TempDatFile file("tfs_items_dat_u16", makeDat(100, sizeof(uint16_t)));
	Items items;
	CHECK(items.loadFromDat(file.path.string()));
	CHECK(items.isLoadedFromDat());
	CHECK(items.getDatItemCount() == 100);
	CHECK(items.isValidItemId(100));
	CHECK(items[100].moveable);
	CHECK(items.isValidItemId(ITEM_BROWSEFIELD));
}

TEST_CASE(loads_uint32_sprite_ids_only_after_full_uint16_probe_fails)
{
	TempDatFile file("tfs_items_dat_u32", makeDat(100, sizeof(uint32_t)));
	Items items;
	CHECK(items.loadFromDat(file.path.string()));
	CHECK(items.isValidItemId(100));
}

TEST_CASE(ignores_declared_visual_sections)
{
	TempDatFile file("tfs_items_dat_visual_sections",
	                 makeDat(100, sizeof(uint32_t), false, VisualCounts{1, 1, 1}));
	Items items;
	CHECK(items.loadFromDat(file.path.string()));
	CHECK(items.getDatItemCount() == 100);
	CHECK(!items.isValidItemId(101));
}

TEST_CASE(loads_astra_extended_animation_layout)
{
	TempDatFile file("tfs_items_dat_astra", makeDat(100, sizeof(uint32_t), false, {}, true));
	Items items;
	CHECK(items.loadFromDat(file.path.string()));
	CHECK(items.getDatItemCount() == 100);
	CHECK(items[100].isAnimation);
}

TEST_CASE(ignores_visual_section_contents)
{
	const size_t visualOffset = makeDat().size();
	auto bytes = makeDat(100, sizeof(uint16_t), false, VisualCounts{1, 0, 0});
	bytes[visualOffset] = 34;
	TempDatFile file("tfs_items_dat_corrupt_visual", bytes);
	Items items;
	CHECK(items.loadFromDat(file.path.string()));
}

TEST_CASE(ignores_bytes_after_item_section)
{
	auto bytes = makeDat(100, sizeof(uint16_t), false, VisualCounts{1, 1, 1});
	bytes.push_back(0);
	TempDatFile file("tfs_items_dat_trailing_bytes", bytes);
	Items items;
	CHECK(items.loadFromDat(file.path.string()));
}

TEST_CASE(dat_unmoveable_applies_only_to_the_parsed_item)
{
	TempDatFile file("tfs_items_dat_unmoveable", makeDat(100, sizeof(uint16_t), true));
	Items items;
	CHECK(items.loadFromDat(file.path.string()));
	CHECK(!items[100].moveable);
	CHECK(!items[99].moveable);
	CHECK(!items[101].moveable);
}

TEST_CASE(xml_stackable_override_applies_in_dat_mode)
{
	const auto root = sourceRoot();
	CurrentPathGuard currentPath(root);
	TempDatFile file("tfs_items_dat_xml", makeDat(52947));

	Items items;
	CHECK(items.loadFromDat(file.path.string()));
	CHECK(items.loadFromXml(false, false));
	CHECK(!items[65000].stackable);
	CHECK(items[65001].stackable);
}

TEST_CASE(rejects_truncated_dat)
{
	auto bytes = makeDat(100, sizeof(uint32_t));
	bytes.pop_back();
	TempDatFile file("tfs_items_dat_truncated", bytes);
	Items items;
	CHECK(!items.loadFromDat(file.path.string()));
	CHECK(items.getLastError().find("offset") != std::string::npos);
}

TEST_CASE(rejects_unknown_dat_flag)
{
	auto bytes = makeDat();
	bytes[12] = 34;
	TempDatFile file("tfs_items_dat_unknown_flag", bytes);
	Items items;
	CHECK(!items.loadFromDat(file.path.string()));
	CHECK(items.getLastError().find("unknown flag 34") != std::string::npos);
}

TEST_CASE(rejects_bad_dat_signature_and_count)
{
	auto badSignature = makeDat();
	badSignature[0] = 0;
	TempDatFile signatureFile("tfs_items_dat_bad_signature", badSignature);
	Items signatureItems;
	CHECK(!signatureItems.loadFromDat(signatureFile.path.string()));
	CHECK(signatureItems.getLastError().find("signature") != std::string::npos);

	auto badCount = makeDat();
	badCount[4] = 99;
	badCount[5] = 0;
	TempDatFile countFile("tfs_items_dat_bad_count", badCount);
	Items countItems;
	CHECK(!countItems.loadFromDat(countFile.path.string()));
	CHECK(countItems.getLastError().find("itemCount") != std::string::npos);
}

TFS_TEST_MAIN()
