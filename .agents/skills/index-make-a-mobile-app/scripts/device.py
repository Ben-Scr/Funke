"""Android device helper for Index apps: install, launch, screenshot, tap, memory, logs and native heap profiles.

    python device.py devices
    python device.py install <apk>
    python device.py launch <package> [--fresh]          # --fresh force-stops first
    python device.py stop <package>
    python device.py shot <out.png>
    python device.py tap <x> <y>                          # device pixels, see the screenshot
    python device.py swipe <x1> <y1> <x2> <y2> [ms]
    python device.py key back|home|enter
    python device.py mem <package> [--seconds 24] [--interval 3]
    python device.py log <package> [--grep TEXT] [--lines 60]
    python device.py crash
    python device.py heap <package> <unstripped libmain.so> [--seconds 6] [--top 4]

Runs adb with argument lists (no shell), so Git Bash never rewrites device paths like /data/... .
Never act on the user's phone without being asked: launching, tapping and playing audio happen on a real device."""
import argparse
import collections
import glob
import os
import shutil
import subprocess
import sys
import tempfile
import time


def find_adb():
    candidates = [shutil.which("adb")]
    for env in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        if os.environ.get(env):
            candidates.append(os.path.join(os.environ[env], "platform-tools", "adb.exe" if os.name == "nt" else "adb"))
    local = os.environ.get("LOCALAPPDATA", "")
    candidates.append(os.path.join(local, "Android", "Sdk", "platform-tools", "adb.exe"))
    # Unity installs bring their own SDK; the newest editor usually has the newest adb.
    candidates += sorted(glob.glob(r"C:\Program Files\Unity\Hub\Editor\*\Editor\Data\PlaybackEngines\AndroidPlayer\SDK\platform-tools\adb.exe"), reverse=True)
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    sys.exit("adb not found: install platform-tools or set ANDROID_HOME")


ADB = None


def adb(*args, check=True, capture=True, binary=False):
    result = subprocess.run([ADB, *args], capture_output=capture, text=not binary)
    if check and result.returncode != 0:
        err = result.stderr if not binary else result.stderr.decode(errors="replace")
        sys.exit(f"adb {' '.join(args)} failed: {err.strip()}")
    return result.stdout


def pid_of(package):
    return adb("shell", "pidof", package, check=False).strip()


def cmd_devices(_):
    print(adb("devices", "-l").strip())


def cmd_install(a):
    print(adb("install", "-r", a.apk).strip().splitlines()[-1])


def cmd_launch(a):
    if a.fresh:
        adb("shell", "am", "force-stop", a.package)
    adb("shell", "monkey", "-p", a.package, "-c", "android.intent.category.LAUNCHER", "1")
    print(f"launched {a.package}")


def cmd_stop(a):
    adb("shell", "am", "force-stop", a.package)


def cmd_shot(a):
    png = adb("exec-out", "screencap", "-p", binary=True)
    with open(a.out, "wb") as f:
        f.write(png)
    print(a.out)


def cmd_tap(a):
    # A 120 ms press: `input tap` presses and releases within one engine frame, so Index UI never sees the click.
    adb("shell", "input", "swipe", str(a.x), str(a.y), str(a.x), str(a.y), "120")


def cmd_swipe(a):
    adb("shell", "input", "swipe", str(a.x1), str(a.y1), str(a.x2), str(a.y2), str(a.ms))


def cmd_key(a):
    codes = {"back": "4", "home": "3", "enter": "66"}
    adb("shell", "input", "keyevent", codes[a.name])


