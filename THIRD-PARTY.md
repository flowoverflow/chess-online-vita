# Third-party software in the VPK

The app links these libraries statically. They come as VitaSDK 2026.08 vdpm packages, installed with
`vdpm install curl libvita2d`.

| Library | vdpm package | Licence | Upstream |
|---|---|---|---|
| libcurl | curl 8.17.0-3 | curl licence (MIT-style), `third_party/licenses/curl.txt` | https://curl.se/ |
| OpenSSL | openssl-1.1.1 1.1.1.r24282.g0a851f7-2 | OpenSSL 1.1.1 is under the dual OpenSSL and SSLeay licence. The SDK ships `third_party/licenses/openssl-Apache-2.0.txt` with the package | https://www.openssl.org/ |
| zlib | zlib | zlib licence, `third_party/licenses/zlib.txt` | https://zlib.net/ |
| zstd | zstd 1.5.7-1 | BSD-3-Clause (or GPL-2.0) | https://github.com/facebook/zstd |
| bzip2 | bzip2 1.0.8-1 | bzip2 licence (BSD-style) | https://sourceware.org/bzip2/ |
| FreeType | freetype 2.14.3-1 | FreeType Licence (FTL) or GPL-2.0 | https://freetype.org/ |
| libpng | libpng16 | PNG Reference Library licence | http://www.libpng.org/pub/png/libpng.html |
| libjpeg | libjpeg-turbo (as packaged) | IJG licence and BSD-3-Clause | https://libjpeg-turbo.org/ |
| vita2d | libvita2d 0.0.0.r188.ga8f15ab-1 | MIT | https://github.com/xerpi/libvita2d |
| VitaSDK stubs and newlib | VitaSDK 2026.08 | their own licences | https://vitasdk.org/ |

Credits that these licences ask for in documentation:

- This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit
  (http://www.openssl.org/). It also includes cryptographic software written by Eric Young (eay@cryptsoft.com).
- Portions of this software are copyright © The FreeType Project (https://freetype.org). All rights reserved.
- This software is based in part on the work of the Independent JPEG Group.

`third_party/licenses/` holds the licence files exactly as VitaSDK's vdpm ships them, in `share/vdpm/licenses`. For
the other libraries, see their upstream projects. The pieces, the board, the fonts and the pictures are listed in
`res/LICENSES.md`.
