// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_STORE_NAME_VALIDATOR_H
#define FS_STORE_NAME_VALIDATOR_H

#include <string>
#include <string_view>

/// Reusable character name validation — used by Store name-change and
/// potentially by other systems that need name validation.
namespace CharacterNameValidator {

/// Maximum allowed character name length.
inline constexpr size_t MaxNameLength = 20;

/// Maximum number of words in a character name.
inline constexpr size_t MaxNameWords = 5;

/// Format a raw name string: trim, lowercase, capitalize each word.
[[nodiscard]] std::string formatName(std::string_view rawName);

/// Validate that a formatted name passes all rules.
/// Returns empty string on success, or a human-readable error message.
[[nodiscard]] std::string validate(std::string_view name);

/// Check if a character name already exists in the database (case-insensitive).
[[nodiscard]] bool nameExistsInDB(std::string_view name);

} // namespace CharacterNameValidator

#endif // FS_STORE_NAME_VALIDATOR_H
