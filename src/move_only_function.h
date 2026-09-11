// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_MOVE_ONLY_FUNCTION_H
#define FS_MOVE_ONLY_FUNCTION_H

#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

// Zig ships a modern C++ standard library but intentionally omits
// std::move_only_function. Keep the project buildable with that portable
// toolchain while using the standard implementation everywhere it exists.
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
using MoveOnlyFunction = std::move_only_function<void()>;
#else
class MoveOnlyFunction
{
public:
	MoveOnlyFunction() noexcept = default;
	MoveOnlyFunction(std::nullptr_t) noexcept {}

	template <typename Callable>
	    requires(!std::same_as<std::remove_cvref_t<Callable>, MoveOnlyFunction> && std::invocable<Callable&>)
	MoveOnlyFunction(Callable&& callable) :
	    target(std::make_unique<Model<std::decay_t<Callable>>>(std::forward<Callable>(callable)))
	{}

	MoveOnlyFunction(MoveOnlyFunction&&) noexcept = default;
	MoveOnlyFunction& operator=(MoveOnlyFunction&&) noexcept = default;
	MoveOnlyFunction(const MoveOnlyFunction&) = delete;
	MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;

	explicit operator bool() const noexcept { return static_cast<bool>(target); }

	void operator()()
	{
		if (!target) {
			throw std::bad_function_call();
		}
		target->invoke();
	}

private:
	struct Interface
	{
		virtual ~Interface() = default;
		virtual void invoke() = 0;
	};

	template <typename Callable>
	struct Model final : Interface
	{
		template <typename Value>
		explicit Model(Value&& value) : callable(std::forward<Value>(value))
		{}

		void invoke() override { std::invoke(callable); }

		Callable callable;
	};

	std::unique_ptr<Interface> target;
};
#endif

#endif // FS_MOVE_ONLY_FUNCTION_H
