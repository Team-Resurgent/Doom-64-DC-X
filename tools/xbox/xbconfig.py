"""The Xbox build's single description of how Doom 64 is compiled.

Rxdk.Cli compiles the game from the manifest tools/xbox/gen_manifest.py
writes; tools/xbox/cc_check.py compiles the same sources the same way to list
every error at once. Both take their defines, include paths and flags from
here.

The defines are the Dreamcast Makefile's (DEFINES: _FINALROM, NDEBUG,
F3DEX_GBI_2), plus TARGET_XBOX, which marks the port's own changes to the
Dreamcast sources (each one is an `XBOX:` comment beside a TARGET_XBOX test).

The host side (wadtool, the ROM extractor the Dreamcast build compiles with
the host's gcc) is built here too: with the RXDK's own clang against its
64-bit MinGW sysroot when this RXDK has one, else with Visual Studio's cl.
"""
import glob
import os
import shutil
import subprocess
import tempfile
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def rxdk_root():
    """%ProgramData%\\RXDK, or $RXDK_ROOT."""
    if os.environ.get("RXDK_ROOT"):
        return os.environ["RXDK_ROOT"]
    pd = os.environ.get("ProgramData") or r"C:\ProgramData"
    return os.path.join(pd, "RXDK")


def rxdk_cli():
    staged = os.environ.get("RXDK_STAGED_TOOLS")
    if staged:
        return os.path.join(staged, "Rxdk.Cli.exe")
    return os.path.join(rxdk_root(), "tools", "Rxdk.Cli.exe")


def _rxdk_roots():
    roots = [rxdk_root()]
    local = os.environ.get("LOCALAPPDATA")
    if local:
        roots.append(os.path.join(local, "RXDK"))
    return [r for r in roots if os.path.isdir(r)]


_clang_cache = []


def clang():
    """The RXDK's own clang: $RXDK_CLANG, the known layouts, then any
    clang.exe under the RXDK's folders. A path that does not exist when there
    is none."""
    if _clang_cache:
        return _clang_cache[0]
    found = None
    env = os.environ.get("RXDK_CLANG", "").strip()
    if env and os.path.isfile(env):
        found = env
    if found is None:
        for root in _rxdk_roots():
            for pattern in (os.path.join(root, "llvm", "*", "bin", "clang.exe"),
                            os.path.join(root, "llvm", "bin", "clang.exe"),
                            os.path.join(root, "tools", "llvm", "*", "bin", "clang.exe"),
                            os.path.join(root, "tools", "llvm", "bin", "clang.exe")):
                hits = sorted(glob.glob(pattern))
                if hits:
                    found = hits[-1]
                    break
            if found:
                break
    if found is None:
        for root in _rxdk_roots():
            for dirpath, dirnames, filenames in os.walk(root):
                if dirpath[len(root):].count(os.sep) >= 5:
                    dirnames[:] = []
                if "clang.exe" in filenames:
                    found = os.path.join(dirpath, "clang.exe")
                    break
            if found:
                break
    if found is None:
        found = os.path.join(rxdk_root(), "llvm", "xboxog-windows-x64", "bin", "clang.exe")
    _clang_cache.append(found)
    return found


def llvm_root():
    """The LLVM install the RXDK's clang is in (the folder above its bin)."""
    return os.path.dirname(os.path.dirname(clang()))


def sdk_include():
    return os.path.join(rxdk_root(), "sdk", "include")


# What Rxdk.Cli puts in front of every C compile (its build log), minus the
# optimisation level and debug info. cc_check.py uses it when it cannot read
# the installed RXDK's own line.
def rxdk_base_flags():
    return ["-fno-sanitize=undefined", "-ffreestanding", "-fno-stack-protector",
            "-fms-extensions", "-fms-compatibility", "-nostdinc",
            "-include", "picolibc.h", "-march=pentium3",
            "-D_XBOX", "-DXBOX", "-fno-builtin", "-U_DEBUG", "-D__ASSERT_VERBOSE",
            "-femulated-tls", "-mlong-double-64",
            "-I", sdk_include(),
            "-Wno-c++11-narrowing", "-Wno-address-of-temporary",
            "-Wno-ignored-pragma-intrinsic", "-Wno-multichar",
            "-Wno-unused-command-line-argument", "-Wno-deprecated-enum-enum-conversion"]


