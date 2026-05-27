#include "otpch.h"

#include "matrixarea.h"

MatrixArea MatrixArea::flip() const
{
	Container newArr(arr.size());
	for (uint32_t row = 0; row < rows; ++row) {
		for (uint32_t col = 0; col < cols; ++col) {
			newArr[row * cols + col] = arr[(rows - row - 1) * cols + col];
		}
	}
	auto&& [centerX, centerY] = center;
	return {{centerX, rows - centerY - 1}, rows, cols, std::move(newArr)};
}

MatrixArea MatrixArea::mirror() const
{
	Container newArr(arr.size());
	for (uint32_t row = 0; row < rows; ++row) {
		for (uint32_t col = 0; col < cols; ++col) {
			newArr[row * cols + col] = arr[row * cols + (cols - col - 1)];
		}
	}
	auto&& [centerX, centerY] = center;
	return {{cols - centerX - 1, centerY}, rows, cols, std::move(newArr)};
}

MatrixArea MatrixArea::transpose() const
{
	Container newArr(arr.size());
	for (uint32_t row = 0; row < rows; ++row) {
		for (uint32_t col = 0; col < cols; ++col) {
			newArr[col * rows + row] = (*this)(row, col);
		}
	}
	auto&& [centerX, centerY] = center;
	return {{centerY, centerX}, cols, rows, std::move(newArr)};
}

MatrixArea MatrixArea::rotate90() const
{
	Container newArr(arr.size());
	for (uint32_t row = 0; row < rows; ++row) {
		for (uint32_t col = 0; col < cols; ++col) {
			newArr[col * rows + (rows - row - 1)] = arr[row * cols + col];
		}
	}
	auto&& [centerX, centerY] = center;
	return {{rows - centerY - 1, centerX}, cols, rows, std::move(newArr)};
}

MatrixArea MatrixArea::rotate180() const
{
	Container newArr(arr.size());
	std::reverse_copy(std::begin(arr), std::end(arr), std::begin(newArr));
	auto&& [centerX, centerY] = center;
	return {{cols - centerX - 1, rows - centerY - 1}, rows, cols, std::move(newArr)};
}

MatrixArea MatrixArea::rotate270() const
{
	Container newArr(arr.size());
	for (uint32_t row = 0; row < rows; ++row) {
		for (uint32_t col = 0; col < cols; ++col) {
			newArr[(cols - col - 1) * rows + row] = arr[row * cols + col];
		}
	}
	auto&& [centerX, centerY] = center;
	return {{centerY, cols - centerX - 1}, cols, rows, std::move(newArr)};
}

MatrixArea createArea(const std::vector<uint32_t>& vec, uint32_t rows)
{
	uint32_t cols;
	if (rows == 0) {
		cols = 0;
	} else {
		cols = vec.size() / rows;
	}

	MatrixArea area{rows, cols};

	uint32_t x = 0;
	uint32_t y = 0;

	for (uint32_t value : vec) {
		if (value == 1 || value == 3) {
			area(y, x) = true;
		}

		if (value == 2 || value == 3) {
			area.setCenter(y, x);
		}

		++x;

		if (cols == x) {
			x = 0;
			++y;
		}
	}
	return area;
}
