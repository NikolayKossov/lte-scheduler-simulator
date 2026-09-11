# LTE Scheduler Simulator

[![CI](https://github.com/NikolayKossov/lte-scheduler-simulator/actions/workflows/ci.yml/badge.svg)](https://github.com/NikolayKossov/lte-scheduler-simulator/actions/workflows/ci.yml)

**C++17 · Round Robin · Proportional Fair · CMake/CTest · GCC/Clang · Sanitizers**

An educational, single-cell downlink scheduling simulator inspired by LTE concepts.
It compares how two policies share a limited resource budget among users with
different traffic loads and changing channel quality.

**This is not an LTE stack, a standards-compliant radio simulator or an Ericsson
OneRAN implementation.** It demonstrates scheduling algorithms, software design,
testing and measurement within a documented simplified model.

## What it does

- Per-user FIFO packet queues with byte-capacity limits and drop-tail admission.
- Three reproducible scenarios, four users and two algorithms: **six experiments**.
- Partial packet service across resource grants and scheduling intervals.
- Throughput, Jain's throughput fairness, packet completion delay, drops, backlog
  and resource-block utilization exported to CSV.
- **18 unit cases and one CLI integration test**, registered individually in CTest.
- Tests include 1,000 seeded TTIs for each algorithm and independent byte-accounting checks.
- GitHub Actions builds and tests with GCC and Clang, warnings as errors,
  AddressSanitizer and UndefinedBehaviorSanitizer.

## Model and assumptions

| Concept | Representation |
| --- | --- |
| Base station | One abstract scheduler, no real network traffic |
| User equipment (UE) | A queue and channel-quality value per user |
| Scheduling interval | One tick = 1 ms |
| Resource budget | 12 abstract RB grants per tick in the CLI scenarios |
| CQI | Integer 0–15; zero means outage |
| Payload per grant | **CQI × 10 bytes**, a deliberately synthetic monotonic mapping |
| Queue | 30,000 bytes per user by default; whole arriving packet dropped when full |
| Packet service | FIFO; segmentation across grants allowed |
| Delay | Completion tick end minus arrival tick start; minimum 1 ms |

The payload mapping does **not** implement the 3GPP CQI/MCS/TBS tables. Channel
quality is constant across all RBs for a user within one TTI. There is no PHY,
interference model, HARQ, RLC/PDCP, signalling, handover, QoS bearer model or core network.
Resource grants are abstract; contiguous allocation and protocol overhead are omitted.

## Algorithms

**Round Robin:** allocate one RB to the next eligible UE, preserving the cursor
between ticks. Empty queues and CQI=0 users are skipped. This targets fair
resource opportunities, which need not produce equal throughput on unequal channels.

**Proportional Fair:** select the eligible user with maximum
`bytes_per_RB(CQI) / max(historical_average_bytes_per_TTI, 1)`.
History is frozen within a TTI and updated afterwards for every user:

```text
average_next = (1 - alpha) * average_previous + alpha * delivered_bytes_this_TTI
alpha = 0.05; initial average = 1
```

The rotating cursor breaks exact score ties. Because frequency-selective channels
are omitted, a user can receive multiple or all RBs in one tick. The denominator
floor prevents division by zero after long idle periods. This is a simplified PF
policy, not a reproduction of a vendor scheduler.

Conceptual references: [ns-3 LTE scheduling design](https://www2.nsnam.org/docs/models/html/lte-design.html)
and [ns-3 LTE scheduler testing](https://www.nsnam.org/docs/models/html/lte-testing.html).
This repository has no ns-3 dependency.

## Build and run

Requires C++17, CMake 3.16+ and Python 3 (standard library only, for integration tests).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/lte_simulator --ticks 1000 --output results
```

For sanitizer checks on GCC/Clang, configure with `-DENABLE_SANITIZERS=ON`.
For a Windows multi-configuration generator, use `--config Debug` when building,
`-C Debug` with CTest, and `build/Debug/lte_simulator.exe` to run.
Windows builds are not part of the current CI matrix.

CLI options: `--ticks 1..100000`, `--output DIRECTORY`, `--help`.
The CLI runs both algorithms on all three scenarios. It overwrites `summary.csv`
and `users.csv` inside the chosen directory. Invalid options return a non-zero exit.

## Reproducible scenarios

| Scenario | Arrivals | Channels |
| --- | --- | --- |
| `equal` | 600 bytes/user/tick | CQI=8 for all users |
| `varying_channel` | 600 bytes/user/tick | One user at CQI=15, others at 3; best user rotates every 10 ms |
| `bursty` | 1,800-byte packet/user every 20 ms, staggered | Same rotating channel trace |

Both policies receive identical arrivals and CQI traces. The duration includes
startup transients; queues are not drained after the measurement window.
The equal-channel scenario has a hand-calculated aggregate capacity of
`12 × 80 × 8 / 0.001 = 7.68 Mbps`; tests verify that baseline and fairness=1.
The varying-channel trace deliberately demonstrates the opportunity to exploit
good channels. It is not evidence that PF always outperforms RR.

## Metrics and results

Verified [CI run](https://github.com/NikolayKossov/lte-scheduler-simulator/actions/runs/34618758129):
**19/19 tests passed on both GCC and Clang**, with sanitizers enabled.
Measured for the default 1,000-TTI experiments (one simulated second):

| Scenario | Policy | Mbps | Jain fairness | Mean completed-packet delay (ms) | Dropped bytes |
| --- | --- | ---: | ---: | ---: | ---: |
| Equal | RR | 7.6800 | 1.000000 | 112.39 | 1,322,400 |
| Equal | PF | 7.6800 | 1.000000 | 112.39 | 1,322,400 |
| Varying channel | RR | 5.7600 | 1.000000 | 147.40 | 1,562,400 |
| Varying channel | PF | 14.0352 | 0.999857 | 54.30 | 546,600 |
| Bursty | RR | 2.8800 | 1.000000 | 4.00 | 0 |
| Bursty | PF | 2.8800 | 1.000000 | 4.00 | 0 |

The first two scenarios intentionally overload the queues. The rotating-channel
scenario favors opportunistic scheduling; these numbers are not LTE network
benchmarks or a general proof that PF is better. Equal offered traffic and
similar final throughputs also do not imply equal short-term waiting times.

- `summary.csv`: six rows, one per scenario/algorithm.
- `users.csv`: 24 rows with per-UE counters and mean delay.
- Throughput = transmitted payload bits / simulated duration, reported in Mbps.
- Fairness = `(sum throughput)^2 / (N * sum throughput^2)` across all configured users.
  All-zero throughput is reported as 0 by convention; mathematically it is undefined.
- Mean delay includes **completed packets only**; unfinished and dropped packets
  are excluded. Zero completions are reported as delay=0; inspect the completion count.
- RB utilization counts allocated grants, including partially used ones; it is not payload efficiency.

Download CSV and JUnit XML from **Actions → completed run → Artifacts**.
Artifacts are retained for 14 days. Compare delay together with drops and backlog:
a lower reported delay alone does not establish a better scheduler.

## Tests

The suite checks configuration, CQI bounds, empty queues, exact capacity, invalid
packet admission, validation before mutation, outages, RR rotation, PF channel
selection and history, per-TTI budgets, segmentation, FIFO delay, EWMA, metric
units, symmetric fairness, deterministic replay and randomized conservation.

CLI integration runs all six experiments twice, compares CSV bytes, checks
per-user accounting and aggregate throughput, validates the known symmetric
baseline and the varying-channel comparison, and rejects invalid CLI arguments.
Explicit assertions stay active in Release builds.

## Design and limitations

Scheduling takes O(R × U) per TTI for R grants and U users, plus work proportional
to completed packets. State is managed using STL containers and RAII, without raw
owning pointers. The simulator is single-threaded and bounded to 1,000,000 ticks
per instance. No real-time guarantee or measured CPU-performance claim is made.

Possible extensions: realistic transport-block tables, packet delay percentiles,
frequency-selective channels, QoS weights, a benchmark suite and deployment packaging.
OpenShift, distributed execution and production LTE interoperability are not implemented.

```text
include/lte/simulator.hpp   Public types and simulator interface
src/simulator.cpp          Queues, scheduling and metrics
src/main.cpp               Scenario runner and CSV output
tests/unit_tests.cpp       18 named unit cases
tests/check_cli.py         End-to-end executable/CSV verification
CMakeLists.txt             Build and test registration
.github/workflows/ci.yml   GCC/Clang CI and artifacts
```

Personal, AI-assisted portfolio project. Be prepared to explain assumptions,
change a scheduling policy and debug a failing test when discussing this work.
