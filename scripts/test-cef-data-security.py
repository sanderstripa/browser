"""Real Windows DPAPI/SQLite/vault tests, isolated from the user's data."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    output = pathlib.Path(sys.argv[2]).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="soulu-data-test-") as directory:
        env = os.environ.copy()
        env.update(LOCALAPPDATA=directory, APPDATA=directory,
                   SOULU_DATA_SECURITY_TEST_ROOT=directory, SOULU_UI_TEST_PORT="9223")
        result = subprocess.run([str(executable), f"--data-security-test-report={output}"],
                                env=env, timeout=120)
        if output.exists():
            report = json.loads(output.read_text())
            print(json.dumps(report))
            assert report.get("passed"), "Native data security check failed"
        else:
            raise AssertionError("Native test did not produce a report")
        assert result.returncode == 0, "Native test process failed"


if __name__ == "__main__":
    main()
