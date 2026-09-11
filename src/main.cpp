#include "lte/simulator.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        std::uint64_t duration = 1000;
        std::filesystem::path output = "results";
        for (int arg = 1; arg < argc; ++arg) {
            const std::string flag = argv[arg];
            if (flag == "--help") {
                std::cout << "lte_simulator [--ticks 1..100000] [--output DIRECTORY]\n"; return 0;
            }
            if (arg + 1 >= argc) throw std::invalid_argument("Missing option value");
            const std::string value = argv[++arg];
            if (flag == "--output") { output = value; }
            else if (flag == "--ticks") {
                if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                    throw std::invalid_argument("Ticks must be a positive integer");
                duration = std::stoull(value);
                if (duration == 0 || duration > 100000) throw std::invalid_argument("Ticks must be in [1, 100000]");
            } else throw std::invalid_argument("Unknown option: " + flag);
        }
        std::filesystem::create_directories(output);
        std::ofstream summary(output / "summary.csv"), users(output / "users.csv");
        if (!summary || !users) throw std::runtime_error("Cannot open CSV output files");
        summary << "scenario,algorithm,ticks,throughput_mbps,jain_fairness,mean_delay_ms,completed_packets,dropped_bytes,queued_bytes,rb_utilization\n";
        users << "scenario,algorithm,user,admitted_bytes,sent_bytes,dropped_bytes,queued_bytes,completed_packets,mean_delay_ms\n";
        summary << std::fixed << std::setprecision(6);
        users << std::fixed << std::setprecision(6);
        for (const std::string scenario : {"equal", "varying_channel", "bursty"}) {
            for (auto algorithm : {lte::Algorithm::round_robin, lte::Algorithm::proportional_fair}) {
                lte::Simulator sim({}, algorithm);
                std::uint64_t allocated = 0;
                for (std::uint64_t tick = 0; tick < duration; ++tick) {
                    std::vector<int> cqi(4, 8);
                    for (std::size_t user = 0; user < 4; ++user) {
                        if (scenario != "equal") cqi[user] = (tick / 10 + user) % 4 == 0 ? 15 : 3;
                        const bool arrival = scenario != "bursty" || (tick + user * 5) % 20 == 0;
                        if (arrival) sim.enqueue(user, scenario == "bursty" ? 1800 : 600);
                    }
                    allocated += sim.tick(cqi).grants.size();
                }
                std::uint64_t sent = 0, dropped = 0, queued = 0, completed = 0, delay = 0;
                std::vector<double> rates;
                for (std::size_t user = 0; user < 4; ++user) {
                    const auto& stats = sim.statistics()[user];
                    sent += stats.sent_bytes; dropped += stats.dropped_bytes; queued += stats.queued_bytes;
                    completed += stats.completed_packets; delay += stats.total_delay_ms;
                    rates.push_back(lte::throughput_mbps(stats.sent_bytes, duration));
                    users << scenario << ',' << lte::name(algorithm) << ',' << user << ',' << stats.admitted_bytes
                          << ',' << stats.sent_bytes << ',' << stats.dropped_bytes << ',' << stats.queued_bytes
                          << ',' << stats.completed_packets << ',' << lte::mean_delay_ms(stats) << '\n';
                }
                const double rate = lte::throughput_mbps(sent, duration);
                const double fairness = lte::jain_fairness(rates);
                summary << scenario << ',' << lte::name(algorithm) << ',' << duration << ',' << rate << ',' << fairness
                        << ',' << (completed == 0 ? 0 : static_cast<double>(delay) / static_cast<double>(completed))
                        << ',' << completed << ',' << dropped << ',' << queued
                        << ',' << static_cast<double>(allocated) / (static_cast<double>(duration) * 12) << '\n';
                std::cout << scenario << " / " << lte::name(algorithm) << ": " << rate
                          << " Mbps, fairness=" << fairness << ", dropped=" << dropped << " bytes\n";
            }
        }
        summary.flush(); users.flush();
        if (!summary || !users) throw std::runtime_error("Failed to write CSV output");
        return 0;
    } catch (const std::exception& error) { std::cerr << "Error: " << error.what() << '\n'; return 1; }
}
