# mocorboy

A Game Boy and Game Boy Color emulator for the Ruio F286 feature phone (MocorOS, ARM Cortex-A7). It runs as an app under
the stock firmware, next to the normal phone and SMS stack, through the
[mocor-sdk](https://github.com/openfeaturephone/mocor-sdk) app loader. The emulation core is
[Walnut-CGB](https://github.com/Mr-PauI/Walnut-CGB) and the sound chip is minigb_apu.

You supply your own games. None are included.

## What works

Measured on my F286 with a 1 MB Game Boy game (MBC3 with battery RAM) using the app's speed display:

- 98-100% of full speed, with about 30-38 frames per second drawn and 10-43% CPU. The game keeps real time and drops
  drawn frames rather than slowing down.
- Keys, leaving and starting again, and games from the SD card.
- Saves: a small test cart read its battery RAM back on each start, and its clock came back with the time that had
  passed added on.
- Sound through the firmware's PCM stream, with about 0.1 s queued by the app's own count. It picks speaker or wired
  headphones from the firmware's headset check.
- The game's intro running for over 100 seconds with no keys pressed: the screen stays on, with no fault. I haven't
  done a long play session yet.

That is one phone and one game, so expect rough edges. Game Boy Color games, and ROMs too big for memory, have only run in
my emulator so far, not on the phone.

**Not done:** save states, link cable, MBC7, saves on MBC5 rumble carts, and Bluetooth audio (sound goes to the speaker
or wired headphones). The cart clock doesn't count time while the game is paused inside the app.

## Building

```
pip install ziglang pyelftools
python build.py
```

That writes `out/mocorboy.bin` with the licence files beside it. There is nothing else to fetch: `sdk/` holds the headers,
`rt.c` and `app.ld` from mocor-sdk, and `vendor/` holds the core.

## Installing

You need:

- A phone with the [mocor-sdk](https://github.com/openfeaturephone/mocor-sdk) app loader flashed. Follow its README; this
  repo doesn't touch the firmware.
- A microSD card in the phone. The phone calls it `E:` (internal storage is `D:`).
- `out/mocorboy.bin` from the build above, or from a release.
- Your own games, as `.gb`, `.gbc` or `.cgb` files.

The card ends up like this:

```
E:\
  apps\
    2.bin            out/mocorboy.bin, renamed
  roms\
    yourgame.gb
```

The slot number is up to you: the loader's launcher runs `E:\apps\1.bin` to `9.bin` for keys 1-9. The
[f286-launcher](https://github.com/openfeaturephone/f286-launcher) menu lists slot 2 as "Gameboy".

### With a card reader

1. Turn the phone off and take the card out.
2. On the PC, make folders called `apps` and `roms` at the top of the card if they aren't there.
3. Copy `out/mocorboy.bin` into `apps` as `2.bin`. If there is already a `2.bin`, copy it somewhere first.
4. Copy your games into `roms`.
5. Put the card back and turn the phone on.

### Over USB with f286dbg

If the phone runs the [f286dbg](https://github.com/openfeaturephone/f286dbg) agent, you can leave the card in. Check what is
already in the slot, and back it up if there is something:

```
python ../f286dbg/tools/f286.py ls "E:\apps\*"
python ../f286dbg/tools/f286.py get "E:\apps\2.bin" slot2.old.bin
```

Then copy the app and a game:

```
python ../f286dbg/tools/f286.py mkdir "E:\apps"
python ../f286dbg/tools/f286.py put out/mocorboy.bin "E:\apps\2.bin"
python ../f286dbg/tools/f286.py mkdir "E:\roms"
python ../f286dbg/tools/f286.py put yourgame.gb "E:\roms\yourgame.gb"
```

To try it without installing, use `python ../f286dbg/tools/f286.py run out/mocorboy.bin`.

### Starting it

Open Apps -> Facebook and press 2. The app lists the games in `E:\roms\`, then `D:\roms\` if the card has none. Pick one
with the D-pad and OK.

### Saves

Saves go beside the game, holding what a real cartridge keeps:

- `<name>.sav`: the battery RAM, in the raw format other emulators use, so you can copy saves to and from a PC emulator.
  Written 3 seconds after the game stops changing it and again on leaving, with `<name>.sa1` written first as a backup.
- `<name>.rtc`: the clock on MBC3 clock carts. The time since it was saved is added on load, as the cart's battery would.

`gbc.cfg` in the same folder remembers the sound level. Leave with red so the save is written before the app closes.

### Updating or removing

To update, copy the new `mocorboy.bin` over `E:\apps\2.bin`; saves are kept. To remove it, delete that file. The games and
saves in `roms` stay until you delete them.

## Keys

| Game Boy | Phone |
|---|---|
| D-pad | D-pad or 2, 4, 6, 8 |
| A | OK, 5, 3, 9 |
| B | Green, 1, 7 |
| Start | Left soft key, `*` |
| Select | `#`, 0 |
| Menu | Right soft key |
| Save and leave | Red |

The menu has resume, save, restart, sound level, smoothing and a speed display. The display reads like `33f 100% 21c`: frames
drawn per second, percent of full game speed, percent of CPU time used by the app. Above it, `q110 p9` is the sound queued in
ms and the firmware's pulls per second.

## How it fits on the phone

The picture is scaled 3:2 to 240x216, under the stock status bar. The ROM goes in one heap block, 16 KB per page, up to free
heap minus 1 MB. A ROM that fits is loaded on the loading screen; a larger one is paged in on demand, which stalls the game
for each miss. An MBC3 clock with no `.rtc` file yet starts from the phone's clock.

The app keeps the screen on during play. After 5 minutes with no key press it lets the screen sleep, and the game pauses
until the screen is back on.

The image has no writable globals, floating point or NEON, and is built with strict alignment, because an unaligned access
resets this phone. Each timer tick stops after 10 ms of work to keep the phone's single UI task responsive.

Sound is 32768 Hz mono. The firmware pulls samples in its audio task and would happily buffer about a second, which is far
too late for a game, so the app primes that buffer with silence, waits for it to drain, then feeds at the real rate. This
sound path has no model in my emulator, where a stream hangs in the firmware, so it has only been tested on the phone. Set
sound off (a `gbc.cfg` holding one zero byte) to run under emulation.

## Tests

`tests/check_gbc.py ROMDIR` builds the core for the PC and runs it, including a note of known pitch and the clock's carry. ROMDIR holds test ROMs you download yourself:
`cgb-acid2.gbc` and `dmg-acid2.gb` with their reference PNGs, and `cpu_instrs.gb`. It needs numpy and Pillow as well.
`tests/check_bin.py` checks the built image for VFP and NEON instructions (needs capstone).
`tests/make_paging_rom.py OUT.gb` writes an 8 MB test ROM, made here and holding no game code, for the paging and save paths.

## Licence

MIT, see `LICENSE`. Third-party code and its notices are in `THIRD_PARTY_NOTICES.md`; keep both files with any copy you pass
on, including the binary on its own.
