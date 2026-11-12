# Chess Online: asset licences

Built by `tools/chessonline/make_assets.py` on 2026-11-10. Originals are kept unchanged in
`evidence/chessonline/open-assets/`. Nothing here comes from Coolmath's game. Personal use only; not distributed.

## Originals

| Kind | File | Author | Licence | Source | sha256 | Bytes |
|---|---|---|---|---|---|---|
| licence list | `lila-COPYING.md` | Lichess | AGPL-3.0-or-later (code) | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/COPYING.md | `ad0355db9ccd9ec0` | 11180 |
| piece | `merida-wP.svg` | Armando Hernandez Marroquin | GPL-2.0-or-later | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/public/piece/merida/wP.svg | `2d49d36334cd2836` | 2026 |
| piece | `merida-wN.svg` | Armando Hernandez Marroquin | GPL-2.0-or-later | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/public/piece/merida/wN.svg | `081d2ca7d7a168fc` | 3945 |
| piece | `merida-wB.svg` | Armando Hernandez Marroquin | GPL-2.0-or-later | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/public/piece/merida/wB.svg | `6de6976b55afe257` | 4500 |
| piece | `merida-wR.svg` | Armando Hernandez Marroquin | GPL-2.0-or-later | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/public/piece/merida/wR.svg | `e278fefe31910848` | 977 |
| piece | `merida-wQ.svg` | Armando Hernandez Marroquin | GPL-2.0-or-later | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/public/piece/merida/wQ.svg | `af2097f7174a0a02` | 4120 |
| piece | `merida-wK.svg` | Armando Hernandez Marroquin | GPL-2.0-or-later | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/public/piece/merida/wK.svg | `3d029fd614be242e` | 3459 |
| piece | `merida-bP.svg` | Armando Hernandez Marroquin | GPL-2.0-or-later | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/public/piece/merida/bP.svg | `09fe0648e683e4f2` | 933 |
| piece | `merida-bN.svg` | Armando Hernandez Marroquin | GPL-2.0-or-later | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/public/piece/merida/bN.svg | `16068e0d7f8bf3de` | 2991 |
| piece | `merida-bB.svg` | Armando Hernandez Marroquin | GPL-2.0-or-later | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/public/piece/merida/bB.svg | `5fd6779102126554` | 3070 |
| piece | `merida-bR.svg` | Armando Hernandez Marroquin | GPL-2.0-or-later | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/public/piece/merida/bR.svg | `e76c948d92145b5e` | 990 |
| piece | `merida-bQ.svg` | Armando Hernandez Marroquin | GPL-2.0-or-later | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/public/piece/merida/bQ.svg | `5c5bf0da5453c6fe` | 2554 |
| piece | `merida-bK.svg` | Armando Hernandez Marroquin | GPL-2.0-or-later | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/public/piece/merida/bK.svg | `6838fd128b1f2fc4` | 4539 |
| board | `maple.jpg` | the lila authors and pirouetti | AGPL-3.0-or-later | https://github.com/lichess-org/lila/blob/87e973ad65489975caf55c440537ea317f40a242/public/images/board/maple.jpg | `bb87290d35132f95` | 55733 |
| font | `Figtree[wght].ttf` | The Figtree Project Authors | OFL-1.1 | https://github.com/google/fonts/blob/5bb28733cec313b63fbd415b574b5ae35c8aa8b8/ofl/figtree/Figtree[wght].ttf | `26ad3db9b31ff7dd` | 62712 |
| font licence | `Figtree-OFL.txt` | The Figtree Project Authors | OFL-1.1 | https://github.com/google/fonts/blob/5bb28733cec313b63fbd415b574b5ae35c8aa8b8/ofl/figtree/OFL.txt | `140d37233e7f3ce7` | 4388 |

## Built files

