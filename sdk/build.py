#!/usr/bin/env python3
"""Build Glass Spheres with Akai's MPC Plugin SDK.

    python sdk/build.py [build.py options, e.g. --target arm32]

Runs <sdk>/tools/build.py on this folder (sdk/) with the include path the engine sources need. The SDK is found at
$MPC_SDK, else ../../mpc-plugin-builder-sdk next to this repo. Output: sdk/build/Padbangers - VST - Glass Spheres/.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SDK = os.environ.get("MPC_SDK") or os.path.join(REPO, "..", "mpc-plugin-builder-sdk")
INCLUDES = [os.path.join(REPO, "third_party", "eurorack")]

build_py = os.path.join(SDK, "tools", "build.py")
if not os.path.isfile(build_py):
    sys.exit("MPC Plugin SDK not found at %s (set MPC_SDK)" % os.path.normpath(SDK))
env = dict(os.environ)
# The SDK takes no include flags; clang (zig c++) and gcc both read CPLUS_INCLUDE_PATH.
env["CPLUS_INCLUDE_PATH"] = os.pathsep.join(INCLUDES + [p for p in [env.get("CPLUS_INCLUDE_PATH")] if p])
sys.exit(subprocess.call([sys.executable, build_py, HERE] + sys.argv[1:], env=env))
