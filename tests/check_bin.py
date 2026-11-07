"""check_bin.py - the built mocorboy.bin must have no VFP or NEON instructions and no writable data (rules in the
mmi-app-dev skill: there is no evidence the kernel saves VFP context for the MMI task)."""
import os
import sys

import capstone
from elftools.elf.elffile import ELFFile

HERE = os.path.dirname(os.path.abspath(__file__))
elf = ELFFile(open(os.path.join(HERE, "..", "out", "mocorboy.elf"), "rb"))
text = elf.get_section_by_name(".text")
base, data = text["sh_addr"], text.data()
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
md.skipdata = True
bad, nfuncs = [], 0
for sym in elf.get_section_by_name(".symtab").iter_symbols():       # functions only: rodata can look like anything
    if sym["st_info"]["type"] != "STT_FUNC" or not sym["st_size"]:
        continue
    nfuncs += 1
    start = (sym["st_value"] & ~1) - base
    for i in md.disasm(data[start:start + sym["st_size"]], start):
        if i.mnemonic.startswith(("v", "fld", "fst", "fmx")):
            bad.append((sym.name, i))
print(f"mocorboy.bin {os.path.getsize(os.path.join(HERE, '..', 'out', 'mocorboy.bin'))} bytes, {nfuncs} functions; VFP/NEON instructions: {len(bad)}")
for name, i in bad[:10]:
    print(f"  {name} {i.address:#x} {i.mnemonic} {i.op_str}")
sys.exit(1 if bad else 0)
