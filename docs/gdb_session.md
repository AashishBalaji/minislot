# Debugging session: inspecting a live ranging exchange with gdb

This is a real transcript (not staged) of attaching gdb to the `cmts` process
and breaking inside the MAC-layer ranging handler while a real `cm` process
was ranging against it over a live UDP socket. Captured with:

```
gdb -batch \
  -ex "break minislot::CmtsScheduler::handleRangingRequest" \
  -ex "run 9400" \
  -ex "bt" \
  -ex "print req.cmId" \
  -ex "print req.type" \
  -ex "print from.host" \
  -ex "print from.port" \
  -ex "continue" \
  --args ./cmts 9400
```

while a `./cm 127.0.0.1 9400 cm-dbg` was started concurrently to actually
trigger the breakpoint by sending a real `RNG-REQ`.

## Transcript

```
Breakpoint 1 at 0x6ee0: file /home/claude/minislot/src/CmtsScheduler.cpp, line 55.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".
[cmts] listening on UDP port 9400
[New Thread 0x7ffff77ff6c0 (LWP 891)]
[New Thread 0x7ffff6ffe6c0 (LWP 892)]
[New Thread 0x7ffff67fd6c0 (LWP 893)]
[Switching to Thread 0x7ffff6ffe6c0 (LWP 892)]

Thread 3 "cmts" hit Breakpoint 1, minislot::CmtsScheduler::handleRangingRequest (this=0x7fffffff64c0, req=..., from=...) at /home/claude/minislot/src/CmtsScheduler.cpp:55
55	MacFrame CmtsScheduler::handleRangingRequest(const MacFrame& req, const Endpoint& from) {
#0  minislot::CmtsScheduler::handleRangingRequest (this=0x7fffffff64c0, req=..., from=...) at /home/claude/minislot/src/CmtsScheduler.cpp:55
#1  0x0000555555557bec in operator() (__closure=0x555555574688) at /home/claude/minislot/src/cmts_main.cpp:70
#2  0x00007ffff7cecdb4 in ?? () from /lib/x86_64-linux-gnu/libstdc++.so.6
#3  0x00007ffff789cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#4  0x00007ffff7929ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78
$1 = 0
$2 = minislot::MessageType::RngReq
$3 = "127.0.0.1"
$4 = 47005
[cmts] RNG-REQ from 127.0.0.1:47005 -> RNG-RSP(Continue)

Thread 3 "cmts" hit Breakpoint 1, minislot::CmtsScheduler::handleRangingRequest (this=0x7fffffff64c0, req=..., from=...) at /home/claude/minislot/src/CmtsScheduler.cpp:55
55	MacFrame CmtsScheduler::handleRangingRequest(const MacFrame& req, const Endpoint& from) {
```

## What this shows

- The breakpoint is hit on **Thread 3**, the CMTS's worker thread
  (`workerLoop` in `cmts_main.cpp`) — not the receive thread or the MAP
  thread — confirming the 3-thread split (receive / dispatch / periodic MAP)
  is real and each socket read is handed off to a worker thread rather than
  processed inline.
- `req.cmId == 0` and `req.type == RngReq` is exactly the frame a CM sends
  before it has been assigned a real `cmId` by the CMTS — the field is only
  meaningful after registration.
- `from.host` / `from.port` are the real peer address/port the kernel
  delivered via `recvfrom`, matching the ephemeral port the `cm` process
  logged itself (`local port 47005`).
- After `continue`, the handler actually runs, the CMTS logs
  `RNG-RSP(Continue)` (round 1 of the 2-round ranging requirement), and the
  breakpoint is hit a second time moments later when the `cm` retries —
  demonstrating `CmtsScheduler::kRangingRoundsBeforeSuccess = 2` live against
  a real retrying peer, not just in the unit test.
