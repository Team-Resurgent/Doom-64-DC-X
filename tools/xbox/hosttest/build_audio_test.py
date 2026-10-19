#!/usr/bin/env python3
"""Build tools/xbox/hosttest/audio_test.exe: the Xbox port's sound path on the PC.

32-bit, like the Xbox (the AICA layer keeps host addresses in 32-bit sound
words), with the RXDK's clang against Visual Studio's x86 libraries.

    python tools/xbox/hosttest/build_audio_test.py
    build/hosttest/audio_test.exe data build/hosttest/out.wav 20 musamb01 0
    python tools/xbox/hosttest/audio_check.py data/mus/musamb01.adpcm build/hosttest/out.wav
"""
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import xbconfig  # noqa: E402

ROOT = xbconfig.ROOT
OUT = os.path.join(ROOT, "build", "hosttest")
P = os.path.join(ROOT, "Platform", "xbox")

SOURCES = [
    os.path.join(P, "aica", "aica_bus_xbox.c"),
    os.path.join(P, "aica", "aica_fw_xbox.c"),
    os.path.join(P, "aica", "aica_mixer_xbox.c"),
    os.path.join(P, "kos_snd_stream_xbox.c"),
    os.path.join(P, "kos_sfxmgr_xbox.c"),
    os.path.join(P, "kos_fs_xbox.c"),
    os.path.join(P, "kos_thread_xbox.c"),
    os.path.join(ROOT, "src", "sndwav.c"),
    os.path.join(HERE, "audio_test.c"),
]


def msvc():
    vs = xbconfig.find_vs()
    if vs is None:
        raise SystemExit("build_audio_test: Visual Studio (or its Build Tools) with the C++ tools is needed")
    tools = sorted(glob.glob(os.path.join(vs, "VC", "Tools", "MSVC", "*")))[-1]
    kits = r"C:\Program Files (x86)\Windows Kits\10"
    ver = sorted(os.listdir(os.path.join(kits, "Lib")))[-1]
    inc = [os.path.join(tools, "include")] + [os.path.join(kits, "Include", ver, d)
                                               for d in ("ucrt", "um", "shared")]
    lib = [os.path.join(tools, "lib", "x86")] + [os.path.join(kits, "Lib", ver, d, "x86")
                                                for d in ("ucrt", "um")]
    return inc, lib


def main():
    inc, lib = msvc()
    os.makedirs(OUT, exist_ok=True)
    # the RXDK's lld answers to its name: as lld-link it is the MSVC linker
    lld_link = os.path.join(OUT, "lld-link.exe")
    if not os.path.isfile(lld_link):
        import shutil
        shutil.copyfile(os.path.join(os.path.dirname(xbconfig.clang()), "lld.exe"), lld_link)
    data = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "data"))
    cmd = [xbconfig.clang(), "--target=i686-pc-windows-msvc", "-O2", "-g",
           "-fms-extensions", "-std=gnu17", "-w", "-include", os.path.join(HERE, "shim", "prelude.h"),
           "-I", os.path.join(HERE, "shim"), "-I", P, "-I", os.path.join(ROOT, "src")]
    # clang's own stdarg.h / vadefs first: the Windows SDK's x86 va_start is
    # pointer arithmetic on the parameters, which clang's optimiser breaks
    res = glob.glob(os.path.join(xbconfig.llvm_root(), "lib", "clang", "*", "include"))
    cmd += ["-isystem", sorted(res)[-1]]
    for i in inc:
        cmd += ["-isystem", i]
    cmd += ["-DTARGET_XBOX=1", "-DAICA_HOST_TEST=1", "-D__BEGIN_DECLS=", "-D__END_DECLS=",
            "-D_CRT_SECURE_NO_WARNINGS", '-DXBOX_DATA_ROOT="%s"' % data.replace("\\", "\\\\")]
    objs = []
    for src in SOURCES:
        obj = os.path.join(OUT, os.path.splitext(os.path.basename(src))[0] + ".obj")
        r = subprocess.run(cmd + ["-c", src, "-o", obj], capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        if r.returncode != 0:
            sys.stdout.write(r.stderr[-4000:])
            raise SystemExit("build_audio_test: %s failed" % os.path.basename(src))
        objs.append(obj)
    link = [lld_link, "/nologo", "/debug", "/subsystem:console", "/machine:x86",
            "/out:" + os.path.join(OUT, "audio_test.exe")] + objs
    link += ["/libpath:" + l for l in lib] + ["libcmt.lib", "libucrt.lib", "libvcruntime.lib",
                                              "kernel32.lib", "oldnames.lib"]
    r = subprocess.run(link, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        sys.stdout.write(r.stdout[-4000:] + r.stderr[-4000:])
        raise SystemExit("build_audio_test: link failed")
    print("build_audio_test: %s" % os.path.join(OUT, "audio_test.exe"))


if __name__ == "__main__":
    main()
