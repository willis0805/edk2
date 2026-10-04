#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Run the production MMC identification and block I/O host tests on Linux x64.

Requires Python 3 and GCC or Clang (CC may select the compiler). No firmware
hardware, EDK II build, or test framework submodules are required.
"""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    test_dir = Path(__file__).resolve().parent
    root = test_dir.parents[3]
    with tempfile.TemporaryDirectory(prefix="mmc-host-test-") as tmp:
        for source in sorted(test_dir.glob("*HostTest.c")):
            executable = Path(tmp) / source.stem
            command = shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter", "-fshort-wchar", "-DMDEPKG_NDEBUG",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-I" + str(root / "MdePkg/Include"),
                "-I" + str(root / "MdePkg/Include/X64"),
                "-I" + str(root / "EmbeddedPkg/Include"),
                str(source), "-o", str(executable),
            ]
            subprocess.run(command, check=True)
            subprocess.run([str(executable)], check=True)



if __name__ == "__main__":
    main()