- `pieces/*.png` (64 px) and `pieces96/*.png` (96 px): the merida SVGs rendered with cairosvg. GPL-2.0-or-later, Armando Hernandez Marroquin.
- `board.png`: Lichess's `maple.jpg` scaled to 512x512. AGPL-3.0-or-later, the lila authors and pirouetti.
- `Figtree-*.ttf`: static instances (wght 400/700/800) of `Figtree[wght].ttf` made with fontTools. SIL OFL 1.1 (`Figtree-OFL.txt`), no Reserved Font Name.
- `logo56.png` and `logo140.png`: the user's own logo (`art/logo-source.webp`) scaled by `tools/chessonline/make_logo_art.py`. Since 0.5.0.
- `sce_sys/` pictures and `splash.png`: since 2026-11-12 from the user's own designs, see "The user's art" below (0.5.0 to 0.8.0 drew them with `make_logo_art.py`; the hashes in the table below are those old files).

| File | sha256 |
|---|---|
| `pieces/wP.png` | `a71518a31985e5e1` |
| `pieces96/wP.png` | `34a0a7206b5f321b` |
| `pieces/wN.png` | `32506e2d450ae87d` |
| `pieces96/wN.png` | `035b907cb9f416b3` |
| `pieces/wB.png` | `71cfc9b3dca34fb5` |
| `pieces96/wB.png` | `4416bf10591d2854` |
| `pieces/wR.png` | `3f69bcb069432d3c` |
| `pieces96/wR.png` | `47e7dee728e60664` |
| `pieces/wQ.png` | `51a9539240d6ccea` |
| `pieces96/wQ.png` | `02bf43261a5a5a22` |
| `pieces/wK.png` | `ee0505118cdc6eeb` |
| `pieces96/wK.png` | `e46d0b3c8b822a0c` |
| `pieces/bP.png` | `3228e276a8f6748e` |
| `pieces96/bP.png` | `01092703cd49a00e` |
| `pieces/bN.png` | `abc5777d783e7f6e` |
| `pieces96/bN.png` | `595f7deae932d946` |
| `pieces/bB.png` | `db4e07eba95f2209` |
| `pieces96/bB.png` | `f9161f078f62ec68` |
| `pieces/bR.png` | `9aa630706b64f380` |
| `pieces96/bR.png` | `31e27d06f8caf6a7` |
| `pieces/bQ.png` | `2b87a3795036909f` |
| `pieces96/bQ.png` | `489e90ee63f698c2` |
| `pieces/bK.png` | `a5d62203146eaac5` |
| `pieces96/bK.png` | `ff2dc2beed03ca6b` |
| `board.png` | `906b59254cdebb61` |
| `Figtree-Regular.ttf` | `988f25224b642861` |
| `Figtree-Bold.ttf` | `b30cbcd35c48c675` |
| `Figtree-ExtraBold.ttf` | `216e615d038e0c3a` |
| `sce_sys/icon0.png` | `476e4c4203925787` |
| `sce_sys/livearea/contents/bg.png` | `006c5ef20ed3cb20` |
| `sce_sys/livearea/contents/startup.png` | `68264cb540d93aff` |
| `splash.png` | `f0ef1a1d6dc504e6` |

## Added in 0.8.0 (2026-11-12)

| Kind | File | Author | Licence | Source |
|---|---|---|---|---|
| font | `fonts/NotoSans-Regular.ttf`, `fonts/NotoSans-Bold.ttf` | The Noto Project Authors | OFL-1.1 (`fonts/NotoSans-OFL.txt`) | Noto Sans 2.015 (notofonts/latin-greek-cyrillic), from this PC's Windows fonts folder, unchanged |
| font | `fonts/NotoSans{JP,KR,SC,TC}-{Regular,Bold,ExtraBold}.ttf` | Adobe, The Noto Project Authors | OFL-1.1 (`fonts/NotoSansCJK-OFL.txt`; Reserved Font Name 'Source' only) | static wght 400/700/800 instances of google/fonts c30b221beb0c1da59d563feb91ec8656a1fa972a `ofl/notosans{jp,kr,sc,tc}/NotoSans*[wght].ttf` (kept unchanged in `evidence/chessonline/open-assets/noto-cjk/`), cut down to the app's characters by `tools/chessonline/make_cjk_fonts.py` |
| piece | `pieces62/*.png` (62 px) | Armando Hernandez Marroquin | GPL-2.0-or-later | the merida SVGs above rendered with cairosvg by `tools/chessonline/make_pieces62.py` |
| flag | `flags/{nl,pl,tr,se,dk,no,fi,ru,jp,kr,cn,tw}-{42,72}.png` | ours | | drawn by `tools/chessonline/make_flags.py` (flags are public symbols; Turkey, Korea, China and Taiwan from their official constructions) |

