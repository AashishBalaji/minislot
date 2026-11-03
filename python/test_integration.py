"""
Black-box integration tests: launch the real cmts/cm binaries as separate
processes communicating over real UDP sockets, and assert on what comes
out of their stdout - the same evidence a human watching terminals would
use. Run with: pytest python/test_integration.py -v
(from the project root, after building: cmake --build build)
"""

import re
import time

import pytest

from harness import MinislotCluster

PORT_BASE = 9300  # offset per test to avoid port collisions between tests


def test_single_cm_reaches_operational():
    with MinislotCluster(PORT_BASE + 1) as cluster:
        cm = cluster.spawn_cm("cm-solo")
        cm.wait_for_line("state -> Operational", timeout_s=10)


def test_three_cms_all_register_with_distinct_ids():
    with MinislotCluster(PORT_BASE + 2) as cluster:
        cms = [cluster.spawn_cm(f"cm-{i}") for i in range(3)]
        ids = set()
        for cm in cms:
            line = cm.wait_for_line("state -> Operational", timeout_s=10)
            match = re.search(r"cmId=(\d+)", line)
            assert match, f"couldn't parse cmId out of: {line}"
            ids.add(match.group(1))
        assert len(ids) == 3, f"expected 3 distinct cmIds, got {ids}"


def test_map_grants_are_proportional_to_requested_bandwidth():
    with MinislotCluster(PORT_BASE + 3) as cluster:
        low = cluster.spawn_cm("cm-low", requested_bytes_per_sec=100_000)
        high = cluster.spawn_cm("cm-high", requested_bytes_per_sec=900_000)
        low.wait_for_line("state -> Operational", timeout_s=10)
        high.wait_for_line("state -> Operational", timeout_s=10)

        # Let a couple of real MAP intervals land (CMTS ticks every 500ms).
        time.sleep(2)

        low_line = low.wait_for_line("got grant", timeout_s=5)
        high_line = high.wait_for_line("got grant", timeout_s=5)
        low_slots = int(re.search(r"(\d+) slots", low_line).group(1))
        high_slots = int(re.search(r"(\d+) slots", high_line).group(1))

        assert high_slots > low_slots, (
            f"CM requesting 9x more bandwidth should get more slots: "
            f"low={low_slots} high={high_slots}"
        )
        # Roughly proportional (9:1 requested -> allow slack for the fairness floor)
        assert high_slots / max(low_slots, 1) > 3


def test_lowest_requester_still_gets_a_nonzero_grant():
    # Same property the unit test EvenTheLowestRequesterGetsAFloorGrant
    # checks, but proven here against the real running binaries, not just
    # the library function in isolation.
    with MinislotCluster(PORT_BASE + 4) as cluster:
        tiny = cluster.spawn_cm("cm-tiny", requested_bytes_per_sec=1)
        huge = cluster.spawn_cm("cm-huge", requested_bytes_per_sec=1_000_000)
        tiny.wait_for_line("state -> Operational", timeout_s=10)
        huge.wait_for_line("state -> Operational", timeout_s=10)

        time.sleep(2)
        tiny_line = tiny.wait_for_line("got grant", timeout_s=5)
        tiny_slots = int(re.search(r"(\d+) slots", tiny_line).group(1))
        assert tiny_slots > 0


def test_malformed_udp_packet_does_not_crash_the_cmts():
    with MinislotCluster(PORT_BASE + 5) as cluster:
        # Garbage shorter than any valid MacFrame, and a second payload
        # with a type byte outside the valid enum range.
        cluster.send_raw_udp(b"\x00\x01\x02")
        cluster.send_raw_udp(bytes([250]) + b"\xff" * 40)
        time.sleep(0.5)

        assert cluster.cmts.proc.poll() is None, "CMTS process died after receiving malformed UDP packets"

        # And it should still be fully functional afterwards for a real CM.
        cm = cluster.spawn_cm("cm-after-garbage")
        cm.wait_for_line("state -> Operational", timeout_s=10)


def test_cm_with_no_cmts_running_does_not_crash_it_just_waits():
    # No cluster/cmts started at all - the CM should sit in Ranging,
    # retrying, rather than crashing on a connection-refused-style error
    # (UDP has no connection to refuse, but this guards the retry loop).
    import subprocess
    from harness import find_build_dir

    build_dir = find_build_dir()
    proc = subprocess.Popen(
        [str(build_dir / "cm"), "127.0.0.1", "9399", "cm-lonely"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    try:
        time.sleep(2)
        assert proc.poll() is None, "CM process exited when it should be retrying ranging indefinitely"
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-v"]))
