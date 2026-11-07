"""build.py - build out/mocorboy.bin, a flat Thumb image for the mocor-sdk app loader. Nothing is flashed.

    pip install ziglang pyelftools
    python build.py
"""
import os
import shutil
import subprocess
import sys

from elftools.elf.elffile import ELFFile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
MAX_IMAGE = 512 * 1024     # the loader's limit

CFLAGS = ["-target", "thumb-freestanding-eabi", "-mcpu=cortex_a7", "-mthumb", "-O2",
          "-ffreestanding", "-fno-builtin", "-nostdlib", "-fno-stack-protector",
          "-mno-unaligned-access", "-ffunction-sections", "-fdata-sections", "-fno-unwind-tables",
          "-fno-asynchronous-unwind-tables", "-Wall", "-Wextra", "-Werror",
          "-g",  # zig strips the symtab otherwise; debug info never reaches the .bin
          "-fropi",
          # zig cc ignores -mno-unaligned-access, and an unaligned access resets the phone
          "-Xclang", "-target-feature", "-Xclang", "+strict-align",
          # there is no evidence the kernel saves VFP context for the MMI task
          "-fno-vectorize", "-fno-slp-vectorize", "-mfloat-abi=soft"]
for feature in ("neon", "vfp2", "vfp3", "vfp4", "d32", "fp16"):
    CFLAGS += ["-Xclang", "-target-feature", "-Xclang", "-" + feature]
INCLUDES = ["src", "sdk", "shim", "vendor"]
SOURCES = ["src/gbc.c", "src/gbc_core.c", "sdk/rt.c"]


def zig(*args):
    r = subprocess.run([sys.executable, "-m", "ziglang", *args], capture_output=True, text=True, cwd=HERE)
    if r.returncode:
        sys.exit(f"FAILED: {' '.join(args)}\n{r.stdout}{r.stderr}")


def main():
    os.makedirs(OUT, exist_ok=True)
    elf = os.path.join(OUT, "mocorboy.elf")
    binf = os.path.join(OUT, "mocorboy.bin")
    inc = [x for d in INCLUDES for x in ("-I", os.path.join(HERE, d))]
    zig("cc", *CFLAGS, *inc, "-T", os.path.join(HERE, "sdk", "app.ld"), "-Wl,--gc-sections",
        "-Wl,-e,f286_main", "-o", elf, *SOURCES)
    zig("objcopy", "-O", "binary", elf, binf)

    with open(elf, "rb") as f:
        for s in ELFFile(f).iter_sections():
            if s.name in (".data", ".bss") and s["sh_size"]:
                sys.exit(f"{s.name} is not empty: the loader cannot run this")
    size = os.path.getsize(binf)
    if size > MAX_IMAGE:
        sys.exit(f"image is {size} bytes, the loader takes {MAX_IMAGE}")

    # MIT asks for these notices to travel with every copy, so keep them beside the binary
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copy(os.path.join(HERE, name), OUT)
    print(f"{binf}: {size} bytes")


if __name__ == "__main__":
    main()
