#!/usr/bin/env python3
"""One command from your Doom 64 ROM to an Xbox disc image.

    python setup.py                 # everything -> out/Release/XISO/Doom64-X.iso
    python setup.py --check         # say what is there and what is missing
    python setup.py --skip-build    # stop before compiling (to build in Visual Studio)
    python setup.py --config Debug  # the debug configuration

Needs: Python 3.8+ (standard library only), git, the RXDK toolchain (its
clang also builds the host-side ROM extractor; Visual Studio's cl or a gcc
on the PATH are used when this RXDK has no host sysroot), an internet
connection the first time (the Dreamcast port's own data is fetched), and
your Doom 64 ROM (US v1.0 or v1.1) in this folder as baserom.z64 or
doom64.z64. A .v64 / .n64 (byte-swapped) dump is accepted and put in order.
Optionally doom64.wad, the IWAD of the 2020 Nightdive release, for the
Lost Levels (maps 34-40), as the Dreamcast port takes it.

Nothing that comes from the ROM, the IWAD or the Dreamcast port's data is
in this repository; all of it is made or fetched here, into folders
.gitignore keeps out:

  1. the ROM, checked (MD5) and put in big-endian order: build/doom64.z64
  2. the Dreamcast port at DOOM64_DC_REV, fetched into extern/doom64-dc,
     for its own data -- the music and sound effects, the normal maps, the
     Knee-Deep-in-the-Dead levels and the rest of its selfboot/ folder
  3. wadtool, the Dreamcast port's ROM extractor (wadtool/), built for
     Windows: build/wadtool.exe
  4. wadtool run on the ROM (and the IWAD): the game's WADs, its levels and
     its sprite sheet, in build/wadgen/
  5. data/, the disc's data folder: the Dreamcast selfboot/ folder's
     contents and wadtool's output, as D:\\data, the game's /cd
  6. the dashboard icons (tools/xbox/make_icons.py), rxdk.project.json
     (gen_manifest.py) and the Visual Studio project (gen_vcxproj.py)
  7. the build: Rxdk.Cli compiles, links, bundles the icons and packs the ISO
"""
import argparse
import hashlib
import os
import shutil
import stat
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, "tools", "xbox"))
import xbconfig  # noqa: E402

NAME = "Doom64-X"
BUILD = os.path.join(ROOT, "build")
ROM_NAMES = ("baserom.z64", "doom64.z64", "baserom.v64", "doom64.v64", "baserom.n64", "doom64.n64")
ROM_OUT = os.path.join(BUILD, "doom64.z64")
IWAD_NAMES = ("doom64.wad", "DOOM64.WAD")
# the README's MD5s (the .z64, big-endian)
ROM_MD5 = {
    "b67748b64a2cc7efd2f3ad4504561e0e": "US v1.0",
    "1b1378bb9ee819f740550f566745af73": "US v1.1",
}
IWAD_MD5 = {
    "654c57d19f5c4a52cf8c63e34caa2fd2": "Nightdive (2020)",
    "0aaba212339c72250f8a53a0a2b6189e": "Nightdive (2020, update)",
}

# jnmartin84/doom64-dc: the revision src/ and wadtool/ here were taken from
DOOM64_DC_URL = "https://github.com/jnmartin84/doom64-dc"
DOOM64_DC_REV = "799ccb9fa252fb026deddaf1839835c73da25549"
EXTERN = os.path.join(ROOT, "extern", "doom64-dc")
SELFBOOT = os.path.join(EXTERN, "selfboot")

WADTOOL_SRC = ["decodes.c", "encode.c", "imgproc.c", "mapconv.c", "wadtool.c"]
WADTOOL_EXE = os.path.join(BUILD, "wadtool.exe")
WADGEN = os.path.join(BUILD, "wadgen")
DATA = os.path.join(ROOT, "data")

# what wadtool makes (the Lost Levels' maps only from the IWAD)
WADGEN_FILES = ["alt.wad", "pow2.wad", "tex/non_enemy.tex"] + \
               ["maps/map%02d.wad" % i for i in range(1, 34)]
LOST_LEVELS = ["maps/map%02d.wad" % i for i in range(34, 41)]


def say(msg):
    print("setup: " + msg, flush=True)


def die(msg):
    print("setup: ERROR: " + msg, file=sys.stderr, flush=True)
    sys.exit(1)


