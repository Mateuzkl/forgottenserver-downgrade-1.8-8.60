#include "../otpch.h"

#include "../matrixarea.h"

#include "test_support.h"

TEST_CASE(test_createArea)
{
	// clang-format off
	auto m = createArea({
        0, 0, 1, 1,
        3, 1, 1, 1,
        0, 0, 1, 1,
    }, 3);
	// clang-format on

	auto&& [centerX, centerY] = m.getCenter();
	CHECK(centerX == 0);
	CHECK(centerY == 1);

	CHECK(m.getCols() == 4);
	CHECK(m.getRows() == 3);

	CHECK(!m(0, 0));
	CHECK(!m(0, 1));
	CHECK(m(0, 2));
	CHECK(m(0, 3));
	CHECK(m(1, 0));
	CHECK(m(1, 1));
	CHECK(m(1, 2));
	CHECK(m(1, 3));
	CHECK(!m(2, 0));
	CHECK(!m(2, 1));
	CHECK(m(2, 2));
	CHECK(m(2, 3));
}

TEST_CASE(test_MatrixArea_flip)
{
	// clang-format off
	auto m = createArea({
        0, 0, 3,
        0, 1, 1,
        1, 1, 1,
        0, 1, 1,
    }, 4).flip();
	// clang-format on

	/** expected area:
	 * 0, 1, 1,
	 * 1, 1, 1,
	 * 0, 1, 1,
	 * 0, 0, 3,
	 */
	auto&& [centerX, centerY] = m.getCenter();
	CHECK(centerX == 2);
	CHECK(centerY == 3);

	CHECK(m.getCols() == 3);
	CHECK(m.getRows() == 4);

	CHECK(!m(0, 0));
	CHECK(m(0, 1));
	CHECK(m(0, 2));
	CHECK(m(1, 0));
	CHECK(m(1, 1));
	CHECK(m(1, 2));
	CHECK(!m(2, 0));
	CHECK(m(2, 1));
	CHECK(m(2, 2));
	CHECK(!m(3, 0));
	CHECK(!m(3, 1));
	CHECK(m(3, 2));
}

TEST_CASE(test_MatrixArea_mirror)
{
	// clang-format off
	auto m = createArea({
        3, 1, 1, 1,
        0, 1, 1, 1,
        0, 0, 1, 0,
    }, 3).mirror();
	// clang-format on

	/** expected area:
	 * 1, 1, 1, 3,
	 * 1, 1, 1, 0,
	 * 0, 1, 0, 0,
	 */
	auto&& [centerX, centerY] = m.getCenter();
	CHECK(centerX == 3);
	CHECK(centerY == 0);

	CHECK(m.getCols() == 4);
	CHECK(m.getRows() == 3);

	CHECK(m(0, 0));
	CHECK(m(0, 1));
	CHECK(m(0, 2));
	CHECK(m(0, 3));
	CHECK(m(1, 0));
	CHECK(m(1, 1));
	CHECK(m(1, 2));
	CHECK(!m(1, 3));
	CHECK(!m(2, 0));
	CHECK(m(2, 1));
	CHECK(!m(2, 2));
	CHECK(!m(2, 3));
}

TEST_CASE(test_MatrixArea_transpose)
{
	// clang-format off
	auto m = createArea({
        0, 1, 1, 1,
        3, 1, 1, 1,
        0, 0, 1, 0,
    }, 3).transpose();
	// clang-format on

	/** expected area:
	 * 0, 3, 0,
	 * 1, 1, 0,
	 * 1, 1, 1,
	 * 1, 1, 0,
	 */
	auto&& [centerX, centerY] = m.getCenter();
	CHECK(centerX == 1);
	CHECK(centerY == 0);

	CHECK(m.getCols() == 3);
	CHECK(m.getRows() == 4);

	CHECK(!m(0, 0));
	CHECK(m(0, 1));
	CHECK(!m(0, 2));
	CHECK(m(1, 0));
	CHECK(m(1, 1));
	CHECK(!m(1, 2));
	CHECK(m(2, 0));
	CHECK(m(2, 1));
	CHECK(m(2, 2));
	CHECK(m(3, 0));
	CHECK(m(3, 1));
	CHECK(!m(3, 2));
}

TEST_CASE(test_MatrixArea_rotate90)
{
	// clang-format off
	auto m = createArea({
        3, 1, 1, 1,
        0, 1, 1, 1,
        0, 0, 1, 0,
    }, 3).rotate90();
	// clang-format on

	/** expected area:
	 * 0, 0, 3,
	 * 0, 1, 1,
	 * 1, 1, 1,
	 * 0, 1, 1,
	 */
	auto&& [centerX, centerY] = m.getCenter();
	CHECK(centerX == 2);
	CHECK(centerY == 0);

	CHECK(m.getCols() == 3);
	CHECK(m.getRows() == 4);

	CHECK(!m(0, 0));
	CHECK(!m(0, 1));
	CHECK(m(0, 2));
	CHECK(!m(1, 0));
	CHECK(m(1, 1));
	CHECK(m(1, 2));
	CHECK(m(2, 0));
	CHECK(m(2, 1));
	CHECK(m(2, 2));
	CHECK(!m(3, 0));
	CHECK(m(3, 1));
	CHECK(m(3, 2));
}

TEST_CASE(test_MatrixArea_rotate180)
{
	// clang-format off
	auto m = createArea({
        3, 1, 1, 1,
        0, 1, 1, 1,
        0, 0, 1, 0,
    }, 3).rotate180();
	// clang-format on

	/** expected area:
	 * 0, 1, 0, 0,
	 * 1, 1, 1, 0,
	 * 1, 1, 1, 3,
	 */
	auto&& [centerX, centerY] = m.getCenter();
	CHECK(centerX == 3);
	CHECK(centerY == 2);

	CHECK(m.getCols() == 4);
	CHECK(m.getRows() == 3);

	CHECK(!m(0, 0));
	CHECK(m(0, 1));
	CHECK(!m(0, 2));
	CHECK(!m(0, 3));
	CHECK(m(1, 0));
	CHECK(m(1, 1));
	CHECK(m(1, 2));
	CHECK(!m(1, 3));
	CHECK(m(2, 0));
	CHECK(m(2, 1));
	CHECK(m(2, 2));
	CHECK(m(2, 3));
}

TEST_CASE(test_MatrixArea_rotate270)
{
	// clang-format off
	auto m = createArea({
        3, 1, 1, 1,
        0, 1, 1, 1,
        0, 0, 1, 0,
    }, 3).rotate270();
	// clang-format on

	/** expected area:
	 * 1, 1, 0,
	 * 1, 1, 1,
	 * 1, 1, 0,
	 * 3, 0, 0,
	 */
	auto&& [centerX, centerY] = m.getCenter();
	CHECK(centerX == 0);
	CHECK(centerY == 3);

	CHECK(m.getCols() == 3);
	CHECK(m.getRows() == 4);

	CHECK(m(0, 0));
	CHECK(m(0, 1));
	CHECK(!m(0, 2));
	CHECK(m(1, 0));
	CHECK(m(1, 1));
	CHECK(m(1, 2));
	CHECK(m(2, 0));
	CHECK(m(2, 1));
	CHECK(!m(2, 2));
	CHECK(m(3, 0));
	CHECK(!m(3, 1));
	CHECK(!m(3, 2));
}

TFS_TEST_MAIN()
