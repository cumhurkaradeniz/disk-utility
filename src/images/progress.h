// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>

namespace images {

// A copy's speed over its last few seconds, and the time it has left at
// that speed.
class RateMeter
{
public:
    using Clock = std::chrono::steady_clock;

    void start(Clock::time_point now);
    void add(Clock::time_point now, uint64_t copied);

    // Bytes a second; 0 until a second has passed.
    double rate() const;
    // None while the rate is unknown.
    std::optional<std::chrono::seconds> remaining(uint64_t total) const;

private:
    std::deque<std::pair<Clock::time_point, uint64_t>> m_samples;
};

// "1.2 GB of 4 GB".
std::string amountText(uint64_t copied, uint64_t total);
// "35.1 MB/s"; empty for 0.
std::string rateText(double bytesPerSecond);
// "About 5 seconds left", "About 2 minutes left", "About 3 hours left".
std::string timeLeftText(std::chrono::seconds remaining);
// "1.2 GB of 4 GB (35.1 MB/s)", or without the rate while it is unknown.
std::string progressText(uint64_t copied, uint64_t total, double bytesPerSecond);

} // namespace images