def run(cmd, cwd=ROOT, env=None):
    r = subprocess.run(cmd, cwd=cwd, env=env)
    if r.returncode != 0:
        die("failed (%d): %s" % (r.returncode, " ".join(cmd)))


def git_ok():
    return shutil.which("git") is not None


# ------------------------------------------------------------------- ROM

def find_rom():
    for n in ROM_NAMES:
        p = os.path.join(ROOT, n)
        if os.path.isfile(p):
            return p
    return None


def to_z64(data):
    """Any of the three N64 byte orders, as big-endian .z64."""
    head = data[:4]
    if head == b"\x80\x37\x12\x40":
        return data, "z64"
    if head == b"\x37\x80\x40\x12":       # .v64: 16-bit swapped
        b = bytearray(data)
        b[0::2], b[1::2] = data[1::2], data[0::2]
        return bytes(b), "v64"
    if head == b"\x40\x12\x37\x80":       # .n64: 32-bit little-endian
        b = bytearray(len(data))
        b[0::4], b[1::4], b[2::4], b[3::4] = data[3::4], data[2::4], data[1::4], data[0::4]
        return bytes(b), "n64"
    return None, None


def rom_check(write=False):
    """(ok, message): the ROM found, put in order, checked; written to
    build/doom64.z64 when `write`."""
    src = find_rom()
    if src is None:
        return False, ("no ROM: put your Doom 64 ROM (US v1.0 or v1.1) in %s as baserom.z64"
                       % ROOT)
    with open(src, "rb") as f:
        raw = f.read()
    data, order = to_z64(raw)
    if data is None:
        return False, "%s is not an N64 ROM" % os.path.basename(src)
    md5 = hashlib.md5(data).hexdigest()
    if md5 not in ROM_MD5:
        return False, ("%s is not a supported Doom 64 ROM (MD5 %s; US v1.0 is %s, v1.1 is %s)"
                       % (os.path.basename(src), md5, *ROM_MD5.keys()))
    if write:
        os.makedirs(BUILD, exist_ok=True)
        old = None
        if os.path.isfile(ROM_OUT):
            with open(ROM_OUT, "rb") as f:
                old = f.read()
        if old != data:
            with open(ROM_OUT, "wb") as f:
                f.write(data)
    note = "" if order == "z64" else " (a .%s dump, put in .z64 order)" % order
    return True, "%s: %s%s" % (os.path.basename(src), ROM_MD5[md5], note)


def find_iwad():
    for n in IWAD_NAMES:
        p = os.path.join(ROOT, n)
        if os.path.isfile(p):
            return p
    return None


def iwad_check():
    """(path or None, message)"""
    p = find_iwad()
    if p is None:
        return None, "none (optional: doom64.wad from the 2020 release adds the Lost Levels)"
    md5 = hashlib.md5(open(p, "rb").read()).hexdigest()
    if md5 not in IWAD_MD5:
        return None, "%s is not a supported IWAD (MD5 %s); left out" % (os.path.basename(p), md5)
    return p, "%s: %s, the Lost Levels are built" % (os.path.basename(p), IWAD_MD5[md5])


# ---------------------------------------------------------- the upstream

