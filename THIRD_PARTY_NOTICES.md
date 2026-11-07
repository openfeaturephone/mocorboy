# Third-party notices

mocorboy is MIT licensed (`LICENSE`). It contains the code below, all under the MIT licence. Keep this file with every
copy of the source or of `mocorboy.bin`.

| Code | Author | Where |
|---|---|---|
| Walnut-CGB | Mr-PauI (Mr. Paul) | `vendor/walnut_cgb/walnut_cgb.h`, built into the binary |
| Peanut-GB, which Walnut-CGB is based on | Mahyar Koshkouei | same file |
| SameBoy, parts marked by comments in that file | Lior Halphon | same file |
| minigb_apu, based on MiniGBS | Mahyar Koshkouei, Alex Baines | `vendor/minigb_apu/`, built into the binary |
| mocor-sdk (`sdk/`) | willisdylan, same licence and holder as this project | built into the binary |

`vendor/walnut_cgb/walnut_cgb.h` is an unmodified copy of commit `a42c186917516cc9c80515ceae1121600a071628` of
https://github.com/Mr-PauI/Walnut-CGB (sha256 `d4af629f597e8f3748d243d3605457dfcd7fbc867bc251a21f5760f63f0a075a`). Its own
notices are still at the top of the file.

`vendor/minigb_apu/` is an unmodified copy of `examples/sdl2/minigb_apu/` from https://github.com/deltabeard/Peanut-GB, commit
`f51a8aca92e9d95752fe0199bf33cea8d110218a` (the last to touch `minigb_apu.c`).

No game, boot ROM or test ROM is included in this repository or in the binary.

"Game Boy" and "Game Boy Color" are trademarks of Nintendo. This project isn't affiliated with or endorsed by Nintendo.

## Walnut-CGB

From `vendor/walnut_cgb/LICENSE.txt`. The header file gives its notice as "Copyright (c) 2025-2026 Mr. Paul
(https://github.com/Mr-PauI)".

```
MIT License

Copyright (c) 2025 Mr-PauI

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Peanut-GB

From the top of `vendor/walnut_cgb/walnut_cgb.h`.

```
Peanut-GB
Copyright (c) 2018-2023 Mahyar Koshkouei

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.
```

## minigb_apu

`vendor/minigb_apu/minigb_apu.c` carries the copyright lines below and says it is released under the MIT licence, without
quoting the text, and the upstream repository has no licence file at that path. The text below is the standard MIT wording
with those lines.

```
minigb_apu
Copyright (c) 2019 Mahyar Koshkouei <mk@deltabeard.com>
Copyright (c) 2017 Alex Baines <alex@abaines.me.uk>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## SameBoy

`vendor/walnut_cgb/walnut_cgb.h` carries the copyright line below and says the marked code was taken from
https://github.com/LIJI32/SameBoy/ under the MIT licence, without quoting that licence's text. The text below is the
standard MIT wording with that copyright line.

```
SameBoy
Copyright (c) 2015-2019 Lior Halphon

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
