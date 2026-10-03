"""Check the complete mocked CLI's local durability ordering and error paths."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
results = []
for length in (0, 24, 65555):
    content = bytes((i * 73) % 256 for i in range(length))
    for blocks in (0, 1, 3):
        for failure in range(5):
            with tempfile.TemporaryDirectory(prefix="tarsnap-durability-") as work:
                root = Path(work)
                old = root / "old"
                new = root / "new"
                old.mkdir()
                new.mkdir()
                (old / "directory").write_bytes(content)
                trace = root / "trace"
                env = dict(os.environ, TEST_FAILURE=str(failure),
                           TEST_BLOCKS=str(blocks), TEST_TRACE=str(trace))
                completed = subprocess.run(
                    [str(binary), "--oldkey", "fixture-old", "--newkey", "fixture-new",
                     "--oldcachedir", str(old), "--newcachedir", str(new)],
                    env=env, capture_output=True, text=True, timeout=10)
                events = trace.read_text().splitlines() if trace.exists() else []
                expected = {
                    0: ["FILE_SYNC", "CLOSE_NEW", "CLOSE_OLD", "DIR_SYNC"] +
                       ["DELETE"] * blocks + ["COMMIT"],
                    1: ["FILE_SYNC_FAIL"],
                    2: ["FILE_SYNC", "CLOSE_NEW", "CLOSE_OLD", "DIR_SYNC_FAIL"],
                    3: ["FILE_SYNC", "CLOSE_NEW"],
                    4: ["FILE_SYNC", "CLOSE_NEW", "CLOSE_OLD"],
                }[failure]
                assert completed.returncode == (0 if failure == 0 else 1), (
                    length, blocks, failure, completed.returncode, completed.stderr, events)
                assert events == expected, (length, blocks, failure, events, expected)
                assert (old / "directory").exists() == (failure != 0)
                if failure == 0:
                    assert (new / "directory").read_bytes() == content
                results.append({"bytes": length, "blocks": blocks, "failure": failure,
                                "exit": completed.returncode, "events": events})
print(json.dumps({"passed": len(results), "cases": results}, indent=2))
