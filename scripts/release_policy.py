"""The explicitly approved first preview is the only unsigned release exception."""

import argparse
import json
import os
from pathlib import Path


UNSIGNED_PREVIEW_TAG = "v2026.1.0-beta.1"


def policy(tag: str = "") -> dict:
    preview = tag == UNSIGNED_PREVIEW_TAG
    return {"tag": tag, "unsignedPreview": preview, "requiresSigning": bool(tag) and not preview}


def current_tag() -> str:
    if os.environ.get("GITHUB_REF_TYPE") != "tag":
        return ""
    tag = os.environ.get("GITHUB_REF_NAME", "")
    if not tag:
        raise ValueError("A tag build requires GITHUB_REF_NAME")
    return tag


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", default=None)
    parser.add_argument("--github-output", type=Path)
    args = parser.parse_args()
    result = policy(current_tag() if args.tag is None else args.tag)
    if args.github_output:
        with args.github_output.open("a", encoding="utf-8") as output:
            output.write(f"requires_signing={str(result['requiresSigning']).lower()}\n")
            output.write(f"unsigned_preview={str(result['unsignedPreview']).lower()}\n")
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