# The Dreamcast Makefile's defines, and the port's own.
DEFINES = [
    "TARGET_XBOX=1",
    "_FINALROM=1",
    "NDEBUG=1",
    "F3DEX_GBI_2=1",
    # newlib's <sys/cdefs.h> (KallistiOS's C library) defines these, and
    # src/sndwav.h opens with them; picolibc's does not, and RXDK searches its
    # SDK headers before the project's, so a wrapper header cannot add them.
    # Every game and platform source is C: both are empty.
    "__BEGIN_DECLS=",
    "__END_DECLS=",
]

# Platform/xbox first: its kos.h, dc/*, arch/* stand in for KallistiOS.
INCLUDE_PATHS = [
    "Platform/xbox",
    "src",
]

COMPILE_FLAGS = [
    # RXDK compiles C as -std=c23 (bool/true/false are keywords there); the
    # Dreamcast build is -std=gnu17.
    "-std=gnu17",
    # -fms-compatibility (RXDK's) switches clang to MSVC bitfield layout,
    # which changes the size of a struct mixing bitfield types. KOS's PVR
    # header words and the game's structs need GCC's.
    "-mno-ms-bitfields",
    # Single-precision float maths in SSE, as the SH-4 does it in its FPU
    # (the P3 has SSE but not SSE2, so doubles stay on x87).
    "-msse", "-mfpmath=sse",
    "-fno-strict-aliasing",
    "-fwrapv",
    # The Dreamcast build is GCC's; clang makes several of these errors where
    # GCC warns, and the Makefile already silences the rest.
    "-Wno-int-conversion",
    "-Wno-incompatible-pointer-types",
    "-Wno-error=implicit-function-declaration",
    "-Wno-implicit-function-declaration",
    "-Wno-unused-variable",
    "-Wno-unused-but-set-variable",
    "-Wno-parentheses",
    "-Wno-missing-braces",
    "-Wno-switch",
    "-Wno-unknown-pragmas",
    "-Wno-unknown-warning-option",
    "-Wno-pragma-pack",
    "-Wno-microsoft-anon-tag",
    "-Wno-macro-redefined",
    "-Wno-int-to-pointer-cast",
    "-Wno-pointer-to-int-cast",
    "-Wno-deprecated-declarations",
    "-Wno-implicit-fallthrough",
    "-Wno-format",
    "-Wno-c23-extensions",
    "-Wno-gnu-folding-constant",
    # i686-pc-windows-gnu predefines these; nothing in this game is Win32.
    "-U_WIN32", "-U_WIN64", "-U__MINGW32__",
]


def _split_cmdline(line):
    """A command line as Rxdk.Cli echoes it, into arguments (Windows rules,
    as far as its echo needs: quotes group, and are dropped)."""
    args, cur, quoted, have = [], [], False, False
    for ch in line:
        if ch == '"':
            quoted = not quoted
            have = True
        elif ch in " \t" and not quoted:
            if have:
                args.append("".join(cur))
            cur, have = [], False
        else:
            cur.append(ch)
            have = True
    if have:
        args.append("".join(cur))
    return args


_probe_lock = threading.Lock()
_probe_cache = []


def rxdk_compile_prefix():
    """The compiler and the flags the INSTALLED RXDK compiles C with, read
    off its own build of a one-line probe (Rxdk.Cli build --compile-only
    echoes each compile as "$ <command>"): everything in front of its "-x c
    -c <source>". RXDK versions differ -- in where their clang lives, and in
    what their SDK headers demand (1.3's picolibc.h stops a compile without
    -mlong-double-64) -- so the asset segments are compiled with whatever
    this RXDK compiles the game with, not with a copy of one version's
    flags. None when the probe fails (rxdk_base_flags() is used then)."""
    with _probe_lock:
        if _probe_cache:
            return _probe_cache[0]
        prefix = None
        cli = rxdk_cli()
        tmp = tempfile.mkdtemp(prefix="doom64x_rxdk_probe")
        try:
            if os.path.isfile(cli):
                with open(os.path.join(tmp, "probe.c"), "w", encoding="utf-8") as f:
                    f.write("int doom64x_rxdk_probe;\n")
                with open(os.path.join(tmp, "rxdk.project.json"), "w", encoding="utf-8") as f:
                    f.write('{ "name": "probe", "defaultConfiguration": "Release", "configurations": '
                            '{ "Release": { "configuration": "release", "sources": ["probe.c"], '
                            '"libraries": [] } } }\n')
                r = subprocess.run([cli, "build", "--project-root", tmp, "--configuration", "Release",
                                    "--compile-only"], capture_output=True, text=True,
                                   encoding="utf-8", errors="replace")
                for line in (r.stdout + "\n" + r.stderr).splitlines():
                    line = line.strip()
                    if not line.startswith("$ ") or "probe.c" not in line:
                        continue
                    args = _split_cmdline(line[2:])
                    if "-x" in args and args and os.path.isfile(args[0]):
                        prefix = args[:args.index("-x")]
                    break
        except OSError:
            prefix = None
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        _probe_cache.append(prefix)
        return prefix


