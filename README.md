# Minislot

A small, real C++ MAC-layer protocol engine modeled on simplified-but-real
DOCSIS semantics: a CMTS (head-end) and CM (cable modem) exchange ranging,
registration, and periodic bandwidth-grant ("MAP") messages over genuine UDP
sockets, across real OS processes, with a proportional-fair scheduler
deciding who gets how many upstream mini-slots.

Built to exercise, for real: C/C++ MAC-layer protocol development,
multi-threading, OOD/design patterns, Linux debugging & perf tooling,
containerized/Kubernetes deployment, and Python test automation — not a toy
echo server with DOCSIS vocabulary bolted on.

## What's actually implemented

- **Wire protocol** (`Message.hpp/.cpp`): a binary `MacFrame` format with six
  message types — `RNG-REQ` / `RNG-RSP`, `REG-REQ` / `REG-RSP`, `MAP`,
  `SYNC` — serialized in network byte order, with bounds-checked
  deserialization that rejects truncated, malformed, or oversized input
  rather than trusting the wire.
- **CM state machine** (`CmStateMachine.hpp/.cpp`): `Init → Ranging →
  Registering → Operational` (or `Failed`), driven entirely by which
  responses actually arrive — not a scripted sequence.
- **CMTS scheduler** (`CmtsScheduler.hpp/.cpp`): tracks per-peer ranging
  rounds (DOCSIS-style — the first `RNG-REQ` always gets `Continue`, not
  `Success`, so the head-end gets a timing/power measurement round before
  committing), assigns unique CM IDs on registration, and allocates upstream
  mini-slots every MAP interval via a pluggable `AllocationStrategy`. The
  shipped strategy, `ProportionalFairStrategy`, splits slots proportionally
  to each CM's requested bandwidth while guaranteeing every registered CM a
  non-zero floor grant (no starvation).
- **Transport** (`UdpSocket.hpp/.cpp`): a small RAII wrapper over POSIX UDP
  sockets with a `select()`-based receive-with-timeout.
- **`cmts` / `cm` binaries**: real multi-threaded (CMTS: receive / dispatch /
  periodic-MAP threads, coordinated through a mutex-guarded
  `ThreadSafeQueue`) and single-threaded (CM) processes that talk to each
  other over actual sockets — not mocked.

## Proof it actually works

- **22 GoogleTest unit tests** across the message format, state machine, and
  scheduler (`test/`), all passing, 0 memory leaks/errors under valgrind
  (`docs/valgrind_report.txt`).
- **A real interactive gdb session** (`docs/gdb_session.md`) breaking inside
  the live scheduler mid-exchange and inspecting the actual request/peer
  data off a genuine UDP packet.
- **Live fairness measurement**, not just a unit assertion: with one CM
  requesting 900,000 B/s and another requesting 100,000 B/s against a real
  running `cmts`, the scheduler handed out 898 and 101 of 1000 total slots
  respectively — a ~9:1 split matching the ~9:1 requested-bandwidth ratio,
  measured off live MAP broadcasts.
- **Black-box integration tests in Python** (`python/test_integration.py`)
  that spawn the real `cmts`/`cm` binaries as OS subprocesses — no mocking —
  and assert on their actual stdout/behavior, including a deliberately
  malformed-UDP-packet test that confirms the CMTS doesn't crash on garbage
  input.
- **Two real bugs found and fixed** while running this, not staged —
  see `BUGS.md`.

## Building

```
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo .
cmake --build build -j
```

Produces `build/cmts`, `build/cm`, and (if GoogleTest is installed)
`build/minislot_tests`.

## Running it by hand

```
./build/cmts 9000 &
./build/cm 127.0.0.1 9000 cm-a 900000   # requests 900,000 B/s
./build/cm 127.0.0.1 9000 cm-b 100000   # requests 100,000 B/s
```

Watch each CM log its state transitions (`Ranging → Registering →
Operational`) and the slot counts it's granted in each `MAP`.

## Unit tests

```
cd build && ctest
# or directly:
./build/minislot_tests
```

## Integration tests (real subprocesses)

```
pip install -r python/requirements.txt
cd python && python3 -m pytest test_integration.py -v
```

## Containers / Kubernetes

```
docker build -f Dockerfile.cmts -t minislot-cmts:latest .
docker build -f Dockerfile.cm   -t minislot-cm:latest   .
kubectl apply -f k8s/
```

`k8s/cm-deployment.yaml` runs 5 CM replicas against the one `cmts` Service —
bump `replicas` to load-test the scheduler against more peers.

## Layout

```
include/minislot/   headers: Message, UdpSocket, ThreadSafeQueue,
                     CmStateMachine, CmtsScheduler
src/                 implementations + cmts_main.cpp / cm_main.cpp
test/                GoogleTest unit tests
python/              black-box integration test harness (real subprocesses)
docs/                valgrind report, gdb debugging session transcript
k8s/, Dockerfile.*   containerization
BUGS.md              real issues found while building/running this
```
