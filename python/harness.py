"""
Process-orchestration harness for Minislot's integration tests.

Spawns the real cmts/cm binaries as separate OS processes (not an
in-process mock) and gives tests a simple way to wait for log lines and
tear everything down cleanly. This is deliberately black-box: tests only
ever look at stdout, exactly like a human reading terminal output, so the
harness exercises the actual wire protocol end to end rather than reaching
into C++ internals.
"""

from __future__ import annotations

import select
import socket
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path


def find_build_dir() -> Path:
    here = Path(__file__).resolve().parent
    candidate = here.parent / "build"
    if not (candidate / "cmts").exists():
        raise FileNotFoundError(
            f"Expected built binaries at {candidate}/cmts and {candidate}/cm - "
            "run cmake --build from the project root first."
        )
    return candidate


@dataclass
class ManagedProcess:
    name: str
    proc: subprocess.Popen
    lines: list[str] = field(default_factory=list)

    def _drain(self) -> None:
        # readline() on a pipe blocks until a full line OR EOF - it does
        # NOT return "" just because nothing is buffered right now. So we
        # must check with select() whether a line is actually ready before
        # calling readline(); otherwise, once a process goes quiet (e.g. the
        # cmts between MAP intervals), the first readline() call with
        # nothing pending blocks forever and hangs the whole suite, since
        # the process never closes its stdout. See BUGS.md (BUG-003).
        while True:
            ready, _, _ = select.select([self.proc.stdout], [], [], 0)
            if not ready:
                return
            line = self.proc.stdout.readline()
            if not line:
                return
            self.lines.append(line.rstrip("\n"))

    def wait_for_line(self, substring: str, timeout_s: float = 10.0) -> str:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            self._drain()
            for line in self.lines:
                if substring in line:
                    return line
            time.sleep(0.1)
        raise TimeoutError(f"[{self.name}] never produced a line containing {substring!r}. "
                            f"Captured so far:\n" + "\n".join(self.lines))

    def all_lines_containing(self, substring: str) -> list[str]:
        self._drain()
        return [l for l in self.lines if substring in l]


class MinislotCluster:
    """Owns one cmts process and zero or more cm processes; tears all of
    them down on exit() even if a test fails midway, so a crashed test
    never leaves orphan UDP listeners around for the next test to collide
    with."""

    def __init__(self, port: int):
        self.build_dir = find_build_dir()
        self.port = port
        self.cmts: ManagedProcess | None = None
        self.cms: list[ManagedProcess] = []

    def __enter__(self) -> "MinislotCluster":
        proc = subprocess.Popen(
            [str(self.build_dir / "cmts"), str(self.port)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1,
        )
        self.cmts = ManagedProcess("cmts", proc)
        self.cmts.wait_for_line("listening on UDP port")
        return self

    def spawn_cm(self, label: str, requested_bytes_per_sec: int = 1_000_000) -> ManagedProcess:
        proc = subprocess.Popen(
            [str(self.build_dir / "cm"), "127.0.0.1", str(self.port), label, str(requested_bytes_per_sec)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1,
        )
        managed = ManagedProcess(label, proc)
        self.cms.append(managed)
        return managed

    def send_raw_udp(self, payload: bytes) -> None:
        """Sends arbitrary bytes straight at the CMTS's port - used to
        inject malformed/garbage packets a real misbehaving or malicious
        peer might send, bypassing the well-formed C++ client entirely."""
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.sendto(payload, ("127.0.0.1", self.port))

    def __exit__(self, *exc_info) -> None:
        for managed in self.cms:
            managed.proc.terminate()
        if self.cmts:
            self.cmts.proc.terminate()
        for managed in self.cms:
            try:
                managed.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                managed.proc.kill()
        if self.cmts:
            try:
                self.cmts.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.cmts.proc.kill()
