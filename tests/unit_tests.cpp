#include "lte/simulator.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

using namespace lte;
void check(bool value) { if (!value) throw std::runtime_error("Check failed"); }
void near(double actual, double expected) { check(std::abs(actual - expected) < 1e-9); }
void invalid(const std::function<void()>& action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Expected invalid_argument");
}

int main(int argc, char** argv) {
    const std::vector<std::pair<std::string, std::function<void()>>> cases = {
        {"invalid_config", [] {
            for (Config c : {Config{0, 1, 100, .1}, Config{1, 0, 100, .1}, Config{1, 1, 0, .1},
                             Config{1, 1, 100, 0}, Config{1, 1, 100, 1.1}})
                invalid([&] { Simulator s(c, Algorithm::round_robin); });
            Config c; c.averaging_weight = std::numeric_limits<double>::quiet_NaN();
            invalid([&] { Simulator s(c, Algorithm::round_robin); });
            invalid([] { Simulator s({}, static_cast<Algorithm>(99)); });
        }},
        {"cqi_mapping", [] {
            check(Simulator::bytes_per_block(0) == 0); check(Simulator::bytes_per_block(15) == 150);
            for (int c = 1; c <= 15; ++c) check(Simulator::bytes_per_block(c) > Simulator::bytes_per_block(c - 1));
            invalid([] { Simulator::bytes_per_block(-1); }); invalid([] { Simulator::bytes_per_block(16); });
        }},
        {"empty_queues", [] {
            for (auto a : {Algorithm::round_robin, Algorithm::proportional_fair}) {
                Simulator s({2, 2, 100, .1}, a); check(s.tick({15, 15}).grants.empty()); check(s.ticks() == 1);
            }
        }},
        {"queue_capacity", [] {
            Simulator s({1, 1, 100, .1}, Algorithm::round_robin);
            check(s.enqueue(0, 100)); check(!s.enqueue(0, 1));
            check(s.statistics()[0].queued_bytes == 100 && s.statistics()[0].dropped_bytes == 1);
            s.tick({10}); check(s.enqueue(0, 100));
        }},
        {"invalid_enqueue", [] {
            Simulator s({}, Algorithm::round_robin);
            invalid([&] { s.enqueue(4, 10); }); invalid([&] { s.enqueue(0, 0); });
            invalid([&] { s.enqueue(0, std::numeric_limits<std::uint64_t>::max()); });
            check(s.statistics()[0].admitted_bytes == 0);
        }},
        {"atomic_input_validation", [] {
            Simulator s({2, 2, 100, .1}, Algorithm::round_robin); s.enqueue(0, 100);
            invalid([&] { s.tick({15}); }); invalid([&] { s.tick({15, 16}); });
            check(s.ticks() == 0 && s.statistics()[0].sent_bytes == 0);
        }},
        {"outage_skipped", [] {
            for (auto a : {Algorithm::round_robin, Algorithm::proportional_fair}) {
                Simulator s({2, 2, 1000, .1}, a); s.enqueue(0, 100); s.enqueue(1, 100);
                auto t = s.tick({0, 10}); check(t.grants.size() == 1 && t.grants[0].user == 1);
                check(s.statistics()[0].queued_bytes == 100);
            }
        }},
        {"round_robin_rotation", [] {
            Simulator s({3, 1, 1000, .1}, Algorithm::round_robin);
            for (std::size_t u = 0; u < 3; ++u) s.enqueue(u, 1000);
            for (std::size_t t = 0; t < 9; ++t) {
                auto r = s.tick({1, 1, 1}); check(r.grants.size() == 1 && r.grants[0].user == t % 3);
            }
        }},
        {"pf_channel_preference", [] {
            Simulator s({2, 1, 1000, .1}, Algorithm::proportional_fair); s.enqueue(0, 1000); s.enqueue(1, 1000);
            auto r = s.tick({2, 15}); check(r.grants.size() == 1 && r.grants[0].user == 1 && r.bytes == 150);
        }},
        {"pf_history_changes_selection", [] {
            Simulator s({2, 1, 1000, .5}, Algorithm::proportional_fair); s.enqueue(0, 1000); s.enqueue(1, 1000);
            check(s.tick({10, 10}).grants[0].user == 0);
            check(s.tick({10, 10}).grants[0].user == 1);
        }},
        {"resource_budget", [] {
            for (auto a : {Algorithm::round_robin, Algorithm::proportional_fair}) {
                Simulator s({2, 3, 10000, .1}, a); s.enqueue(0, 10000); s.enqueue(1, 10000);
                auto r = s.tick({15, 15}); check(r.grants.size() == 3 && r.bytes == 450);
            }
        }},
        {"segmentation_and_latency", [] {
            Simulator s({1, 1, 1000, .1}, Algorithm::round_robin); s.enqueue(0, 150);
            s.tick({10}); check(s.statistics()[0].completed_packets == 0 && s.statistics()[0].queued_bytes == 50);
            s.tick({10}); check(s.statistics()[0].completed_packets == 1); near(mean_delay_ms(s.statistics()[0]), 2);
        }},
        {"fifo_latency", [] {
            Simulator s({1, 1, 1000, .1}, Algorithm::round_robin); s.enqueue(0, 150); s.enqueue(0, 50);
            s.tick({10}); check(s.statistics()[0].completed_packets == 0);
            s.tick({10}); check(s.statistics()[0].completed_packets == 2 && s.statistics()[0].total_delay_ms == 4);
        }},
        {"idle_ewma", [] {
            Simulator s({1, 1, 1000, .5}, Algorithm::proportional_fair);
            s.tick({0}); near(s.statistics()[0].average_bytes_per_tti, .5);
            s.enqueue(0, 100); s.tick({10}); near(s.statistics()[0].average_bytes_per_tti, 50.25);
        }},
        {"metric_units", [] {
            near(throughput_mbps(125000, 1000), 1); near(throughput_mbps(0, 0), 0);
            near(mean_delay_ms(UserStats{}), 0);
            near(jain_fairness({10, 10}), 1); near(jain_fairness({10, 0}), .5);
            near(jain_fairness({0, 0}), 0); near(jain_fairness({}), 0);
            invalid([] { jain_fairness({-1}); });
        }},
        {"equal_channels_fairness", [] {
            for (auto a : {Algorithm::round_robin, Algorithm::proportional_fair}) {
                Simulator s({4, 4, 100000, .05}, a);
                for (std::size_t u = 0; u < 4; ++u) s.enqueue(u, 100000);
                for (int t = 0; t < 100; ++t) s.tick({8, 8, 8, 8});
                for (const auto& stats : s.statistics()) check(stats.sent_bytes == 8000);
            }
        }},
        {"deterministic_replay", [] {
            Simulator a({}, Algorithm::proportional_fair), b({}, Algorithm::proportional_fair);
            for (int t = 0; t < 100; ++t) {
                for (std::size_t u = 0; u < 4; ++u) { a.enqueue(u, 400); b.enqueue(u, 400); }
                const std::vector<int> c{1 + t % 15, 8, 3, 15}; auto x = a.tick(c), y = b.tick(c);
                check(x.bytes == y.bytes && x.grants.size() == y.grants.size());
                for (std::size_t i = 0; i < x.grants.size(); ++i)
                    check(x.grants[i].user == y.grants[i].user && x.grants[i].bytes == y.grants[i].bytes);
            }
        }},
        {"randomized_conservation", [] {
            for (auto a : {Algorithm::round_robin, Algorithm::proportional_fair}) {
                Simulator s({4, 7, 1000, .05}, a); std::mt19937 rng(42);
                std::vector<std::uint64_t> offered(4, 0), received(4, 0);
                for (int t = 0; t < 1000; ++t) {
                    std::vector<int> cqi;
                    for (std::size_t u = 0; u < 4; ++u) {
                        const auto bytes = 1 + rng() % 400; offered[u] += bytes; s.enqueue(u, bytes);
                        cqi.push_back(static_cast<int>(rng() % 16));
                    }
                    auto r = s.tick(cqi); check(r.grants.size() <= 7);
                    std::uint64_t sum = 0;
                    for (const auto& grant : r.grants) {
                        check(grant.user < 4 && grant.bytes > 0 && grant.bytes <= Simulator::bytes_per_block(cqi[grant.user]));
                        sum += grant.bytes; received[grant.user] += grant.bytes;
                    }
                    check(sum == r.bytes);
                    for (std::size_t u = 0; u < 4; ++u) {
                        const auto& stats = s.statistics()[u];
                        check(stats.admitted_bytes + stats.dropped_bytes == offered[u]);
                        check(stats.sent_bytes == received[u]);
                        check(stats.admitted_bytes == stats.sent_bytes + stats.queued_bytes && stats.queued_bytes <= 1000);
                    }
                }
            }
        }}
    };
    if (argc != 2) { std::cerr << "Pass one test case name\n"; return 1; }
    for (const auto& [case_name, test] : cases) {
        if (case_name == argv[1]) {
            try { test(); std::cout << "PASS " << case_name << '\n'; return 0; }
            catch (const std::exception& error) { std::cerr << "FAIL " << case_name << ": " << error.what() << '\n'; return 1; }
        }
    }
    std::cerr << "Unknown test case\n"; return 1;
}
