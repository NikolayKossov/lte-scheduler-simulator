#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace lte {
enum class Algorithm { round_robin, proportional_fair };
const char* name(Algorithm algorithm);

struct Config {
    std::size_t users = 4;
    std::size_t resource_blocks = 12;
    std::uint64_t queue_capacity_bytes = 30000;
    double averaging_weight = 0.05;
};

struct UserStats {
    std::uint64_t admitted_bytes = 0;
    std::uint64_t dropped_bytes = 0;
    std::uint64_t sent_bytes = 0;
    std::uint64_t queued_bytes = 0;
    std::uint64_t completed_packets = 0;
    std::uint64_t total_delay_ms = 0;
    double average_bytes_per_tti = 1.0;
};

struct Grant {
    std::size_t user;
    std::uint64_t bytes;
};

struct TickResult {
    std::vector<Grant> grants;
    std::uint64_t bytes = 0;
};

// Abstract downlink model. One tick = 1 ms; CQI mapping is illustrative only.
class Simulator {
public:
    Simulator(Config config, Algorithm algorithm);
    bool enqueue(std::size_t user, std::uint64_t bytes);
    TickResult tick(const std::vector<int>& cqi);
    const std::vector<UserStats>& statistics() const noexcept { return stats_; }
    std::uint64_t ticks() const noexcept { return ticks_; }
    static std::uint64_t bytes_per_block(int cqi);

private:
    struct Packet { std::uint64_t remaining; std::uint64_t arrival_tick; };
    Config config_;
    Algorithm algorithm_;
    std::vector<std::deque<Packet>> queues_;
    std::vector<UserStats> stats_;
    std::size_t cursor_ = 0;
    std::uint64_t ticks_ = 0;
    std::size_t select(const std::vector<int>& cqi) const;
    void send(std::size_t user, std::uint64_t bytes);
};

double jain_fairness(const std::vector<double>& values);
double throughput_mbps(std::uint64_t bytes, std::uint64_t ticks);
double mean_delay_ms(const UserStats& stats);
}