| File | sha256 |
|---|---|
| `fonts/NotoSans-Bold.ttf` | `3a08a47daa00cade` |
| `fonts/NotoSans-OFL.txt` | `6612e095d5f79fc7` |
| `fonts/NotoSans-Regular.ttf` | `f5f552c8c5edb61f` |
| `fonts/NotoSansCJK-OFL.txt` | `1c05c68c34f97084` |
| `fonts/NotoSansJP-Bold.ttf` | `d03346f1d075f743` |
| `fonts/NotoSansJP-ExtraBold.ttf` | `6f8e88d4ff6dd277` |
| `fonts/NotoSansJP-Regular.ttf` | `14836f8ab0dce197` |
| `fonts/NotoSansKR-Bold.ttf` | `56e0603d3722d1e2` |
| `fonts/NotoSansKR-ExtraBold.ttf` | `ddad43267f270889` |
| `fonts/NotoSansKR-Regular.ttf` | `026249575a8e6a01` |
| `fonts/NotoSansSC-Bold.ttf` | `f8e259346b783d04` |
| `fonts/NotoSansSC-ExtraBold.ttf` | `91fe4697b9662255` |
| `fonts/NotoSansSC-Regular.ttf` | `bc4b63120f327223` |
| `fonts/NotoSansTC-Bold.ttf` | `639762a9fe3bef58` |
| `fonts/NotoSansTC-ExtraBold.ttf` | `67fda6b044a0b988` |
| `fonts/NotoSansTC-Regular.ttf` | `b19c52de80bc4796` |
| `pieces62/bB.png` | `5b84213e79437357` |
| `pieces62/bK.png` | `22ae43f6881b9d42` |
| `pieces62/bN.png` | `8e1c19365868cbe0` |
| `pieces62/bP.png` | `962a5bd9f53f6573` |
| `pieces62/bQ.png` | `28e1282dbe192117` |
| `pieces62/bR.png` | `edb214b075222020` |
| `pieces62/wB.png` | `1de43e386d7d2139` |
| `pieces62/wK.png` | `9efb2b36ae65b05d` |
| `pieces62/wN.png` | `e33915082a2f790f` |
| `pieces62/wP.png` | `07ab38dd3bcbe65f` |
| `pieces62/wQ.png` | `50bae1aa78c27746` |
| `pieces62/wR.png` | `42fbd451cfc8a428` |
| `flags/nl-42.png` | `e7fd5a6d94045bef` |
| `flags/nl-72.png` | `062967fbff130697` |
| `flags/pl-42.png` | `07d11819730f5e2f` |
| `flags/pl-72.png` | `2ec1ee416fb96e12` |
| `flags/tr-42.png` | `f12b6820a4f40b11` |
| `flags/tr-72.png` | `5495f52f6dd25def` |
| `flags/se-42.png` | `ea1cf2685261693b` |
| `flags/se-72.png` | `af83723d3491c0e1` |
| `flags/dk-42.png` | `eea3378280885afe` |
| `flags/dk-72.png` | `eabb14671c081f52` |
| `flags/no-42.png` | `b8522a1a80168c8f` |
| `flags/no-72.png` | `576678541a125598` |
| `flags/fi-42.png` | `b51c41544456223e` |
| `flags/fi-72.png` | `efdedb707ace5307` |
| `flags/ru-42.png` | `84fae1e2c4329b58` |
| `flags/ru-72.png` | `9ae13cb19622f95b` |
| `flags/jp-42.png` | `c4bdb564d011ab51` |
| `flags/jp-72.png` | `596c464cace1c89d` |
| `flags/kr-42.png` | `b5ca871121097720` |
| `flags/kr-72.png` | `dec2262a7682ba64` |
| `flags/cn-42.png` | `ffbd81317152178c` |
| `flags/cn-72.png` | `88a197626623cae0` |
| `flags/tw-42.png` | `d7bddc2ec8d9fa1d` |
| `flags/tw-72.png` | `5376f217ef67dfc6` |