def cmd_mem(a):
    """Native Heap is what leaks in practice (driver allocations land there too); PSS hides swapped memory."""
    samples = max(1, int(a.seconds / a.interval))
    for i in range(samples):
        time.sleep(a.interval)
        pid = pid_of(a.package)
        if not pid:
            print(f"{(i + 1) * a.interval:5.0f}s  DEAD (see: device.py crash)")
            return
        info = adb("shell", "dumpsys", "meminfo", pid, check=False)
        native = pss = graphics = 0
        for line in info.splitlines():
            parts = line.split()
            if line.strip().startswith("Native Heap") and len(parts) > 2 and parts[2].isdigit():
                native = int(parts[2]) // 1024
            elif line.strip().startswith("TOTAL PSS:"):
                pss = int(parts[2]) // 1024
            elif line.strip().startswith("Graphics:"):
                graphics = int(parts[1]) // 1024
        print(f"{(i + 1) * a.interval:5.0f}s  native={native}MB graphics={graphics}MB pss={pss}MB")


def cmd_log(a):
    pid = pid_of(a.package)
    args = ["logcat", "-d"] + (["--pid", pid] if pid else [])
    lines = adb(*args, check=False).splitlines()
    if not pid:
        lines = [l for l in lines if " Index " in l]
    if a.grep:
        lines = [l for l in lines if a.grep.lower() in l.lower()]
    print("\n".join(l[:300] for l in lines[-a.lines:]))


def cmd_crash(_):
    print(adb("logcat", "-d", "-b", "crash", check=False)[-6000:])


# ---------------------------------------------------------------- heap profile (heapprofd)

def _varint(b, i):
    r = s = 0
    while True:
        c = b[i]
        i += 1
        r |= (c & 0x7F) << s
        s += 7
        if c < 0x80:
            return r, i


def _fields(b):
    i = 0
    while i < len(b):
        key, i = _varint(b, i)
        f, wt = key >> 3, key & 7
        if wt == 0:
            v, i = _varint(b, i)
        elif wt == 1:
            v = b[i:i + 8]
            i += 8
        elif wt == 2:
            n, i = _varint(b, i)
            v = b[i:i + n]
            i += n
        elif wt == 5:
            v = b[i:i + 4]
            i += 4
        else:
            raise ValueError(f"wire type {wt}")
        yield f, wt, v


def _packed(v):
    out, i = [], 0
    while i < len(v):
        x, i = _varint(v, i)
        out.append(x)
    return out


HEAP_CONFIG = """buffers { size_kb: 131072 fill_policy: DISCARD }
data_sources { config { name: "android.heapprofd" heapprofd_config {
  sampling_interval_bytes: 32768 process_cmdline: "%s" shmem_size_bytes: 33554432 block_client: true } } }
duration_ms: %d
"""


