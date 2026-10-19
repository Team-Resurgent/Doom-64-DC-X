#!/usr/bin/env python3
"""Write rxdk.project.json: what Rxdk.Cli builds Doom-64-DC-X from.

The Dreamcast Makefile builds every src/*.c (C_FILES := $(wildcard src/*.c))
and links array_fast_copy.o, its SH-4 assembly copy routines. The Xbox build
compiles the same C files; the assembly's two routines are C in the platform
layer (Platform/xbox/kos_extra_xbox.c), and the platform layer itself
(Platform/xbox/**/*.c, the KallistiOS the game is written against) is added.
Compile settings come from tools/xbox/xbconfig.py.

    python tools/xbox/gen_manifest.py
"""
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xbconfig  # noqa: E402

ROOT = xbconfig.ROOT
PROJECT_NAME = "Doom-64-DC-X"           # the ISO is out/<Config>/XISO/Doom-64-DC-X.iso
# Team Resurgent TitleID-DB: the next after Star-Fox-64-X (54520012)
TITLE_ID = "0x54520013"
TITLE_NAME = "Doom 64"

LIBS_RELEASE = ["libd3d8.lib", "libd3dx8.lib", "libxgraphics.lib", "libdsound.lib",
                "libxapi.lib", "libc.lib", "libcpp.lib", "libkernel.lib"]
LIBS_DEBUG = ["libxbdmd.lib", "libd3d8d.lib", "libd3dx8d.lib", "libxgraphicsd.lib",
              "libdsoundd.lib", "libxapid.lib", "libcd.lib", "libcppd.lib", "libkerneld.lib"]


def game_sources():
    out = sorted(os.path.relpath(p, ROOT).replace("\\", "/")
                 for p in glob.glob(os.path.join(ROOT, "src", "*.c")))
    if not out:
        raise SystemExit("gen_manifest: no src/*.c")
    return out


def platform_sources():
    out = []
    base = os.path.join(ROOT, "Platform", "xbox")
    for dp, dn, fn in os.walk(base):
        dn[:] = sorted(d for d in dn if d != "gen")
        for f in sorted(fn):
            if f.endswith(".c") and not f.endswith(".inc.c"):
                out.append(os.path.relpath(os.path.join(dp, f), ROOT).replace("\\", "/"))
    return sorted(out)


def main():
    game = game_sources()
    sources = platform_sources() + game

    # RXDK's Visual Studio build names each object after its file alone
    seen = {}
    for s in sources:
        b = os.path.basename(s).lower()
        if b in seen:
            raise SystemExit("gen_manifest: %s and %s share a file name; RXDK's Visual Studio "
                             "build would overwrite one object with the other" % (seen[b], s))
        seen[b] = s

    def cfg(debug):
        return {
            "configuration": "debug" if debug else "release",
            # the dashboard icons: BMPs (tools/xbox/make_icons.py) bundled
            # to DXT1 .xbx before the link
            "resources": ["Platform/xbox/titleimage.rdf", "Platform/xbox/saveimage.rdf"],
            "sources": sources,
            "libraries": LIBS_DEBUG if debug else LIBS_RELEASE,
            "includePaths": xbconfig.INCLUDE_PATHS,
            "defines": xbconfig.DEFINES,
            "compileFlags": xbconfig.COMPILE_FLAGS,
            "cppStandard": "c++20",
            "exceptions": False,
            "deployPaths": ["data"],
            "forceCopy": True,
            "createIso": True,
            "outputDir": "out/Debug" if debug else "out/Release",
            "imageBuild": {
                "stackSize": 262144,
                "debug": debug,
                "noLogo": True,
                "noLibWarn": True,
                "testId": TITLE_ID,
                "testName": TITLE_NAME,
                "testVersion": "1",
                "titleImage": "Platform/xbox/gen/titleimage.xbx",
                "defaultSaveImage": "Platform/xbox/gen/saveimage.xbx",
            },
        }

    manifest = {
        "name": PROJECT_NAME,
        "type": "executable",
        "defaultConfiguration": "Release",
        "configurations": {"Debug": cfg(True), "Release": cfg(False)},
    }
    out = os.path.join(ROOT, "rxdk.project.json")
    text = json.dumps(manifest, indent=2) + "\n"
    old = open(out, encoding="utf-8").read() if os.path.isfile(out) else None
    if old != text:            # unchanged, it keeps its time (the RXDK extension watches it)
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print("gen_manifest: %d sources (%d game, %d platform)"
          % (len(sources), len(game), len(sources) - len(game)))


if __name__ == "__main__":
    main()
