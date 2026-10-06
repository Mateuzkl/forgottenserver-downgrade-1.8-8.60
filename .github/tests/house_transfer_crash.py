"""Exercise abrupt process exit and restart through the real house-transfer path."""
import os
import subprocess
import sys


def main():
    database = os.environ.get("TFS_HOUSE_TEST_DB", "")
    if not database.startswith("tfs_save_audit_house_"):
        raise SystemExit("Only an explicitly named disposable house-test database is allowed")
    executable = os.path.abspath(sys.argv[1])
    for prefix in ("", "trade-"):
        for phase, code in (("before", 73), ("after", 74)):
            crash = subprocess.run([executable, "--" + prefix + "crash-" + phase], timeout=60)
            if crash.returncode != code:
                raise SystemExit(f"{prefix}{phase}-commit crash: expected {code}, got {crash.returncode}")
            # This is a new process: no transfer state survives in RAM.
            subprocess.run([executable, "--" + prefix + "verify-" + phase], check=True, timeout=60)
    print("4 real process-crash/restart scenarios passed; house items and payment retained exactly once")


if __name__ == "__main__":
    main()
