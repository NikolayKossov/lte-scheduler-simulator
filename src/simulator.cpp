#include "lte/simulator.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lte {
const char* name(Algorithm algorithm) {
    switch (algorithm) {
        case Algorithm::round_robin: return "round_robin";
        case Algorithm::proportional_fair: return "proportional_fair";
    }
    throw std::invalid_argument("Unknown algorithm");
}

Simulator::Simulator(Config config, Algorithm algorithm) : config_(config), algorithm_(algorithm) {
    if (config.users == 0 || config.users > 1000 || config.resource_blocks == 0
        || config.resource_blocks > 1000 || config.queue_capacity_bytes == 0
        || config.queue_capacity_bytes > 1000000000ULL || !std::isfinite(config.averaging_weight)
        || config.averaging_weight <= 0 || config.averaging_weight > 1)
        throw std::invalid_argument("Invalid simulator configuration");
    (void)name(algorithm);
    queues_.resize(config.users);
    stats_.resize(config.users);
}

std::uint64_t Simulator::bytes_per_block(int cqi) {
    if (cqi < 0 || cqi > 15) throw std::invalid_argument("CQI must be in [0, 15]");
    return static_cast<std::uint64_t>(cqi) * 10;
}

bool Simulator::enqueue(std::size_t user, std::uint64_t bytes) {
    if (user >= config_.users || bytes == 0 || bytes > 1000000000ULL)
        throw std::invalid_argument("Invalid user or packet size");
    auto& stats = stats_[user];
    if (bytes > config_.queue_capacity_bytes - stats.queued_bytes) {
        stats.dropped_bytes += bytes;
        return false;
    }
    queues_[user].push_back({bytes, ticks_});
    stats.admitted_bytes += bytes;
    stats.queued_bytes += bytes;
    return true;
}

std::size_t Simulator::select(const std::vector<int>& cqi) const {
    std::size_t selected = config_.users;
    double best = -1;
    for (std::size_t offset = 0; offset < config_.users; ++offset) {
        const auto user = (cursor_ + offset) % config_.users;
        if (stats_[user].queued_bytes == 0 || cqi[user] == 0) continue;
        if (algorithm_ == Algorithm::round_robin) return user;
        // History is frozen during the TTI and updated after all RB grants.
        const double score = static_cast<double>(bytes_per_block(cqi[user]))
                           / std::max(stats_[user].average_bytes_per_tti, 1.0);
        if (score > best) { best = score; selected = user; }
    }
    return selected;
}

void Simulator::send(std::size_t user, std::uint64_t bytes) {
    auto& stats = stats_[user];
    stats.sent_bytes += bytes;
    stats.queued_bytes -= bytes;
    while (bytes > 0) {
        auto& packet = queues_[user].front();
        const auto chunk = std::min(bytes, packet.remaining);
        packet.remaining -= chunk;
        bytes -= chunk;
        if (packet.remaining == 0) {
            ++stats.completed_packets;
            stats.total_delay_ms += ticks_ - packet.arrival_tick + 1;
            queues_[user].pop_front();
        }
    }
}

TickResult Simulator::tick(const std::vector<int>& cqi) {
    if (cqi.size() != config_.users) throw std::invalid_argument("One CQI per user is required");
    for (int quality : cqi) (void)bytes_per_block(quality); // Validate before mutation.
    if (ticks_ == 1000000) throw std::length_error("Simulation limit: 1,000,000 TTIs");
    TickResult result;
    std::vector<std::uint64_t> delivered(config_.users, 0);
    for (std::size_t rb = 0; rb < config_.resource_blocks; ++rb) {
        const auto user = select(cqi);
        if (user == config_.users) break;
        const auto bytes = std::min(bytes_per_block(cqi[user]), stats_[user].queued_bytes);
        result.grants.push_back({user, bytes});
        result.bytes += bytes;
        delivered[user] += bytes;
        send(user, bytes);
        cursor_ = (user + 1) % config_.users;
    }
    for (std::size_t user = 0; user < config_.users; ++user)
        stats_[user].average_bytes_per_tti =
            (1 - config_.averaging_weight) * stats_[user].average_bytes_per_tti
            + config_.averaging_weight * static_cast<double>(delivered[user]);
    ++ticks_;
    return result;
}

double throughput_mbps(std::uint64_t bytes, std::uint64_t ticks) {
    return ticks == 0 ? 0 : static_cast<double>(bytes) * 0.008 / static_cast<double>(ticks);
}

double mean_delay_ms(const UserStats& stats) {
    return stats.completed_packets == 0 ? 0
        : static_cast<double>(stats.total_delay_ms) / static_cast<double>(stats.completed_packets);
}

double jain_fairness(const std::vector<double>& values) {
    double maximum = 0;
    for (double value : values) {
        if (!std::isfinite(value) || value < 0) throw std::invalid_argument("Invalid fairness input");
        maximum = std::max(maximum, value);
    }
    // No traffic: fairness is undefined mathematically; the reporting convention is 0.
    if (maximum == 0) return 0;
    double sum = 0, squares = 0;
    for (double value : values) { const double normalized = value / maximum; sum += normalized; squares += normalized * normalized; }
    return sum * sum / (static_cast<double>(values.size()) * squares);
}
}
