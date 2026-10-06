// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in the LICENSE file.
#ifndef FS_TRANSACTION_RETRY_H
#define FS_TRANSACTION_RETRY_H

#include <cstdint>
#include <exception>

namespace tfs::database {

// The sole transaction retry policy. Preserve the original error before rollback;
// never retry permanent errors, failed rollback, or ambiguous connection failures.
template <typename Transaction, typename Callback, typename Retryable, typename Report>
bool runTransaction(const Callback& callback, const Retryable& retryable, const Report& report, uint8_t maxAttempts)
{
	for (uint8_t attempt = 1; attempt <= maxAttempts; ++attempt) {
		Transaction transaction;
		if (!transaction.begin()) return false;
		try {
			if (callback() && transaction.commit()) return true;
			const bool retry = retryable();
			if (!transaction.rollback() || !retry || attempt == maxAttempts) return false;
		} catch (const std::exception& e) {
			transaction.rollback();
			report(e.what());
			return false;
		} catch (...) {
			transaction.rollback();
			report("unknown exception");
			return false;
		}
	}
	return false;
}

} // namespace tfs::database
#endif
