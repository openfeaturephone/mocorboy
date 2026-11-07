"""make_paging_rom.py OUT.gb - an 8 MB MBC5 test ROM for the demand-paging and save paths of mocorboy.bin.
Its program reads the first byte of banks 300, 400 and 500, which lie beyond what fits in memory, and stores them in
cart RAM. The expected save starts with the bytes EXPECT. Written for this project; contains no game code."""
import sys

BANKS = (300, 400, 500)
EXPECT = bytes((b * 7 + 0x5a) & 0xff for b in BANKS)


def build():
    rom = bytearray(512 * 0x4000)
    for page in range(512):
        rom[page * 0x4000] = (page * 7 + 0x5a) & 0xff
    # entry: jp 0x150
    rom[0x100:0x104] = bytes([0x00, 0xc3, 0x50, 0x01])
    rom[0x134:0x13c] = b"PAGETEST"
    rom[0x143] = 0x80                  # CGB
    rom[0x147] = 0x1b                  # MBC5 + RAM + battery
    rom[0x148] = 8                     # 512 banks
    rom[0x149] = 2                     # 8 KiB
    code = bytearray([0x3e, 0x0a, 0xea, 0x00, 0x00])            # ld a,0x0a; ld (0x0000),a  enable RAM
    for i, b in enumerate(BANKS):
        code += bytes([0x3e, b & 0xff, 0xea, 0x00, 0x20])       # ld a,low; ld (0x2000),a
        code += bytes([0x3e, b >> 8, 0xea, 0x00, 0x30])         # ld a,high; ld (0x3000),a
        code += bytes([0xfa, 0x00, 0x40])                       # ld a,(0x4000)
        code += bytes([0xea, i, 0xa0])                          # ld (0xa000+i),a
    code += bytes([0x18, 0xfe])                                 # jr self
    rom[0x150:0x150 + len(code)] = code
    s = 0
    for i in range(0x134, 0x14d):
        s = (s - rom[i] - 1) & 0xff
    rom[0x14d] = s
    return bytes(rom)


if __name__ == "__main__":
    open(sys.argv[1], "wb").write(build())
    print("expect save to start with", EXPECT.hex())