## The user's art (2026-11-12)

The user's own three designs, kept unchanged in `art/user-design/` (splash-bg.webp `780c9797...`, startup.webp
`3819a8e1...`, icon.webp `10b22666...`), cropped and scaled by `tools/chessonline/make_user_art.py --app`. The start
button and the icon keep the design's wordmark and logo on a backdrop of our own drawing in the splash's colours.
Home's header pictures are the design's own wordmark (`wordmark-stack.png`; `wordmark.png` is the one-line form, drawn
only if the stacked one is missing) and the user's logo at 90 px (`logo90.png`).

| File | sha256 |
|---|---|
| `splash.png` | `b50c2e2120d0515b` |
| `sce_sys/icon0.png` | `c6479cc1ff72bf49` |
| `sce_sys/livearea/contents/bg.png` | `46753e945f848e47` |
| `sce_sys/livearea/contents/startup.png` | `6ca39748b529c1f9` |
| `wordmark-stack.png` | `6b602cc9ac7da117` |
| `wordmark.png` | `fa16b3c64b7664d2` |
| `logo90.png` | `3d426bb92c87c8b1` |
| `logo56.png` | `d8edda6e7398de41` |
| `logo140.png` | `4bac3ba15b6fc242` |

## Also in the VPK (listed for 1.0.0)

| Kind | File | Author | Licence | Source |
|---|---|---|---|---|
| flag | `flags/{gb,us,br,pt,es,fr,de,it}-{42,72}.png` (0.7.0) | ours | | drawn by `tools/chessonline/make_flags.py` (simplified shapes, no coats of arms); the app draws the 42 px ones |
| CA roots | `cacert.pem` | the certificate authorities | public root certificates | GTS Root R1, R3, R4 and ISRG Root X1, X2 from Ubuntu's ca-certificates (its set has no R2), written by `scripts/chessonline/build.sh` |

| File | sha256 |
|---|---|
| `flags/gb-42.png` | `a9ee855a0bfdde9b` |
| `flags/gb-72.png` | `a0a074175dd34d0d` |
| `flags/us-42.png` | `ac5d5b16f200decd` |
| `flags/us-72.png` | `85e074e90c78f6a6` |
| `flags/br-42.png` | `c9644a46b3fe8074` |
| `flags/br-72.png` | `ea7d19efc11fef3e` |
| `flags/pt-42.png` | `261ad2a59da5bfd3` |
| `flags/pt-72.png` | `13c536068e322936` |
| `flags/es-42.png` | `e6decc948a9ba38e` |
| `flags/es-72.png` | `5b54b2857063a317` |
| `flags/fr-42.png` | `3a5ba3a121df973f` |
| `flags/fr-72.png` | `44af05f1cfa481b4` |
| `flags/de-42.png` | `34199089a4e45032` |
| `flags/de-72.png` | `05391fa80988caf3` |
| `flags/it-42.png` | `c9988ab46276ad3d` |
| `flags/it-72.png` | `bb88fa6a6ecdc786` |
| `cacert.pem` | `71464a2d132b73e4` |
