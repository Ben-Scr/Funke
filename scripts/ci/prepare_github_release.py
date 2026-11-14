"""Create or refresh an unpublished release from the verified tag-run artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import release_policy


def digest(path: Path) -> str:
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(chunk)
    return checksum.hexdigest()


def payload_names(version: str) -> set[str]:
    windows = f"Funke-Editor-{version}-Windows-x64.zip"
    names = {windows, windows + ".sha256", "ci-evidence.json"}
    if release_policy.policy("v" + version)["requiresSigning"]:
        names.add(windows + ".sha256.p7s")
    for platform in ("Linux-x64", "macOS-arm64"):
        names.add(f"Funke-Editor-{version}-{platform}.tar.gz")
    for platform, extension in (("Windows-x64", "exe"), ("Linux-x64", "run"), ("macOS-arm64", "dmg")):
        installer = f"FunkeSetup-{version}-{platform}.{extension}"
        names.update((installer, installer + ".sha256", installer + ".manifest.json"))
    names.add(f"FunkeSetup-{version}-Windows-x64.exe.dependencies.json")
    return names


def prepare_assets(directory: Path, version: str, commit: str, repository: str) -> list[Path]:
    expected = payload_names(version)
    provenance_name = f"Funke-{version}-provenance.json"
    entries = list(directory.iterdir())
    if any(path.is_symlink() or not path.is_file() for path in entries):
        raise ValueError("Release directory must contain only regular files")
    actual = {path.name for path in entries}
    signature_name = f"Funke-Editor-{version}-Windows-x64.zip.sha256.p7s"
    if signature_name in actual:
        expected.add(signature_name)
    if expected - actual or actual - expected - {provenance_name, "SHA256SUMS"}:
        raise ValueError(f"Unexpected release inventory; missing={sorted(expected - actual)}, "
                         f"extra={sorted(actual - expected - {provenance_name, 'SHA256SUMS'})}")
    for name in sorted(expected):
        path = directory / name
        if not path.stat().st_size:
            raise ValueError(f"Empty release asset: {name}")
        if name.endswith(".sha256"):
            checksum = path.read_text(encoding="utf-8-sig").split()
            if len(checksum) != 2 or checksum[1].lstrip("*") != name.removesuffix(".sha256"):
                raise ValueError(f"Invalid checksum file: {name}")
            if checksum[0].lower() != digest(directory / checksum[1].lstrip("*")):
                raise ValueError(f"Checksum mismatch: {name}")
        if name.endswith(".manifest.json"):
            manifest = json.loads(path.read_text(encoding="utf-8-sig"))
            engine = manifest["engine"]
            if (engine.get("commit") != commit or engine.get("version") != version or
                    engine.get("dirty") is not False or engine.get("identityScope") != "repository"):
                raise ValueError(f"Wrong or dirty installer identity: {name}")
            if name.endswith(".dmg.manifest.json"):
                mac_signed, mac_notarized = manifest.get("signed") is True, manifest.get("notarized") is True
                if release_policy.policy("v" + version)["requiresSigning"] and not (mac_signed and mac_notarized):
                    raise ValueError("The macOS release must be signed and notarized")
    evidence = json.loads((directory / "ci-evidence.json").read_text(encoding="utf-8-sig"))
    if (evidence.get("commit") != commit or evidence.get("passed") is not True or evidence.get("errors") or
            evidence.get("scope") != {"platforms": "all", "coreOnly": False} or
            str(evidence.get("runId")) != os.environ.get("GITHUB_RUN_ID") or
            str(evidence.get("attempt")) != os.environ.get("GITHUB_RUN_ATTEMPT")):
        raise ValueError("Full successful CI evidence from this tag-run attempt is required")
    records = [{"name": name, "bytes": (directory / name).stat().st_size,
                "sha256": digest(directory / name)} for name in sorted(expected)]
    preview = release_policy.policy("v" + version)["unsignedPreview"]
    provenance = {"schemaVersion": 1, "version": version, "tag": "v" + version, "commit": commit,
                  "status": "unsigned-preview-draft" if preview else "signed-tag-draft", "publicationReady": False,
                  "signing": {"required": not preview, "windowsChecksumSignatureIncluded": signature_name in actual,
                              "macosDeveloperIdSigned": mac_signed, "macosNotarized": mac_notarized},
                  "ci": {"runId": evidence["runId"], "attempt": evidence["attempt"],
                         "url": f"https://github.com/{repository}/actions/runs/{evidence['runId']}"},
                  "assets": records,
                  "pendingPublicationGates": (["Early-preview release review and known-limitations disclosure",
                                                "Durable private-symbol backup"] if preview else
                                               ["Clean-machine acceptance", "Durable private-symbol backup",
                                                "Final release review"])}
    (directory / provenance_name).write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    files = [directory / name for name in sorted(expected | {provenance_name})]
    (directory / "SHA256SUMS").write_text(
        "".join(f"{digest(path)}  {path.name}\n" for path in files), encoding="utf-8")
    return files + [directory / "SHA256SUMS"]


def gh(*arguments: str) -> str:
    return subprocess.run(["gh", *arguments], check=True, text=True, encoding="utf-8",
                          stdout=subprocess.PIPE, timeout=3600).stdout


def sync_draft(repository: str, tag: str, commit: str, notes: Path, files: list[Path]) -> str:
    ref = json.loads(gh("api", f"repos/{repository}/git/ref/tags/{tag}"))["object"]
    for _ in range(5):
        if ref["type"] == "commit":
            break
        if ref["type"] != "tag":
            raise ValueError("Release tag must resolve to a commit")
        ref = json.loads(gh("api", f"repos/{repository}/git/tags/{ref['sha']}"))["object"]
    if ref["type"] != "commit" or ref["sha"] != commit:
        raise ValueError("Remote tag does not point to the verified commit")
    pages = json.loads(gh("api", f"repos/{repository}/releases?per_page=100", "--paginate", "--slurp"))
    matches = [release for page in pages for release in page if release["tag_name"] == tag]
    if len(matches) > 1:
        raise ValueError("Multiple releases match the tag")
    expected = {path.name: path for path in files}
    title = f"Funke {tag[1:]}" + (" — Unsigned Preview" if release_policy.policy(tag)["unsignedPreview"] else "")
    if matches:
        release = json.loads(gh("api", f"repos/{repository}/releases/{matches[0]['id']}"))
        if release.get("draft") is not True or release.get("published_at"):
            raise ValueError("Refusing to modify a published release")
        extras = {asset["name"] for asset in release["assets"]} - expected.keys()
        if extras:
            raise ValueError(f"Draft contains unexpected assets; review them before retrying: {sorted(extras)}")
        gh("release", "upload", tag, "--repo", repository, "--clobber", *map(str, files))
    else:
        gh("release", "create", tag, "--repo", repository, "--draft", "--verify-tag", "--latest=false",
           "--target", commit, "--title", title, "--notes-file", str(notes),
           f"--prerelease={'true' if '-' in tag else 'false'}", *map(str, files))
    current = json.loads(gh("api", f"repos/{repository}/releases/tags/{tag}"))
    if current.get("draft") is not True or current.get("published_at"):
        raise ValueError("Release is no longer an unpublished draft")
    uploaded = {asset["name"]: asset for asset in current["assets"]}
    if set(uploaded) != set(expected) or len(current["assets"]) != len(expected):
        raise ValueError("Uploaded release inventory differs from the verified artifacts")
    for name, path in expected.items():
        asset = uploaded[name]
        if (asset["state"] != "uploaded" or asset["size"] != path.stat().st_size or
                asset.get("digest") != "sha256:" + digest(path)):
            raise ValueError(f"Uploaded asset does not match the verified artifact: {name}")
    gh("release", "edit", tag, "--repo", repository, "--draft=true", "--latest=false", "--target", commit,
       "--title", title, "--notes-file", str(notes),
       f"--prerelease={'true' if '-' in tag else 'false'}")
    return current["html_url"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--notes", type=Path, required=True)
    args = parser.parse_args()
    try:
        version = os.environ["VERSION"]
        commit = os.environ["GITHUB_SHA"]
        repository = os.environ["GITHUB_REPOSITORY"]
        if (os.environ.get("GITHUB_REF_TYPE") != "tag" or os.environ.get("GITHUB_REF_NAME") != "v" + version or
                not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", version) or
                not re.fullmatch(r"[0-9a-f]{40}", commit) or
                not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository)):
            raise ValueError("This helper requires a matching version tag and full commit identity")
        if not args.notes.read_text(encoding="utf-8").strip():
            raise ValueError("Release notes cannot be empty")
        files = prepare_assets(args.directory, version, commit, repository)
        if release_policy.policy("v" + version)["unsignedPreview"]:
            notice = ("**Unsigned preview for early testers.** This version does not require Windows publisher signing "
                      "or macOS Developer ID signing/notarization. Windows security features and macOS Gatekeeper "
                      "may warn or block launch. All required CI tests and artifact checksum checks passed; manual "
                      "clean-machine and hardware acceptance remain pending. Back up projects before testing.\n\n")
            notes = args.notes.read_text(encoding="utf-8")
            if not notes.startswith(notice):
                args.notes.write_text(notice + notes, encoding="utf-8")
        print(sync_draft(repository, "v" + version, commit, args.notes, files))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"[Release draft] {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
