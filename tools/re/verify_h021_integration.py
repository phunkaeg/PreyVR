"""Offline checks for the H-021 integration ownership reads. No process access."""
import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from verify_h021_static import verify
from verify_h005_skinning import DEFAULT_MODULE

if __name__ == "__main__":
    try:
        print(json.dumps(verify(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_MODULE,
                                "h021_integration_landmarks.json"), indent=2))
    except (OSError, ValueError) as error:
        print(json.dumps({"error": str(error), "runtime_tested": False}), file=sys.stderr)
        sys.exit(1)