def cmd_heap(a):
    """Needs a debuggable APK (temporarily add android:debuggable="true" to the Android manifest template).
    Decodes the trace itself (no trace_processor needed) and symbolizes libmain.so frames with llvm-symbolizer."""
    if not pid_of(a.package):
        sys.exit("start the app first (device.py launch ...)")
    remote = "/data/misc/perfetto-traces/index_heap.pftrace"
    cfg = HEAP_CONFIG % (a.package, int(a.seconds * 1000))
    run = subprocess.run([ADB, "shell", "perfetto", "--txt", "-c", "-", "-o", remote], input=cfg, capture_output=True, text=True)
    if run.returncode != 0:
        sys.exit(run.stderr)
    local = os.path.join(tempfile.gettempdir(), "index_heap.pftrace")
    adb("pull", remote, local)
    data = open(local, "rb").read()

    fn, frames, stacks, maps, paths, samples = {}, {}, {}, {}, {}, []
    for _, _, pkt in _fields(data):
        for pf, _, pv in _fields(pkt):
            if pf == 12:  # interned data
                for sf, _, sv in _fields(pv):
                    d = collections.defaultdict(list)
                    for k, _, v in _fields(sv):
                        d[k].append(v)
                    iid = d[1][0] if 1 in d else 0
                    if sf in (5, 17):
                        (fn if sf == 5 else paths)[iid] = d[2][0].decode("utf-8", "replace") if 2 in d else ""
                    elif sf == 6:
                        frames[iid] = (d[2][0] if 2 in d else 0, d[3][0] if 3 in d else 0, d[4][0] if 4 in d else 0)
                    elif sf == 7:
                        ids = []
                        for x in d.get(2, []):
                            ids += _packed(x) if isinstance(x, (bytes, bytearray)) else [x]
                        stacks[iid] = ids
                    elif sf == 19:
                        maps[iid] = [paths.get(p, "?") for p in d.get(7, [])]
            elif pf == 37:  # profile packet
                for sf, _, sv in _fields(pv):
                    if sf == 5:
                        for k, _, v in _fields(sv):
                            if k == 2:
                                s = {kk: vv for kk, _, vv in _fields(v)}
                                samples.append((s.get(1, 0), s.get(2, 0) - s.get(3, 0)))

    symbolizer = shutil.which("llvm-symbolizer") or next(iter(sorted(glob.glob(
        r"C:\Program Files\Unity\Hub\Editor\*\Editor\Data\PlaybackEngines\AndroidPlayer\NDK\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-symbolizer.exe"),
        reverse=True)), None)

    def frame(fid):
        name, mid, pc = frames.get(fid, (0, 0, 0))
        lib = "/".join(maps.get(mid, ["?"])).split("/")[-1]
        text = f"{lib}+0x{pc:x} {fn.get(name, '')}"
        if lib == "libmain.so" and symbolizer and a.lib:
            out = subprocess.run([symbolizer, "--obj=" + a.lib, "-C", "-f", "--no-inlines", hex(pc)],
                                 capture_output=True, text=True).stdout.splitlines()
            text += "  ->  " + (out[0] if out else "?")
        return text

    live = collections.Counter()
    for cs, bytes_live in samples:
        live[cs] += bytes_live
    print(f"live sampled heap: {sum(v for v in live.values() if v > 0) / 1e6:.1f} MB over {len(stacks)} stacks")
    for cs, size in live.most_common(a.top):
        print(f"\n### {size / 1e6:.1f} MB live (leaf last)")
        for fid in stacks.get(cs, [])[-14:]:
            print("   ", frame(fid))


def main():
    global ADB
    ADB = find_adb()
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("devices").set_defaults(fn=cmd_devices)
    s = sub.add_parser("install"); s.add_argument("apk"); s.set_defaults(fn=cmd_install)
    s = sub.add_parser("launch"); s.add_argument("package"); s.add_argument("--fresh", action="store_true"); s.set_defaults(fn=cmd_launch)
    s = sub.add_parser("stop"); s.add_argument("package"); s.set_defaults(fn=cmd_stop)
    s = sub.add_parser("shot"); s.add_argument("out"); s.set_defaults(fn=cmd_shot)
    s = sub.add_parser("tap"); s.add_argument("x", type=int); s.add_argument("y", type=int); s.set_defaults(fn=cmd_tap)
    s = sub.add_parser("swipe")
    for n in ("x1", "y1", "x2", "y2"):
        s.add_argument(n, type=int)
    s.add_argument("ms", type=int, nargs="?", default=300)
    s.set_defaults(fn=cmd_swipe)
    s = sub.add_parser("key"); s.add_argument("name", choices=["back", "home", "enter"]); s.set_defaults(fn=cmd_key)
    s = sub.add_parser("mem"); s.add_argument("package"); s.add_argument("--seconds", type=float, default=24)
    s.add_argument("--interval", type=float, default=3); s.set_defaults(fn=cmd_mem)
    s = sub.add_parser("log"); s.add_argument("package"); s.add_argument("--grep"); s.add_argument("--lines", type=int, default=60)
    s.set_defaults(fn=cmd_log)
    sub.add_parser("crash").set_defaults(fn=cmd_crash)
    s = sub.add_parser("heap"); s.add_argument("package"); s.add_argument("lib", nargs="?", default="")
    s.add_argument("--seconds", type=float, default=6); s.add_argument("--top", type=int, default=4); s.set_defaults(fn=cmd_heap)
    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