def extern_rev():
    if not os.path.exists(os.path.join(EXTERN, ".git")):
        return None
    r = subprocess.run(["git", "-C", EXTERN, "rev-parse", "HEAD"], capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    return r.stdout.strip() if r.returncode == 0 else None


def remove_tree(path):
    def retry(func, p, _exc):
        os.chmod(p, stat.S_IWRITE)
        func(p)
    shutil.rmtree(path, onerror=retry)


def step_fetch():
    """The Dreamcast port at DOOM64_DC_REV in extern/doom64-dc: only that
    commit, without history (its music alone is 600 MB)."""
    rev = extern_rev()
    if rev == DOOM64_DC_REV:
        return
    if not git_ok():
        die("git not found (install Git for Windows: https://git-scm.com/download/win)")
    if rev is None:
        if os.path.isdir(EXTERN):
            remove_tree(EXTERN)
        os.makedirs(EXTERN)
        say("fetching the Dreamcast port's data (doom64-dc %s, about 600 MB)" % DOOM64_DC_REV[:12])
        run(["git", "init", "-q", EXTERN])
        run(["git", "-C", EXTERN, "remote", "add", "origin", DOOM64_DC_URL])
    else:
        say("moving extern/doom64-dc from %s to %s" % (rev[:12], DOOM64_DC_REV[:12]))
    run(["git", "-C", EXTERN, "fetch", "--depth", "1", "origin", DOOM64_DC_REV])
    run(["git", "-C", EXTERN, "checkout", "-q", "--detach", "FETCH_HEAD"])
    if extern_rev() != DOOM64_DC_REV:
        die("extern/doom64-dc is not at %s after the fetch" % DOOM64_DC_REV)


# --------------------------------------------------------------- wadtool

def newest(paths):
    return max(os.path.getmtime(p) for p in paths)


def step_wadtool():
    srcs = [os.path.join(ROOT, "wadtool", f) for f in WADTOOL_SRC]
    hdrs = [os.path.join(ROOT, "wadtool", f) for f in os.listdir(os.path.join(ROOT, "wadtool"))
            if f.endswith(".h")]
    if os.path.isfile(WADTOOL_EXE) and os.path.getmtime(WADTOOL_EXE) >= newest(srcs + hdrs):
        return
    say("building wadtool, the ROM extractor")
    os.makedirs(BUILD, exist_ok=True)
    ok, how = xbconfig.build_host_program(srcs, WADTOOL_EXE, BUILD)
    if not ok:
        die("could not build wadtool: %s" % how)
    say("wadtool built with %s" % how)


def step_extract(iwad):
    """wadtool into build/wadgen, when the ROM, the IWAD or wadtool changed
    since it last ran."""
    stamp = os.path.join(WADGEN, ".stamp")
    inputs = [ROM_OUT, WADTOOL_EXE] + ([iwad] if iwad else [])
    want = LOST_LEVELS if iwad else []
    done = os.path.isfile(stamp) and os.path.getmtime(stamp) >= newest(inputs) and \
        all(os.path.isfile(os.path.join(WADGEN, f)) for f in WADGEN_FILES + want) and \
        open(stamp).read().strip() == ("iwad" if iwad else "rom")
    if done:
        return
    say("extracting the game from the ROM%s" % (" and the IWAD" if iwad else ""))
    if os.path.isdir(WADGEN):
        remove_tree(WADGEN)
    os.makedirs(os.path.join(WADGEN, "tex"))
    os.makedirs(os.path.join(WADGEN, "maps"))
    cmd = [WADTOOL_EXE, ROM_OUT, WADGEN] + ([iwad] if iwad else [])
    t = time.time()
    run(cmd, cwd=WADGEN)
    missing = [f for f in WADGEN_FILES + want if not os.path.isfile(os.path.join(WADGEN, f))]
    if missing:
        die("wadtool did not make %s" % ", ".join(missing))
    with open(stamp, "w") as f:
        f.write("iwad" if iwad else "rom")
    say("extracted in %.0f s" % (time.time() - t))


# ------------------------------------------------------------------ data

def copy_if_changed(src, dst):
    if os.path.isfile(dst):
        a, b = os.stat(src), os.stat(dst)
        if a.st_size == b.st_size and int(a.st_mtime) <= int(b.st_mtime):
            return False
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)
    return True


def step_data(iwad):
    """data/: the Dreamcast disc's files, as the Dreamcast build lays out
    its selfboot/ folder for mkdcdisc: the port's own data, then wadtool's
    output over it. Anything else in data/ is removed (it would end up on
    the disc)."""
    want = {}
    for dp, dn, fn in os.walk(SELFBOOT):
        dn[:] = [d for d in dn if not d.startswith(".")]
        for f in fn:
            rel = os.path.relpath(os.path.join(dp, f), SELFBOOT).replace("\\", "/")
            if os.path.getsize(os.path.join(dp, f)) == 0:
                continue        # maps/a: git's placeholder for an empty folder
            if rel == "controls.ini":
                continue        # the Dreamcast pad's layout; the Xbox build has its own
            want[rel] = os.path.join(dp, f)
    for f in WADGEN_FILES + (LOST_LEVELS if iwad else []):
        want[f] = os.path.join(WADGEN, f)
    for f in ("bump.wad", "tex/wepn_decs.raw", "symbols.raw", "warn3.dt", "doom1mn.lmp"):
        if f not in want:
            die("the Dreamcast port's %s is missing from extern/doom64-dc/selfboot" % f)
    copied = 0
    for rel, src in sorted(want.items()):
        copied += copy_if_changed(src, os.path.join(DATA, rel.replace("/", os.sep)))
    removed = 0
    for dp, dn, fn in os.walk(DATA, topdown=False):
        for f in fn:
            rel = os.path.relpath(os.path.join(dp, f), DATA).replace("\\", "/")
            if rel not in want:
                os.remove(os.path.join(dp, f))
                removed += 1
        if dp != DATA and not os.listdir(dp):
            os.rmdir(dp)
    total = sum(os.path.getsize(p) for p in want.values())
    say("data/: %d files, %.0f MB (%d copied, %d removed)" % (len(want), total / 1048576.0,
                                                               copied, removed))