def compile_command(src, obj, extra=()):
    """The clang command line RXDK compiles `src` with (cc_check.py)."""
    cmd = rxdk_compile_prefix()
    if cmd is not None:
        cmd = list(cmd)
    else:
        if not os.path.isfile(clang()):
            raise SystemExit("no Xbox C compiler: the RXDK's clang (%s) was not found" % clang())
        res = None
        try:
            r = subprocess.run([clang(), "-print-resource-dir"], capture_output=True, text=True)
            res = os.path.join(r.stdout.strip(), "include")
        except OSError:
            pass
        cmd = [clang(), "-target", "i686-pc-windows-gnu"] + (["-isystem", res] if res else [])
        cmd += rxdk_base_flags()
    for p in INCLUDE_PATHS:
        cmd += ["-I", os.path.join(ROOT, p)]
    cmd += ["-D" + d for d in DEFINES]
    cmd += COMPILE_FLAGS + list(extra)
    cmd += ["-x", "c", "-c", src, "-o", obj]
    return cmd


# ------------------------------------------------------------ host tools

def host_clang_command(sources, out):
    """A command line that builds `sources` into the host program `out` with
    the RXDK's clang and its 64-bit MinGW sysroot, or None when this RXDK has
    no host sysroot."""
    c = clang()
    if not os.path.isfile(c):
        return None
    sysroot = os.path.join(llvm_root(), "host-sysroot")
    resdirs = sorted(glob.glob(os.path.join(sysroot, "lib", "clang", "*")))
    if not os.path.isdir(sysroot) or not resdirs:
        return None
    return [c, "--target=x86_64-w64-mingw32", "--sysroot=" + sysroot,
            "-resource-dir", resdirs[-1], "-fuse-ld=lld", "-rtlib=compiler-rt",
            "-unwindlib=libunwind", "-O2", "-fno-common", "-Wno-unused-result",
            "-Wno-everything", "-o", out] + list(sources)


def find_vs():
    """The newest Visual Studio (2019 or later) with the x64 C++ tools, by
    vswhere: its folder, or None."""
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.isfile(vswhere):
        return None
    r = subprocess.run([vswhere, "-latest", "-products", "*",
                        "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                        "-property", "installationPath", "-utf8"],
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    path = r.stdout.strip().splitlines()[0] if r.returncode == 0 and r.stdout.strip() else ""
    return path if path and os.path.isdir(path) else None


def host_cc_available():
    """Which host compiler build_host_program would use, for --check."""
    if host_clang_command(["x.c"], "x.exe") is not None:
        return "RXDK clang (MinGW host sysroot)"
    if find_vs():
        return "Visual Studio cl"
    for cc in ("gcc", "clang"):
        if shutil.which(cc):
            return cc
    return None


def build_host_program(sources, out, cwd):
    """Compile `sources` (C files) into the Windows program `out`. Returns
    (ok, message)."""
    tries = []
    cmd = host_clang_command(sources, out)
    if cmd is not None:
        tries.append(("RXDK clang", cmd, False))
    vs = find_vs()
    if vs is not None:
        vcvars = os.path.join(vs, "VC", "Auxiliary", "Build", "vcvars64.bat")
        if os.path.isfile(vcvars):
            line = 'call "%s" >nul && cl /nologo /O2 /w /Fe"%s" %s' % (
                vcvars, out, " ".join('"%s"' % s for s in sources))
            tries.append(("Visual Studio cl", 'cmd /d /s /c "' + line + '"', True))
    for cc in ("gcc", "clang"):
        exe = shutil.which(cc)
        if exe:
            tries.append((cc, [exe, "-O2", "-fno-common", "-w", "-o", out] + list(sources), False))
    why = []
    for name, cmd, shell in tries:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, shell=shell,
                           encoding="utf-8", errors="replace")
        if r.returncode == 0 and os.path.isfile(out):
            return True, name
        tail = (r.stdout + r.stderr).strip().splitlines()
        why.append("%s: %s" % (name, tail[-1] if tail else "failed"))
    return False, "; ".join(why) if why else "no host C compiler found"
