# Findings log

Honest record of real problems found while building and exercising this
project — not retrofitted after the fact. Both were hit while running the
actual multi-process system over real sockets, not in a unit test.

## BUG-001: `CmtsScheduler.hpp` used `Endpoint` without including its definition

**Symptom:** `cmake --build` failed across `CmtsScheduler.hpp` and
`CmtsScheduler.cpp` with `'Endpoint' does not name a type` on every member
that touched a CM's address (`RegisteredCm::endpoint`,
`handleRangingRequest(..., const Endpoint&)`, etc).

**Root cause:** `Endpoint` is defined in `UdpSocket.hpp`. `CmtsScheduler.hpp`
uses it throughout but never included that header — it had compiled by
accident in an earlier draft only because another translation unit happened
to include `UdpSocket.hpp` first and the compiler's header cache papered
over the missing include. A clean build exposed it immediately.

**Fix:** added `#include "minislot/UdpSocket.hpp"` to
`include/minislot/CmtsScheduler.hpp`. Lesson: each header should include
what it directly uses, not rely on include order from whoever happens to
`#include` it first.

## BUG-002: CMTS/CM logs were silently empty under redirection

**Symptom:** Running `./cmts 9000 > cmts.log &` and a few `cm` clients and
then `cat cmts.log` showed nothing, even minutes after the processes were
clearly exchanging ranging/registration traffic (confirmed via a packet
capture and the processes' live memory state). The log files stayed empty
until the processes were killed, at which point the *entire* run's output
appeared all at once.

**Root cause:** `stdout` is fully block-buffered by the C library whenever
it isn't connected to a terminal — which is exactly the case once you
redirect it to a file, as any real deployment or test harness does. Every
`printf`/`std::cout` call was queued in a large libc buffer that only
flushes on a full buffer or process exit, not on newline. For a long-running
server this means "tail -f the log" shows nothing until the process dies.

**Fix:** added `std::setvbuf(stdout, nullptr, _IOLBF, 0);` at the top of
`main()` in both `cmts_main.cpp` and `cm_main.cpp`, forcing line-buffered
output regardless of whether stdout is a TTY or a file. This is the same
class of bug that bites real DOCSIS head-end software whenever its logs are
piped into a log collector instead of a terminal — worth getting right here
rather than discovering it in front of a customer.

## BUG-003: the Python test harness's `_drain()` could hang the entire suite

**Symptom:** `python3 -m pytest test_integration.py -v` would run the first
bit of setup and then hang indefinitely — no output, no timeout, no crash,
just stuck. It reproduced reliably, not intermittently.

**Root cause:** `ManagedProcess._drain()` called
`self.proc.stdout.readline()` in a `while True:` loop, exiting only when
`readline()` returned an empty string. But `readline()` on a live pipe only
returns `""` on **EOF** — while the process is alive and simply hasn't
produced a new line *yet*, `readline()` blocks and waits for one. Since the
real `cmts` process keeps running (and therefore never closes its stdout),
the moment `_drain()` had consumed every line already flushed and the CMTS
went quiet for even a few hundred milliseconds between log lines, the next
`readline()` call blocked forever — not until the next line arrived
eventually, but genuinely forever in the test's threads, because
`wait_for_line`'s polling loop was itself stuck inside that call and never
got the chance to time out. Confirmed with `ps aux`: the `cmts` and
`python3 -m pytest` processes were both alive and idle, not crashed, not
looping - just blocked in a blocking read.

**Fix:** `_drain()` now checks `select.select([self.proc.stdout], [], [],
0)` before each `readline()` call, and returns immediately if no data is
actually ready to be read, instead of trusting `readline()` to tell it that.
`wait_for_line`'s existing poll-and-sleep loop then works as originally
intended. Re-ran the full suite after the fix: all 6 integration tests pass
in ~19s (previously: hung indefinitely, confirmed by `ps aux` showing both
processes alive-but-stuck rather than crashed).

## Not a bug, but worth noting: ranging takes exactly 2 rounds by design

`CmtsScheduler::kRangingRoundsBeforeSuccess = 2` means a CM's first `RNG-REQ`
always gets `RNG-RSP(Continue)`, never `Success`, even though nothing is
actually wrong — this mirrors how real DOCSIS ranging uses the first
exchange to let the CMTS measure timing/power offset before committing. It's
intentional, but it's the kind of thing that looks like a bug the first time
you watch the log (see `docs/gdb_session.md`, where the breakpoint is hit
twice for exactly this reason) if you don't know the protocol is designed
that way.
