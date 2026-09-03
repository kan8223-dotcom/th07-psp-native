# PSP formal-release font contract

The unified release deliberately ships two OFL-licensed Noto Sans CJK JP
files beside the EBOOT:

| File | Runtime role | Exact release bytes |
| --- | --- | --- |
| `NotoSansJP-Regular.ttf` | fixed PSP-2000/3000/Go payload | 4,491,696 bytes; SHA-256 `6ab1664d8adc20b19237ddc451c94e31f493cb851a1917242debf66f9af6da05` |
| `msgothic-subset.ttf` | fixed PSP-1000 payload | 264,288 bytes; SHA-256 `c456df98197c895c2919a690c737ab3c4a2924799bb4d92fa3a53849c6b56dec` |

`msgothic-subset.ttf` is only the filename expected by the accepted PSP-1000
runtime. Its internal family and legal records identify Noto Sans CJK JP
2.004. It is not Microsoft MS Gothic. A private MS Gothic subset created by
`tools/build_local_msgothic_subset.py` remains local-only and is forbidden in
the repository and every formal archive.

## Deterministic build and audit

The PSP-1000 file uses exactly the 1,190 numeric codepoints in
`src/Th07FontCoverage.hpp`. No original TH07 text or asset is an input to the
formal build.

Install the pinned tool and regenerate or audit with:

```sh
python3 -m pip install fonttools==4.62.1
python3 tools/build_release_noto_subset.py
python3 tools/build_release_noto_subset.py --check
```

The default source is the existing full payload
`psp/assets/NotoSansJP-Regular.ttf`. The tool pins its source SHA-256, every
material fontTools option, the authority count/hash, current upstream OFL
text, legal font metadata, and the expected output size/hash. It builds twice
and requires byte equality. `make psp-release-font-audit`, `make
release-build`, and `make release-audit` run the non-mutating check.

Independent upstream reproduction is possible after separately downloading
the official Sans2.004 `06_NotoSansCJKjp.zip` and verifying the archive and
member hashes in `licenses/NotoSansJP/FONTLOG-TH07PSP.txt`:

```sh
python3 tools/build_release_noto_subset.py \
  --source /path/to/NotoSansCJKjp-Regular.otf \
  --output /tmp/msgothic-subset.ttf
```

Both approved sources produce the same 264,288-byte file. A network download
is never performed by `make` or by the audit.

## Why `kern` remains

A no-layout 257,076-byte experiment changed shaping for many audited strings.
Keeping only horizontal GPOS `kern` produced a 264,288-byte file and matched
the full font for all 1,623 unique real Japanese strings in the local
stock-data audit with HarfBuzz language `ja`. The two mismatches among 1,625
total test rows were synthetic ASCII/name-entry stress rows, not real game
strings. The release artifact therefore keeps `kern` and drops other GPOS and
all GSUB features.

The local audit read original data only in memory and reported counts/hashes;
no original string, image, replay, DAT, or derived XMB asset is committed or
packaged.

## Provenance and remaining gate

The applicable OFL notice, upstream artifact hashes, modification log, and
the one historical limitation for the older full payload are recorded in
`licenses/NotoSansJP/FONTLOG-TH07PSP.txt`. The historical build did not retain
its fontTools version, so the repository does not claim byte reproduction of
the existing full 4.49 MiB payload from its original system TTC. Its bytes are
instead release-pinned, while the new subset is independently reproducible
from the official upstream JP OTF.

Before publication, the exact subset SHA above still needs a real PSP-1000
gate through stage 4 and the fixed stage 1-6 Lunatic replay. The pass must show
correct Japanese text, `provided=1190/1190`, and no allocation, font-coverage,
`REPLAY INVALID`, or fatal error. A PPSSPP pass or a run made with a private
Microsoft-derived subset is not a substitute.
