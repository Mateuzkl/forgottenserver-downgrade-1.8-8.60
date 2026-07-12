// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_METRICS_H
#define FS_METRICS_H

#include <cstdint>
#include <memory>
#include <string>

struct MetricsOptions
{
	bool enablePrometheus = false;
	std::string prometheusAddress = "0.0.0.0:9464";
	bool enableOstream = false;
	int64_t ostreamIntervalMilliseconds = 1000;
};

// OpenTelemetry metrics bridge. Only observable (pull) instruments are
// registered: every series reads the pre-aggregated relaxed atomics kept by
// CreatureSchedulerMetrics / PerformanceMetrics / Dispatcher at collection
// time, so game hot paths never call into OpenTelemetry.
//
// Compiled to a stub unless the binary is built with -DENABLE_METRICS=ON
// (CMake) which defines METRICS_ENABLED and links opentelemetry-cpp.
class Metrics
{
public:
	Metrics();
	~Metrics();

	Metrics(const Metrics&) = delete;
	Metrics& operator=(const Metrics&) = delete;

	void init(const MetricsOptions& options);
	void shutdown();

	[[nodiscard]] bool isEnabled() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

extern Metrics g_metrics;

#endif // FS_METRICS_H