def tool(name, *args):
    run([sys.executable, os.path.join(ROOT, "tools", "xbox", name)] + list(args))


def step_xbox():
    tool("make_icons.py")
    tool("gen_manifest.py")
    if os.path.isfile(xbconfig.rxdk_cli()):
        tool("gen_vcxproj.py")


def step_build(config):
    cli = xbconfig.rxdk_cli()
    if not os.path.isfile(cli):
        die("Rxdk.Cli.exe not found: install RXDK (RXDK > RXDK Window > Install Prerequisites)")
    # the build copies data/ into its output and never removes anything
    # there: what data/ no longer has would still go onto the disc
    out_data = os.path.join(ROOT, "out", config, "Build", NAME, "data")
    stale = 0
    for dp, dn, fn in os.walk(out_data, topdown=False):
        for f in fn:
            rel = os.path.relpath(os.path.join(dp, f), out_data)
            if not os.path.isfile(os.path.join(DATA, rel)):
                os.remove(os.path.join(dp, f))
                stale += 1
        if dp != out_data and not os.listdir(dp):
            os.rmdir(dp)
    if stale:
        say("removed %d stale file(s) from out/%s/Build/%s/data" % (stale, config, NAME))
    say("building %s" % config)
    run([cli, "build", "--project-root", ROOT, "--configuration", config])
    iso = os.path.join(ROOT, "out", config, "XISO", NAME + ".iso")
    if os.path.isfile(iso):
        say("done: %s (%.0f MB)" % (iso, os.path.getsize(iso) / 1048576.0))
    else:
        say("built; the ISO is under out/%s/XISO" % config)


# ----------------------------------------------------------------- check

def check():
    ok = True
    good, why = rom_check()
    say("ROM: %s" % why)
    ok &= good
    _, why = iwad_check()
    say("IWAD: %s" % why)
    py = sys.version_info >= (3, 8)
    say("Python: %d.%d%s" % (sys.version_info[0], sys.version_info[1], "" if py else " (3.8 or later needed)"))
    ok &= py
    g = shutil.which("git")
    say("git: %s" % (g or "missing (install Git for Windows)"))
    ok &= g is not None
    cli = xbconfig.rxdk_cli()
    there = os.path.isfile(cli)
    say("RXDK CLI: %s" % (cli if there else "missing (RXDK > RXDK Window > Install Prerequisites)"))
    ok &= there
    hc = xbconfig.host_cc_available()
    say("host C compiler (wadtool): %s" % (hc or "missing (the RXDK's clang, Visual Studio or gcc)"))
    ok &= hc is not None
    rev = extern_rev()
    say("doom64-dc data: %s" % ("at %s" % rev[:12] if rev == DOOM64_DC_REV else
                                ("at %s, will be moved to %s" % (rev[:12], DOOM64_DC_REV[:12]) if rev
                                 else "not fetched yet (the build fetches it)")))
    for label, path in (("wadtool", WADTOOL_EXE), ("extracted game", os.path.join(WADGEN, ".stamp")),
                        ("data", DATA), ("manifest", os.path.join(ROOT, "rxdk.project.json"))):
        say("%s: %s" % (label, "built" if os.path.exists(path) else "not built yet"))
    return ok


def main():
    ap = argparse.ArgumentParser(description="ROM -> Xbox disc image for Doom 64")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--config", default="Release", choices=["Release", "Debug"])
    a = ap.parse_args()
    if sys.version_info < (3, 8):
        die("Python 3.8 or later is needed")
    if a.check:
        sys.exit(0 if check() else 1)

    good, why = rom_check(write=True)
    if not good:
        die(why)
    say(why)
    iwad, why = iwad_check()
    say("IWAD: " + why)
    step_fetch()
    step_wadtool()
    step_extract(iwad)
    step_data(iwad)
    step_xbox()
    if not a.skip_build:
        step_build(a.config)


if __name__ == "__main__":
    main()
