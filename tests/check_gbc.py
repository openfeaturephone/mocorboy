"""check_gbc.py - PC tests for gbc_core (the glue and the Walnut-CGB configuration used on the phone).

    python check_gbc.py ROMDIR

ROMDIR holds test ROMs, which are not in this repository:
    cgb-acid2.gbc, cgb-acid2-ref.png   https://github.com/mattcurrie/cgb-acid2 (MIT)
    dmg-acid2.gb,  dmg-acid2-ref.png   https://github.com/mattcurrie/dmg-acid2 (MIT), img/reference-dmg.png
    cpu_instrs.gb                      optional, Blargg's test ROM
Compares the picture with the reference, repeats with a tiny page cache to exercise eviction, and checks the header guards.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))


def build(out):
    exe = os.path.join(out, "host_gbc.exe")
    cmd = [sys.executable, "-m", "ziglang", "cc", "-O2", "-DGBC_HOST", "-Wall", "-Wextra", "-Werror", "-I", os.path.join(ROOT, "vendor"),
           "-I", os.path.join(ROOT, "src"), "-o", exe, os.path.join(HERE, "host_gbc.c"),
           os.path.join(ROOT, "src", "gbc_core.c")]
    subprocess.run(cmd, check=True)
    return exe


def run(exe, rom, frames, out, slots, *keys):
    r = subprocess.run([exe, rom, str(frames), out, str(slots), *keys], capture_output=True, text=True, errors="replace")
    return r.returncode, r.stdout


def native(ppm):
    """The 240x216 nearest-neighbour frame back to 160x144 (columns 3k and 3k+2, rows likewise)."""
    a = np.asarray(Image.open(ppm).convert("RGB")).astype(int)
    keep_x = [x for x in range(240) if x % 3 != 1]
    keep_y = [y for y in range(216) if y % 3 != 1]
    return a[np.ix_(keep_y, keep_x)]


def compare_shades(ppm, ref):
    """An original Game Boy game: our colours differ from the reference's greys, so each colour must map to one grey."""
    got = native(ppm) >> 3
    want = np.asarray(Image.open(ref).convert("L"))
    keys = got[:, :, 0] << 12 | got[:, :, 1] << 6 | got[:, :, 2]
    bad = 0
    for k in np.unique(keys):
        vals, counts = np.unique(want[keys == k], return_counts=True)
        bad += int(counts.sum() - counts.max())
    return bad


def compare(ppm, ref):
    if "dmg" in os.path.basename(ref):
        return compare_shades(ppm, ref)
    got = native(ppm) >> 3
    want = np.asarray(Image.open(ref).convert("RGB")).astype(int) >> 3
    return int((np.abs(got - want).max(axis=2) > 0).sum())


TONE_HZ = 131072 / (2048 - 1750)


def tone_rom():
    """32 KB, no mapper: channel 1 plays one square wave at TONE_HZ, then the program spins."""
    rom = bytearray(0x8000)
    rom[0x100:0x104] = bytes([0x00, 0xc3, 0x50, 0x01])
    rom[0x134:0x138] = b"TONE"
    code = bytearray()
    for reg, val in ((0x26, 0x80), (0x24, 0x77), (0x25, 0x11), (0x11, 0x80), (0x12, 0xf0), (0x13, 1750 & 255), (0x14, 0x80 | (1750 >> 8))):
        code += bytes([0x3e, val, 0xe0, reg])      # ld a,val; ldh (reg),a
    code += bytes([0x18, 0xfe])                    # jr self
    rom[0x150:0x150 + len(code)] = code
    s = 0
    for i in range(0x134, 0x14d):
        s = (s - rom[i] - 1) & 0xff
    rom[0x14d] = s
    return bytes(rom)


def main():
    romdir = sys.argv[1]
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        exe = build(tmp)
        for rom, ref in (("cgb-acid2.gbc", "cgb-acid2-ref.png"), ("dmg-acid2.gb", "dmg-acid2-ref.png")):
            for slots in (16, 4):
                ppm = os.path.join(tmp, "f.ppm")
                rc, out = run(exe, os.path.join(romdir, rom), 120, ppm, slots)
                diff = compare(ppm, os.path.join(romdir, ref)) if rc == 0 else -1
                ok = rc == 0 and diff == 0
                print(f"{'ok  ' if ok else 'FAIL'} {rom} slots={slots}: {diff} px differ; {out.strip().splitlines()[-1]}")
                failed += not ok
        cpu = os.path.join(romdir, "cpu_instrs.gb")
        if os.path.exists(cpu):
            for slots in (64, 4):
                rc, out = run(exe, cpu, 4000, os.path.join(tmp, "c.ppm"), slots)
                ok = rc == 0 and "Passed all tests" in out
                print(f"{'ok  ' if ok else 'FAIL'} cpu_instrs slots={slots}: {out.strip().splitlines()[-1]}")
                failed += not ok

        # malformed headers must be refused before the core indexes its tables with them
        good = bytearray(open(os.path.join(romdir, "cgb-acid2.gbc"), "rb").read())

        def fixsum(b):
            s = 0
            for i in range(0x134, 0x14d):
                s = (s - b[i] - 1) & 0xff
            b[0x14d] = s
        for name, off, val, want in (("bank count", 0x148, 0xff, 2), ("cart type", 0x147, 0xfc, 2), ("ram size", 0x149, 0xee, 4)):
            b = bytearray(good)
            b[off] = val
            fixsum(b)
            path = os.path.join(tmp, "bad.gb")
            open(path, "wb").write(b)
            rc, out = run(exe, path, 1, os.path.join(tmp, "x.ppm"), 4)
            ok = rc == 3 and f"header error {want}" in out
            print(f"{'ok  ' if ok else 'FAIL'} bad {name}: {out.strip()}")
            failed += not ok
        b = bytearray(good)
        b[0x14d] ^= 0xff
        path = os.path.join(tmp, "bad.gb")
        open(path, "wb").write(b)
        rc, out = run(exe, path, 1, os.path.join(tmp, "x.ppm"), 4)
        ok = rc == 3 and "header error 3" in out
        print(f"{'ok  ' if ok else 'FAIL'} bad checksum: {out.strip()}")
        failed += not ok

        # a program of nothing but invalid opcodes must stop with an error, not spin
        b = bytearray(good)
        b[0x100:0x104] = bytes([0xd3, 0xd3, 0xd3, 0xd3])
        open(path, "wb").write(b)
        try:
            r = subprocess.run([exe, path, "5", os.path.join(tmp, "x.ppm"), "4"], capture_output=True, text=True, timeout=20)
            ok = "emulation error 1" in r.stdout
            print(f"{'ok  ' if ok else 'FAIL'} invalid opcode: {r.stdout.strip().splitlines()[0]}")
        except subprocess.TimeoutExpired:
            ok = False
            print("FAIL invalid opcode: hang")
        failed += not ok
        r = subprocess.run([exe, "--rtc"], capture_output=True, text=True)
        ok = "rtc 5 0 0 0 128 halted 1 2 3 4 64" in r.stdout
        print(f"{'ok  ' if ok else 'FAIL'} clock carry and halt: {r.stdout.strip()}")
        failed += not ok
        # sound: a note of known pitch from a tiny program, measured by counting rising zero crossings
        path = os.path.join(tmp, "tone.gb")
        pcm = os.path.join(tmp, "tone.pcm")
        open(path, "wb").write(tone_rom())
        r = subprocess.run([exe, path, "120", os.path.join(tmp, "x.ppm"), "4"], capture_output=True, text=True,
                           errors="replace", env={**os.environ, "GBC_PCM": pcm})
        a = np.fromfile(pcm, dtype=np.int16)
        a = a - int(a.mean())
        hz = int(((a[:-1] < 0) & (a[1:] >= 0)).sum()) * 32768 / len(a)
        ok = r.returncode == 0 and abs(hz - TONE_HZ) < 5 and int(abs(a).max()) > 1000
        print(f"{'ok  ' if ok else 'FAIL'} sound: {hz:.1f} Hz, want {TONE_HZ:.1f}; peak {int(abs(a).max())}")
        failed += not ok
    print("all passed" if not failed else f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
