# Third-Party Notices

Patchy's own application code is covered by the [MIT License](LICENSE). This
file identifies the terms for third-party code and assets in the source tree,
desktop and web packages, optional downloads, and test fixtures. Each entry
identifies the component and where it is used.

For example, Patchy uses the [G'MIC and CImg sources](#gmic-and-cimg-desktop-builds)
in `src/filters/gmic/` under CeCILL-C, including the changes Patchy makes to
those sources. It uses [LibRaw](#libraw) in `src/formats/libraw/` under
CDDL 1.0. The [Dink Smallwood theme artwork](#bundled-ui-themes) includes
original game data under zlib-style terms and HD artwork under the Dink
Smallwood HD License.

## Qt 6

The Windows packages include the following Qt 6 modules and their runtime
dependencies from the Qt installation used for the build.

- Qt Core
- Qt GUI
- Qt Network
- Qt OpenGL
- Qt Widgets
- Qt QML and Qt Quick
- Qt PrintSupport
- Qt SVG
- Qt ImageFormats
- Qt PDF (optional; present when the build's Qt carries the add-on)

Qt PDF bundles a snapshot of PDFium, which is distributed under a BSD 3-Clause
license together with Apache License 2.0 components. Patchy uses it only to
render PDF pages to images when opening a `.pdf` file; PDF writing goes through
Qt GUI's own QPdfWriter and needs no part of it. The full component list and
license text ship in `licenses/qt/qtpdf-<version>.spdx`.

The package also includes the Qt plugins needed by the current app:

- `platforms/qwindows.dll`
- `styles/qmodernwindowsstyle.dll`
- `iconengines/qsvgicon.dll`
- `imageformats/qjpeg.dll`
- `imageformats/qsvg.dll`
- `imageformats/qtiff.dll`
- `imageformats/qwebp.dll`

Qt is available under a commercial Qt license or under open-source licenses.
The Windows packages use dynamic Qt DLLs. The SPDX notices under `licenses/qt/`
identify the Qt build, its third-party components, and their license texts.
The Qt QML and Qt Quick notices are in `qtdeclarative-<version>.spdx`.

The macOS app's Qt cocoa platform plugin (`platforms/libqcocoa.dylib`) is a
modified build of Qt 6.8.3, used under the GNU Lesser General Public License
v3. The complete change is
`packaging/macos/qt-patches/qtbase-6.8.3-cocoa-a11y-cell-retain.patch` in this
repository, applied to the unmodified qtbase 6.8.3 source release by
`scripts/setup-qt-cocoa-patch-mac.sh`. It fixes an accessibility crash (Qt bug
QTBUG-134784). The other Qt libraries and plugins are the unmodified Qt builds.

## Microsoft Visual C++ Runtime DLLs

The Windows package includes the app-local Microsoft Visual C++ runtime DLLs
(the OpenMP runtime `vcomp140.dll` among them, used by the G'MIC engine)
from the local Visual Studio redistributable CRT directory. These DLLs are
provided under Microsoft's runtime redistribution terms.

## Noto Naskh Arabic

Patchy includes Noto Naskh Arabic Regular and Bold from the Noto Arabic fonts
project for Photoshop text-layer compatibility. The font files are distributed
under the SIL Open Font License, Version 1.1; the license text is included at
`fonts/noto_naskh_arabic/OFL.txt` in the release package.

## Bundled fonts (web build only)

The WebAssembly build bundles a set of open fonts under `third_party/fonts-web/`
because a browser exposes no system fonts to the app. Desktop packages do not
include these files. Every family is distributed under the SIL Open Font
License, Version 1.1, and each family directory carries its own license text as
`OFL.txt`, packaged into the web build's preloaded data file. All files are
unmodified static-instance builds fetched from the projects' official
repositories on 2026-07-31 unless noted below:

| Family | Files | Source |
| --- | --- | --- |
| Liberation Sans / Serif / Mono | 12 (R/B/I/BI each) | github.com/liberationfonts/liberation-fonts, release 2.1.5 |
| Carlito | 4 (R/B/I/BI) | github.com/google/fonts, `ofl/carlito` |
| Noto Sans | 4 (R/B/I/BI) | github.com/notofonts/notofonts.github.io, `fonts/NotoSans/hinted/ttf` |
| Noto Serif | 4 (R/B/I/BI) | github.com/notofonts/notofonts.github.io, `fonts/NotoSerif/hinted/ttf` |
| Noto Sans JP | 2 (Regular, Bold) | Google Fonts static TTF builds (fonts.gstatic.com via the css2 API; the TrueType-outline builds Google serves for JP web use, preferred over the noto-cjk CFF OTFs for FreeType rendering consistency) |
| Noto Sans SC | 2 (Regular, Bold) | Google Fonts static TTF builds (fonts.gstatic.com via the css2 API), fetched September 15, 2026 |
| Noto Sans TC | 2 (Regular, Bold) | Google Fonts static TTF builds (fonts.gstatic.com via the css2 API), fetched September 15, 2026 |
| Nanum Gothic | 2 (Regular, Bold) | [google/fonts, ofl/nanumgothic](https://github.com/google/fonts/tree/133ccbee9a8b408eb71f31a36ccb9116f5c695ad/ofl/nanumgothic), revision `133ccbee9a8b408eb71f31a36ccb9116f5c695ad`, fetched October 2, 2026; unmodified static TTFs, copyright NHN Corporation, designed by Sandoll Communications Inc.; SIL OFL 1.1 in `third_party/fonts-web/nanum_gothic/OFL.txt` |
| Montserrat | 2 (Regular, Bold) | github.com/JulietaUla/Montserrat, `fonts/ttf` |
| Oswald | 2 (Regular, Bold) | github.com/googlefonts/OswaldFont, `fonts/ttf` |
| Caveat | 2 (Regular, Bold) | github.com/googlefonts/caveat, `fonts/ttf` |
| Abril Fatface | 1 (Regular) | github.com/google/fonts, `ofl/abrilfatface` |
| Pacifico | 1 (Regular) | github.com/google/fonts, `ofl/pacifico` |
| Lobster | 1 (Regular) | github.com/google/fonts, `ofl/lobster` |

## miniz

`src/formats/miniz/` vendors miniz 3.0.2 (https://github.com/richgel999/miniz),
the single-source zlib/deflate implementation used by the Aseprite file format's
compressed cels. MIT License; the license text is included at
`src/formats/miniz/LICENSE`.

The archive routines in `miniz.c` also carry this notice.

```text
Copyright 2016 Martin Raiber
```

```text
Copyright 2013-2014 RAD Game Tools and Valve Software
Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## stb_image

`src/formats/stb/` vendors stb_image 2.30 (https://github.com/nothings/stb),
compiled decode-only and restricted to JPEG and PNG. It decodes the original
image files that Affinity .af documents embed for placed/opened pictures (the
document stores only a mip pyramid plus the untouched original). stb_image is
dual-licensed MIT / public domain; Patchy uses it under the MIT license,
included at `src/formats/stb/LICENSE`.

## stb_dxt

`src/formats/stb/stb_dxt.h` vendors stb_dxt 1.12 (https://github.com/nothings/stb), the
BC1/BC3 (DXT1/DXT5) block encoder behind the DDS writer, built with floating-point
contraction disabled so every toolchain produces the same blocks. stb_dxt is
dual-licensed MIT / public domain; Patchy uses it under the MIT license, included at
`src/formats/stb/LICENSE`.

The following notice covers both stb_image and stb_dxt.

```text
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
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

## bcdec

`src/formats/bcdec/` vendors bcdec 0.985 by Sergii "iOrange" Kudlai
(https://github.com/iOrange/bcdec), the BC1-BC7 (BC6H included) block decoder behind
the DDS reader, compiled with `BCDEC_BC4BC5_PRECISE` for the signed BC4/BC5 variants.
bcdec is dual-licensed MIT / Unlicense; Patchy uses it under the MIT license, included
at `src/formats/bcdec/LICENSE`.

```text
ALTERNATIVE A - MIT License

Copyright (c) 2022 Sergii Kudlai

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

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

## bc7enc

`src/formats/bc7enc/` vendors bc7enc by Richard Geldreich, Jr.
(https://github.com/richgel999/bc7enc), the BC7 block encoder (modes 1 and 6) behind the
DDS writer, built with floating-point contraction disabled so every toolchain produces the
same blocks. bc7enc is dual-licensed MIT / public domain; Patchy uses it under the MIT
license, included at `src/formats/bc7enc/LICENSE`.

```text
ALTERNATIVE A - MIT License
Copyright(c) 2020 Richard Geldreich, Jr.
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files(the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions :
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Zstandard

`src/formats/zstd/` vendors the decompression half of Zstandard 1.5.7
(https://github.com/facebook/zstd, Copyright Meta Platforms, Inc. and
affiliates) as the project's generated single-file decoder (`zstddeclib.c`
from `build/single_file_libs`), used to read the compressed streams inside
Affinity .af documents. Zstandard is dual-licensed BSD-3-Clause / GPLv2;
Patchy uses it under the BSD-3-Clause license, included at
`src/formats/zstd/LICENSE`. No compression code is vendored.

The single-file decoder also carries these notices for xxHash and decoder sources.

```text
Copyright (c) Yann Collet - Meta Platforms, Inc
Copyright (c) Yann Collet, Meta Platforms, Inc. and affiliates.
```

```text
BSD License

For Zstandard software

Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

 * Neither the name Facebook, nor Meta, nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Little CMS

`src/color/lcms2/` vendors the Little CMS 2.17 core library
(https://github.com/mm2/Little-CMS), used to convert CMYK PSD documents through
their embedded ICC color profile on import and to convert single colors through
the CMYK profile chosen in Preferences. Patchy includes the core library under its MIT license and the notice in
`cmssm.c` below. The optional speed plugins are omitted.

The core source headers carry the following copyright notices. The project license is
`src/color/lcms2/LICENSE`.

```text
Copyright (c) 1998-2024 Marti Maria Saguer
Copyright (c) 1998-2025 Marti Maria Saguer
```

```text
MIT License

Copyright (c) 2023 Marti Maria Saguer

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the 
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to 
permit persons to whom the Software is furnished to do so, subject
to the following conditions:

The above copyright notice and this permission notice shall be 
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

The closest-point routine in `src/color/lcms2/cmssm.c` carries this notice.

```text
Copyright 2001, softSurfer (www.softsurfer.com)
This code may be freely used and modified for any purpose
providing that this copyright notice is included with it.
SoftSurfer makes no warranty for this code, and cannot be held
liable for any real or imagined damage resulting from its use.
Users of this code must verify correctness for their application.
```

## libheif (web build only)

`src/formats/libheif/` vendors libheif 1.23.1
(https://github.com/strukturag/libheif), Copyright Dirk Farin and
contributors, under the GNU Lesser General Public License version 3 or later.
The complete LGPL/GPL license text is included at
`src/formats/libheif/COPYING` and is staged with the web application as
`libheif-COPYING.txt`.

Patchy fetched the official `v1.23.1` source archive from
`https://github.com/strukturag/libheif/archive/refs/tags/v1.23.1.zip`. Its
SHA-256 is
`F2303C92396B672D8B1BF8156DC1FC1715CC5D4AE1F147D205EA957DB334B2FA`.
Patchy's local change to `libheif/plugins/decoder_webcodecs.cc` adds runtime
capability/error handling, safe asynchronous input copies, decoder/frame
lifetime cleanup, decoded-buffer bounds checks, and support for odd-sized and
multi-slice images. That modified source is included in this repository.

libheif is linked only into Patchy's WebAssembly browser application as an
HEIF container parser and bridge to the browser's WebCodecs HEVC decoder.
The build disables libde265, x265, FFmpeg, every other software codec backend,
every compressed-codec encoder backend, dynamic plug-in loading, and the
experimental uncompressed codec. Patchy does not distribute an HEVC decoder
or encoder.

The complete corresponding application source, including the modified libheif,
is available at <https://github.com/SethRobinson/Patchy>. The pinned toolchain,
configuration, and rebuild instructions are in `docs/wasm.md`; they permit a
recipient to replace libheif with a modified interface-compatible version and
relink the WebAssembly application. Release tags identify the source for each
deployed version.

## LibRaw

`src/formats/libraw/` vendors LibRaw 0.22.1 (https://www.libraw.org,
Copyright (C) 2008-2025 LibRaw LLC), the camera raw decoder and develop
pipeline behind opening CR2/CR3/NEF/ARW/RAF/DNG and other camera raw files.
LibRaw is dual-licensed and may be used under either the GNU LGPL 2.1 or the
CDDL 1.0; Patchy uses it under the CDDL 1.0. Only the stock release tarball is
vendored: the separate LibRaw demosaic-pack repositories are GPL-licensed and
are not included. LibRaw itself incorporates code from dcraw (Dave Coffin,
non-restricted portions), the BSD-licensed DCB demosaic and FBDD denoise
(Jacek Gozdz), the BSD-licensed X3F tools (Roland Karlsson), and MIT-licensed
Adobe DNG SDK fragments. The license texts are included at
`src/formats/libraw/LICENSE.CDDL`, `src/formats/libraw/LICENSE.LGPL`, and
`src/formats/libraw/COPYRIGHT`.

To obtain the LibRaw source used in a binary release, download the source archive for
that version from [Patchy releases](https://github.com/SethRobinson/Patchy/releases)
and open `src/formats/libraw/`. The CDDL license text in that directory follows.

```text
COMMON DEVELOPMENT AND DISTRIBUTION LICENSE (CDDL) Version 1.0

1. Definitions.

1.1. Contributor means each individual or entity that creates or
contributes to the creation of Modifications.

1.2. Contributor Version means the combination of the Original
Software, prior Modifications used by a Contributor (if any),
and the Modifications made by that particular Contributor.

1.3. Covered Software means (a) the Original Software, or (b)
Modifications, or (c) the combination of files containing
Original Software with files containing Modifications, in each
case including portions thereof.

1.4. Executable means the Covered Software in any form other
than Source Code.

1.5. Initial Developer means the individual or entity that first
makes Original Software available under this License.

1.6. Larger Workmeans a work which combines Covered Software or
portions thereof with code not governed by the terms of this
License.

1.7. License means this document.

1.8. Licensable means having the right to grant, to the maximum
extent possible, whether at the time of the initial grant or 
subsequently acquired, any and all of the rights conveyed herein.

1.9. Modifications means the Source Code and Executable form of
any of the following: A. Any file that results from an addition
to, deletion from or modification of the contents of a file
containing Original Software or previous Modifications; B. Any
new file that contains any part of the Original Software or
previous Modification; or C. Any new file that is contributed or
otherwise made available under the terms of this License.

1.10. Original Software means the Source Code and Executable
form of computer software code that is originally released under
this License.

1.11. Patent Claims means any patent claim(s), now owned or
hereafter acquired, including without limitation, method,
process, and apparatus claims, in any patent Licensable by
grantor.

1.12. Source Code means (a) the common form of computer software
code in which modifications are made and (b) associated
documentation included in or with such code.

1.13. You (or Your) means an individual or a legal entity
exercising rights under, and complying with all of the terms of,
this License. For legal entities, You includes any entity which
controls, is controlled by, or is under common control with You.
For purposes of this definition, control means (a) the power,
direct or indirect, to cause the direction or management of such
entity, whether by contract or otherwise, or (b) ownership of
more than fifty percent (50%) of the outstanding shares or
beneficial ownership of such entity.

2. License Grants.

2.1. The Initial Developer Grant. Conditioned upon Your
compliance with Section 3.1 below and subject to third party
intellectual property claims, the Initial Developer hereby
grants You a world-wide, royalty-free, non-exclusive license:

(a) under intellectual property rights (other than patent or
trademark) Licensable by Initial Developer, to use, reproduce,
modify, display, perform, sublicense and distribute the Original
Software (or portions thereof), with or without Modifications,
and/or as part of a Larger Work; and

(b) under Patent Claims infringed by the making, using or
selling of Original Software, to make, have made, use, practice,
sell, and offer for sale, and/or otherwise dispose of the
Original Software (or portions thereof);

(c) The licenses granted in Sections 2.1(a) and (b) are
effective on the date Initial Developer first distributes or
otherwise makes the Original Software available to a third party
under the terms of this License;

(d) Notwithstanding Section 2.1(b) above, no patent license is
granted: (1) for code that You delete from the Original
Software, or (2) for infringements caused by: (i) the
modification of the Original Software, or (ii) the combination
of the Original Software with other software or devices.

2.2. Contributor Grant. Conditioned upon Your compliance with
Section 3.1 below and subject to third party intellectual
property claims, each Contributor hereby grants You a
world-wide, royalty-free, non-exclusive license:

(a) under intellectual property rights (other than patent or
trademark) Licensable by Contributor to use, reproduce, modify,
display, perform, sublicense and distribute the Modifications
created by such Contributor (or portions thereof), either on an
unmodified basis, with other Modifications, as Covered Software
and/or as part of a Larger Work; and

(b) under Patent Claims infringed by the making, using, or
selling of Modifications made by that Contributor either alone
and/or in combination with its Contributor Version (or portions
of such combination), to make, use, sell, offer for sale, have
made, and/or otherwise dispose of: (1) Modifications made by
that Contributor (or portions thereof); and (2) the combination
of Modifications made by that Contributor with its Contributor
Version (or portions of such combination).

(c) The licenses granted in Sections 2.2(a) and 2.2(b)
areeffective on the date Contributor first distributes or
otherwise makes the Modifications available to a third party.

(d) Notwithstanding Section 2.2(b) above, no patent license is
granted: (1) for any code that Contributor has deleted from the
Contributor Version; (2) for infringements caused by: (i) third
party modifications of Contributor Version, or (ii) the
combination of Modifications made by that Contributor with other
software (except as part of the Contributor Version) or other
devices; or (3) under Patent Claims infringed by Covered
Software in the absence of Modifications made by that
Contributor.

3. Distribution Obligations.

3.1. Availability of Source Code. Any Covered Software that You
distribute or otherwise make available in Executable form must
also be made available in Source Code form and that Source Code
form must be distributed only under the terms of this License.
You must include a copy of this License with every copy of the
Source Code form of the Covered Software You distribute or
otherwise make available. You must inform recipients of any such
Covered Software in Executable form as to how they can obtain
such Covered Software in Source Code form in a reasonable manner
on or through a medium customarily used for software exchange.

3.2. Modifications. The Modifications that You create or to
which You contribute are governed by the terms of this License.
You represent that You believe Your Modifications are Your
original creation(s) and/or You have sufficient rights to grant
the rights conveyed by this License.

3.3. Required Notices. You must include a notice in each of Your
Modifications that identifies You as the Contributor of the
Modification. You may not remove or alter any copyright, patent
or trademark notices contained within the Covered Software, or
any notices of licensing or any descriptive text giving
attribution to any Contributor or the Initial Developer.

3.4. Application of Additional Terms. You may not offer or
impose any terms on any Covered Software in Source Code form
that alters or restricts the applicable version of this License
or the recipients rights hereunder. You may choose to offer, and
to charge a fee for, warranty, support, indemnity or liability
obligations to one or more recipients of Covered
Software. However, you may do so only on Your own behalf, and
not on behalf of the Initial Developer or any Contributor. You
must make it absolutely clear that any such warranty, support,
indemnity or liability obligation is offered by You alone, and
You hereby agree to indemnify the Initial Developer and every
Contributor for any liability incurred by the Initial Developer
or such Contributor as a result of warranty, support, indemnity
or liability terms You offer.

3.5. Distribution of Executable Versions. You may distribute the
Executable form of the Covered Software under the terms of this
License or under the terms of a license of Your choice, which
may contain terms different from this License, provided that You
are in compliance with the terms of this License and that the
license for the Executable form does not attempt to limit or
alter the recipients rights in the Source Code form from the
rights set forth in this License. If You distribute the Covered
Software in Executable form under a different license, You must
make it absolutely clear that any terms which differ from this
License are offered by You alone, not by the Initial Developer
or Contributor. You hereby agree to indemnify the Initial
Developer and every Contributor for any liability incurred by
the Initial Developer or such Contributor as a result of any
such terms You offer.

3.6. Larger Works. You may create a Larger Work by combining
Covered Software with other code not governed by the terms of
this License and distribute the Larger Work as a single product.
In such a case, You must make sure the requirements of this
License are fulfilled for the Covered Software.

4. Versions of the License.

4.1. New Versions. Sun Microsystems, Inc. is the initial license
steward and may publish revised and/or new versions of this
License from time to time. Each version will be given a
distinguishing version number. Except as provided in Section
4.3, no one other than the license steward has the right to
modify this License.

4.2. Effect of New Versions. You may always continue to use,
distribute or otherwise make the Covered Software available
under the terms of the version of the License under which You
originally received the Covered Software. If the Initial
Developer includes a notice in the Original Software prohibiting
it from being distributed or otherwise made available under any
subsequent version of the License, You must distribute and make
the Covered Software available under the terms of the version of
the License under which You originally received the Covered
Software.  Otherwise, You may also choose to use, distribute or
otherwise make the Covered Software available under the terms of
any subsequent version of the License published by the license
steward.

4.3. Modified Versions. When You are an Initial Developer and
You want to create a new license for Your Original Software, You
may create and use a modified version of this License if You:
(a) rename the license and remove any references to the name of
the license steward (except to note that the license differs
from this License); and (b) otherwise make it clear that the
license contains terms which differ from this License.

5. DISCLAIMER OF WARRANTY. COVERED SOFTWARE IS PROVIDED UNDER
THIS LICENSE ON AN AS IS BASIS, WITHOUT WARRANTY OF ANY KIND,
EITHER EXPRESSED OR IMPLIED, INCLUDING, WITHOUT LIMITATION,
WARRANTIES THAT THE COVERED SOFTWARE IS FREE OF DEFECTS,
MERCHANTABLE, FIT FOR A PARTICULAR PURPOSE OR NON-INFRINGING.
THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE COVERED
SOFTWARE IS WITH YOU. SHOULD ANY COVERED SOFTWARE PROVE
DEFECTIVE IN ANY RESPECT, YOU (NOT THE INITIAL DEVELOPER OR ANY
OTHER CONTRIBUTOR) ASSUME THE COST OF ANY NECESSARY SERVICING,
REPAIR OR CORRECTION. THIS DISCLAIMER OF WARRANTY CONSTITUTES AN
ESSENTIAL PART OF THIS LICENSE. NO USE OF ANY COVERED SOFTWARE
IS AUTHORIZED HEREUNDER EXCEPT UNDER THIS DISCLAIMER.

6. TERMINATION.

6.1. This License and the rights granted hereunder will
terminate automatically if You fail to comply with terms herein
and fail to cure such breach within 30 days of becoming aware of
the breach. Provisions which, by their nature, must remain in
effect beyond the termination of this License shall survive.

6.2. If You assert a patent infringement claim (excluding
declaratory judgment actions) against Initial Developer or a
Contributor (the Initial Developer or Contributor against whom
You assert such claim is referred to as Participant) alleging
that the Participant Software (meaning the Contributor Version
where the Participant is a Contributor or the Original Software
where the Participant is the Initial Developer) directly or
indirectly infringes any patent, then any and all rights granted
directly or indirectly to You by such Participant, the Initial
Developer (if the Initial Developer is not the Participant) and
all Contributors under Sections 2.1 and/or 2.2 of this License
shall, upon 60 days notice from Participant terminate
prospectively and automatically at the expiration of such 60 day
notice period, unless if within such 60 day period You withdraw
Your claim with respect to the Participant Software against such
Participant either unilaterally or pursuant to a written
agreement with Participant.

6.3. In the event of termination under Sections 6.1 or 6.2
above, all end user licenses that have been validly granted by
You or any distributor hereunder prior to termination (excluding
licenses granted to You by any distributor) shall survive
termination.

7. LIMITATION OF LIABILITY. UNDER NO CIRCUMSTANCES AND UNDER NO
LEGAL THEORY, WHETHER TORT (INCLUDING NEGLIGENCE), CONTRACT, OR
OTHERWISE, SHALL YOU, THE INITIAL DEVELOPER, ANY OTHER
CONTRIBUTOR, OR ANY DISTRIBUTOR OF COVERED SOFTWARE, OR ANY
SUPPLIER OF ANY OF SUCH PARTIES, BE LIABLE TO ANY PERSON FOR ANY
INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES OF ANY
CHARACTER INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOST
PROFITS, LOSS OF GOODWILL, WORK STOPPAGE, COMPUTER FAILURE OR
MALFUNCTION, OR ANY AND ALL OTHER COMMERCIAL DAMAGES OR LOSSES,
EVEN IF SUCH PARTY SHALL HAVE BEEN INFORMED OF THE POSSIBILITY
OF SUCH DAMAGES. THIS LIMITATION OF LIABILITY SHALL NOT APPLY TO
LIABILITY FOR DEATH OR PERSONAL INJURY RESULTING FROM SUCH
PARTYS NEGLIGENCE TO THE EXTENT APPLICABLE LAW PROHIBITS SUCH
LIMITATION. SOME JURISDICTIONS DO NOT ALLOW THE EXCLUSION OR
LIMITATION OF INCIDENTAL OR CONSEQUENTIAL DAMAGES, SO THIS
EXCLUSION AND LIMITATION MAY NOT APPLY TO YOU.

8. U.S. GOVERNMENT END USERS. The Covered Software is a
commercial item, as that term is defined in 48 C.F.R. 2.101
(Oct. 1995), consisting of commercial computer software (as that
term is defined at 48 C.F.R. 252.227-7014(a)(1)) and commercial
computer software documentation as such terms are used in 48
C.F.R. 12.212 (Sept. 1995). Consistent with 48 C.F.R. 12.212 and
48 C.F.R. 227.7202-1 through 227.7202-4 (June 1995), all
U.S. Government End Users acquire Covered Software with only
those rights set forth herein. This U.S. Government Rights
clause is in lieu of, and supersedes, any other FAR, DFAR, or
other clause or provision that addresses Government rights in
computer software under this License.

9. MISCELLANEOUS. This License represents the complete agreement
concerning subject matter hereof. If any provision of this
License is held to be unenforceable, such provision shall be
reformed only to the extent necessary to make it enforceable.
This License shall be governed by the law of the jurisdiction
specified in a notice contained within the Original Software
(except to the extent applicable law, if any, provides
otherwise), excluding such jurisdictions conflict-of-law
provisions. Any litigation relating to this License shall be
subject to the jurisdiction of the courts located in the
jurisdiction and venue specified in a notice contained within
the Original Software, with the losing party responsible for
costs, including, without limitation, court costs and reasonable
attorneys fees and expenses. The application of the United
Nations Convention on Contracts for the International Sale of
Goods is expressly excluded. Any law or regulation which
provides that the language of a contract shall be construed
against the drafter shall not apply to this License. You agree
that You alone are responsible for compliance with the United
States export administration regulations (and the export control
laws and regulation of any other countries) when You use,
distribute or otherwise make available any Covered Software.

10. RESPONSIBILITY FOR CLAIMS. As between Initial Developer and
the Contributors, each party is responsible for claims and
damages arising, directly or indirectly, out of its utilization
of rights under this License and You agree to work with Initial
Developer and Contributors to distribute such responsibility on
an equitable basis. Nothing herein is intended or shall be
deemed to constitute any admission of liability.

----------------------------------------------------------------

NOTICE PURSUANT TO SECTION 9 OF THE COMMON DEVELOPMENT AND
DISTRIBUTION LICENSE (CDDL): This code is released under the
CDDL and shall be governed by the laws of the State of
California (excluding conflict-of-law provisions). Any
litigation relating to this License shall be subject to the
jurisdiction of the Federal Courts of the Northern District of
California and the state courts of the State of California, with
venue lying in Santa Clara County, California.

----------------------------------------------------------------

```

LibRaw identifies the origin of the dcraw portions in `src/formats/libraw/COPYRIGHT`.

```text
LibRaw uses code from dcraw.c -- Dave Coffin's raw photo decoder,
dcraw.c is copyright 1997-2018 by Dave Coffin, dcoffin a cybercom o net.
LibRaw do not use RESTRICTED code from dcraw.c
```

The DCB demosaic and FBDD denoise routines carry this notice in `dcb_demosaic.cpp`.

```text
Copyright (C) 2010,  Jacek Gozdz (cuniek@kft.umcs.lublin.pl)

This code is licensed under a (3-clause) BSD license as follows :

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following
conditions are met:

* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following
disclaimer in the documentation and/or other materials provided
with the distribution.
* Neither the name of the author nor the names of its
contributors may be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
```

The X3F decoder carries this notice in `x3f_utils_patched.cpp`.

```text
Copyright (c) 2010, Roland Karlsson (roland@proxel.se)
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the organization nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY ROLAND KARLSSON ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ROLAND KARLSSON BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

LibRaw identifies the Adobe DNG SDK fragments in its copyright file as MIT licensed.

```text
Copyright (c) 2005 Adobe Systems Incorporated

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## ISO Coated v2 CMYK profile (basICColor)

`third_party/icc/basiccolor/ISOcoated_v2_bas.ICC` is the "ISO Coated v2
(basICColor)" ICC profile from basICColor GmbH's Offset 2009 set
(http://www.basiccolor.de, http://colormanagement.org), taken unmodified from the
OpenICC project's `icc-profiles-basiccolor-printing2009-1.2.0` package
(https://sourceforge.net/projects/openicc/files/basICColor-Profiles/), fetched
October 14, 2026. Copyright (c) 2007-2010 basICColor GmbH, zlib/libpng License;
the license text is at `third_party/icc/basiccolor/LICENSE-ZLIB-bICC`. Patchy
compiles the profile and that license text into the application and uses the
profile as the default for CMYK color numbers.

## G'MIC and CImg (desktop builds)

`src/filters/gmic/` vendors G'MIC 4.0.5 (https://gmic.eu, Copyright David
Tschumperlé and the G'MIC contributors, GREYC / CNRS): the interpreter
(`gmic.h`, `gmic.cpp`), the CImg Library it is built on (`CImg.h`,
https://cimg.eu) and the standard filter library (`gmic_stdlib.gmic`). These
files are dual-licensed under CeCILL-C 1.0 or CeCILL 2.1; Patchy uses them
under CeCILL-C. Patchy's modifications are marked `PATCHY` in the sources and
described in `docs/gmic.md`; the complete corresponding source is this
repository. The G'MIC-Qt plug-in, the community filters, and FFTW are not
included. The license texts are at `src/filters/gmic/COPYING` and ship in the
desktop packages as `gmic-COPYING.txt` (in `licenses\gmic` on Windows,
`Contents/Resources/licenses` on macOS and `share/licenses/com.rtsoft.patchy`
on Linux). The web build does not contain this code.

## pocketfft (desktop builds)

The G'MIC engine computes its Fourier transforms with pocketfft
(`src/filters/gmic/pocketfft/pocketfft_hdronly.h`, unmodified, from
https://github.com/mreineck/pocketfft, cpp branch, commit
`c90e55b3d529f8efa40ed01a20de22405f45fc65`). Copyright (C) 2010-2024
Max-Planck-Society, Copyright (C) 2019-2020 Peter Bell, Copyright (C) 2024
Tan Ping Liang and Peter Bell, Copyright (C) 2024 Cris Luengo, with portions
Copyright (C) 2003, 2007-14 Matteo Frigo and the Massachusetts Institute of
Technology; BSD 3-Clause License. The license text is
`src/filters/gmic/pocketfft/LICENSE.md` and ships beside the G'MIC texts as
`pocketfft-LICENSE.md`.

## LLVM OpenMP runtime (macOS)

The macOS app carries `libomp.dylib` in `Contents/Frameworks`: the OpenMP
runtime the G'MIC engine runs its loops with, built by
`scripts/setup-openmp-mac.sh` from the LLVM
21.1.8 release source (https://github.com/llvm/llvm-project). Copyright the
LLVM project contributors; Apache License 2.0 with LLVM Exceptions. The upstream
license ships in `Contents/Resources/licenses/libomp-LICENSE.txt`. The
Windows package's OpenMP runtime is part of the Visual C++ runtime above, and
the Linux build uses the Flatpak runtime's `libgomp`.

## SANE and libgphoto2 (Linux Flatpak only)

The Flatpak carries sane-backends 1.4.0 (<https://gitlab.com/sane-project/backends>;
GNU GPL version 2 or later, with SANE's exception that permits linking on `libsane` and
most backends; its `LICENSE` file says not every backend carries it) and libgphoto2
2.5.32 (<https://github.com/gphoto/libgphoto2>; GNU LGPL version 2.1 or later), built
unmodified from the release tarballs. Patchy loads both with `dlopen` at run time for
scanner and camera import; the AppImage and native builds use the distribution's copies
and ship neither. Their license texts ship in `share/licenses/com.rtsoft.patchy/` as
`sane-backends-COPYING.txt`, `sane-backends-LICENSE.txt` and `libgphoto2-COPYING.txt`.

Source code: the corresponding source of both, the exact release tarballs the Flatpak
manifest names with the manifest that builds them, is `PatchyLinuxScannerSources.zip`,
published with every Patchy release on GitHub next to the Flatpak bundles
(<https://github.com/SethRobinson/Patchy/releases>, the release of that version). The
Flatpak's `scanner-SOURCES.txt` says the same.

## ONNX Runtime (desktop builds)

Automatic selection (the Object Selection tool, and Select > Subject in builds that include it) runs on ONNX
Runtime 1.30.0's CPU execution provider, dynamically linked
(`onnxruntime.dll` / `libonnxruntime`). Copyright (c) Microsoft Corporation,
MIT License. Its license and `ThirdPartyNotices.txt` ship in `ai-models/` as
`onnxruntime-LICENSE.txt` and `onnxruntime-ThirdPartyNotices.txt`.
Source: https://github.com/microsoft/onnxruntime

## ONNX Runtime Web (web build only)

The web application runs the Object Selection models in ONNX Runtime Web 1.30.0
(the `onnxruntime-web` npm package, https://www.npmjs.com/package/onnxruntime-web),
Copyright (c) Microsoft Corporation, MIT License. The page loads it from the
`onnxruntime-web-1.30.0/` folder beside it on first use; its `LICENSE` and
`ThirdPartyNotices.txt` are staged in that folder.

## Automatic selection models (desktop builds, and the two Object Selection models in the web build)

Shipped in `ai-models/` (the web build's folder holds only the two MobileSAM files; the
EfficientViT-SAM files are downloaded on request into the user's data folder) as conversions Patchy made from the
original float32 files (`scripts/ai/quantize_models.py`; provenance and checksums in
`docs/ai-selection.md`; the license texts, including Apache License 2.0, and the
modification notice are in `ai-models/MODEL-LICENSES.txt`, from `third_party/ai-models/`):

- `efficientvit_sam_xl0_encoder_fp16.onnx`, `efficientvit_sam_xl0_decoder_multi_fp16.onnx`
  (Object Selection's High Quality model, a desktop download): EfficientViT-SAM XL0 (MIT Han Lab,
  https://github.com/mit-han-lab/efficientvit, Apache License 2.0), from the project's
  own ONNX export (https://huggingface.co/mit-han-lab/efficientvit-sam), with the Segment
  Anything prompt encoder and mask decoder (Meta Platforms, Inc., Apache License 2.0).
  Weights stored as float16; the decoder outputs all four masks.
- `mobile_sam_image_encoder_int8.onnx`, `sam_mask_decoder_multi_int8.onnx`: MobileSAM
  (Chaoning Zhang et al., https://github.com/ChaoningZhang/MobileSAM, Apache
  License 2.0) with the Segment Anything mask decoder (Meta Platforms, Inc.,
  https://github.com/facebookresearch/segment-anything, Apache License 2.0).
  ONNX export by Acly (https://huggingface.co/Acly/MobileSAM, MIT License).
- `birefnet_lite_general_1024_int8.onnx` (only in builds that include Select Subject;
  current releases do not): BiRefNet_lite general (Peng Zheng et al.,
  https://github.com/ZhengPeng7/BiRefNet, MIT License), from the project's own ONNX
  export.

## Adobe TrustMark watermark models (desktop download)

Not shipped: on the user's request Patchy downloads Adobe's ONNX models for TrustMark variants
Q, P and C (decoders) and Q (encoder), unchanged, from Patchy's `ai-models-3` GitHub release (or
the Content Authenticity Initiative's model host) into the user's data folder, with Adobe's
license beside them (`trustmark_*.onnx`, `trustmark_LICENSE.txt`; provenance and checksums in
`docs/watermarks.md`).
TrustMark (https://github.com/adobe/trustmark), code and models:

Copyright 2023 Adobe. All Rights Reserved. MIT License.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## PP-OCR text recognition models (desktop download)

Not shipped: on the user's request Patchy downloads PaddlePaddle's ONNX exports of the PP-OCR
text recognition models, unchanged, from Patchy's `ai-models-4` GitHub release (or PaddlePaddle's
own repositories on Hugging Face) into the user's data folder, with the Apache License 2.0 text
beside them (`ppocr_*.onnx`, each recognizer's character list `ppocr_*.yml`, and
`ppocr_LICENSE.txt`; pinned commits and checksums in `docs/legal-licensing.md` and
`scripts/setup-ai-deps.ps1`). The files are renamed, not modified.

PaddleOCR (https://github.com/PaddlePaddle/PaddleOCR), Copyright (c) 2016 PaddlePaddle Authors,
Apache License 2.0. The models, each from its own repository under
https://huggingface.co/PaddlePaddle, each declared Apache License 2.0 there:
`PP-OCRv6_small_det_onnx`, `PP-OCRv6_small_rec_onnx`, and the PP-OCRv5 mobile recognizers
`korean`, `cyrillic`, `eslav`, `el`, `th`, `devanagari`, `ta` and `te`
(`<name>_PP-OCRv5_mobile_rec_onnx`).

## Bundled photo-texture pattern presets (Poly Haven, CC0)

The 20 photo-based pattern presets under the "Textures" folder (embedded as
512x512 PNG tiles in `src/ui/textures/`, ids in `src/core/pattern_presets.cpp`)
are downscaled diffuse maps from Poly Haven (https://polyhaven.com), published
under CC0 ("You can use our assets for any purpose, including commercial work.
You do not need to give credit or attribution"). All are real photo-scanned
surfaces created by named human artists (no AI generation, per
docs/legal-licensing.md); the 1K PNG masters were fetched from dl.polyhaven.org on
2026-07-12 and downscaled losslessly (no JPEG step) with a seam-preserving
3x3-tile filter.

| Patchy preset | Poly Haven asset | Author(s) |
| --- | --- | --- |
| Fine Wood Grain | fine_grained_wood | Rob Tuytel |
| Dark Walnut | dark_wood | Dario Barresi, Dimitrios Savva, Rico Cilliers |
| Oak Veneer | oak_veneer_01 | Jenelle van Heerden |
| Weathered Wood | rough_wood | Rob Tuytel |
| Old Planks | old_planks_02 | Rob Tuytel |
| Medieval Wood | medieval_wood | Rob Tuytel |
| Tree Bark | bark_brown_01 | Rob Tuytel |
| Weathered Marble | marble_rock_01 | (Poly Haven) |
| Slate Slabs | slab_tiles | (Poly Haven) |
| Granite Blocks | japanese_stone_wall | (Poly Haven) |
| Rock Face | rock_face | (Poly Haven) |
| Coarse Rust | rust_coarse_01 | Dimitrios Savva, Rico Cilliers |
| Steel Plate | metal_plate | Rob Tuytel |
| Brown Leather | brown_leather | Rob Tuytel |
| Denim Weave | denim_fabric | (Poly Haven) |
| Burlap | hessian_230 | (Poly Haven) |
| Rippled Sand | damp_sand | (Poly Haven) |
| Snow | snow_02 | (Poly Haven) |
| Cracked Earth | mud_cracked_dry_03 | (Poly Haven) |
| Mossy Forest Floor | forest_leaves_02 | (Poly Haven) |

Each asset page is `https://polyhaven.com/a/<asset id>`. CC0 requires no
attribution; the authors are credited here voluntarily (authors marked
"(Poly Haven)" are listed on the asset pages).

## Bundled custom shapes (game-icons.net, CC BY 3.0)

The 878 shapes in the Custom Shape picker that name an author in their tooltip (the topic
folders such as Animals, Nature and Games, plus most of Arrows and Symbols; ids
`shape.gameicons.*`, embedded as
`src/ui/shapes/game-icons.json`) are outlines from the game-icons.net collection
(https://game-icons.net, repository https://github.com/game-icons/icons at commit
`82d948812bfe3f269ef8f731dcdb07b08160edc4`), drawn by the people below (no AI generation, per
docs/legal-licensing.md). They are licensed under the Creative Commons Attribution 3.0
Unported license (https://creativecommons.org/licenses/by/3.0/), except Viscious Speed's and
Zeromancer's, which are CC0. Changes: the black background square was removed, the outlines
were renamed, sorted into Patchy's own folders and scaled to a unit box; the selection list is
`scripts/dev/gameicons/game_icons_manifest.tsv`. The collection asks for the credit "Icons
made by {author}"; Patchy shows each shape's author in its tooltip and the authors in the
Shapes Manager.

Icons made by:

| Author | Shapes |
| --- | --- |
| Delapouite | 447 |
| Lorc | 250 |
| Skoll | 47 |
| Sbed | 39 |
| Caro Asercion | 33 |
| Aussiesim | 11 |
| Viscious Speed | 10 |
| Cathelineau | 8 |
| PriorBlue | 5 |
| DarkZaitzev | 4 |
| Lord Berandas | 4 |
| Guard13007 | 3 |
| Faithtoken | 2 |
| Lucas | 2 |
| Quoting | 2 |
| Willdabeast | 2 |
| Zeromancer | 2 |
| Andy Meneely | 1 |
| GeneralAce135 | 1 |
| Irongamer | 1 |
| Pierre Leducq | 1 |
| Rihlsul | 1 |
| SeregaCthtuf | 1 |
| Zajkonur | 1 |

## Test fixtures (not distributed with the application)

Files under `test-fixtures/` are used only by the automated test suites and are
not part of any release package.

- `test-fixtures/readme/san_francisco_cityscape_cc0.jpg`: "Cityscape of San
  Francisco," photographed by Vladimir Mokry and published under the Creative
  Commons CC0 1.0 Universal Public Domain Dedication. The exact Wikimedia
  Commons original was downloaded on 2026-07-15 from
  https://commons.wikimedia.org/wiki/File:Cityscape_of_San_Francisco.jpg
  (SHA-256 `8B09FA97853316DC4C548C7A1631D4C0955148338DF7CA2E75BAA5BC576DDE0D`).
  It is used only to regenerate the README and gallery screenshots.
- `test-fixtures/readme/havana_vintage_car_cc0.jpg`: "Vintage car driving in Centro
  Havana," photographed by Eva Blue (Unsplash, https://unsplash.com/photos/ppuYl-1noKc) and
  published under the Creative Commons CC0 1.0 Universal Public Domain Dedication. The
  exact Wikimedia Commons original was downloaded on 2026-10-12 from
  https://commons.wikimedia.org/wiki/File:Vintage_car_driving_in_Centro_Havana_(Unsplash).jpg
  (SHA-256 `90D61080598A430F12615282014CA47138CD04BDE3AE8185333E0530480A35D8`).
  It is used only to regenerate the README and gallery screenshots.
- `test-fixtures/readme/brick_wall_new_brunswick_cc0.jpg`: "Brick wall in New Brunswick,
  NJ" (behind the College Avenue Gymnasium), photographed by Siddharth Patil and published
  under the Creative Commons CC0 1.0 Universal Public Domain Dedication. The exact
  Wikimedia Commons original was downloaded on 2026-10-28 from
  https://commons.wikimedia.org/wiki/File:Brick_wall_in_New_Brunswick,_NJ.jpg
  (SHA-256 `0AD3042939C2F01E8D4C9DBC0009B8FED1085E2914583742FA6CEB64C4E2EF25`).
  It is used only to regenerate the gallery Perspective Planes screenshot.
- `test-fixtures/svg/hot_air_balloons_cc0.svg`: "Hot Air Balloon Scene," a
  vector illustration published under the Creative Commons CC0 1.0 Universal
  Public Domain Dedication (uploader OpenClipart). The exact file was
  downloaded on 2026-07-18 from https://freesvg.org/hot-air-balloon-scene
  (SHA-256 `72F141E0F46C71E646349B56B28812C17653AF045739C89F73C0D02B44021DA4`).
  It is used only to regenerate the README SVG-import screenshot and by the
  SVG import tests.
- `test-fixtures/readme/stylized_sunset_cc0.png`: a raster rendering of
  "Stylized Sunset Illustration," a human-made vector landscape published
  under the Creative Commons CC0 1.0 Universal Public Domain Dedication
  (uploader OpenClipart, sourced from Pixabay). The source SVG was downloaded
  on 2026-08-25 from https://freesvg.org/stylized-sunset-illustration
  (SHA-256 `E3A7B2F6F58374CD04D936110DD349AB1B691F92CA9F4FC7BDB7D985B87E7B64`)
  and rasterized unmodified at its native 1920x1200 with Patchy's own SVG
  import (`patchy.exe <svg> --export <png>`); the committed PNG's SHA-256 is
  `770AB5D6CAA07150F3FE075C3E5F14EC78FF95B44FAD87BDA91C7FA7C9348BD8`.
  It is used only to regenerate the README and gallery screenshots.
- `test-fixtures/readme/beach_walker_cc0.jpg`: "Person walking on windy beach (Unsplash),"
  photographed by Timothy Kolczak and published under the Creative Commons CC0 1.0 Universal
  Public Domain Dedication: the 960 px Wikimedia Commons thumbnail of
  https://commons.wikimedia.org/wiki/File:Person_walking_on_windy_beach_(Unsplash).jpg,
  scaled to 800 px on its longer side (SHA-256
  `18CFF5080DDBB85F026EE1BD60AD40F9049D95E072D64C79CEEAE48556B4933B`). It is used only to
  regenerate the gallery Remove Object screenshot.
- `test-fixtures/readme/dink_tiles_ts02.png`: tile sheet `tiles/ts02.bmp` from the original
  Dink Smallwood game data (github.com/SethRobinson/RTDink, `bin/dink/tiles/`), released
  under a zlib-style license in 2008; converted to PNG unmodified (SHA-256
  `9FA479680DE9894DA574067007D8387C2FE8FE84FA6C5C43DAF5BD93D56B890A`). Dink Smallwood is (C) Robinson
  Technologies Corporation; this product includes software developed by Seth A. Robinson
  ( www.rtsoft.com ). It is used only to regenerate the gallery seamless tiling screenshot.
- `test-fixtures/ico/cpython-py.ico`: the CPython `py.ico` application icon
  from https://github.com/python/cpython (`PC/icons/py.ico`), included under the
  Python Software Foundation License 2.0 as a real-world multi-size icon sample.
- `test-fixtures/ico/vscode-code.ico`: the Visual Studio Code application icon
  from https://github.com/microsoft/vscode (`resources/win32/code.ico`),
  included under the MIT License as a real-world PNG-entry icon sample.
- `test-fixtures/ico/pillow-*.ico` / `pillow-cursor.cur`,
  `test-fixtures/tga/pillow-*.tga`, and `test-fixtures/gif/pillow-animated.gif`:
  generated locally with the Pillow imaging library (self-authored art; no
  third-party content).
- `test-fixtures/af/tiny-*.af` and `tiny-rgba8.png`: Affinity documents
  (self-authored gradient/pattern images, a nested-group document, and a CMYK
  conversion) created by the Patchy team by scripting a licensed Affinity 3.2.3
  install through its built-in JavaScript SDK; no third-party content. Used by
  the .af importer tests.
- `test-fixtures/af/tiny-v2-*.afphoto`: Affinity 2 documents (simple color
  fills, a curve shape, and short text) created by the Patchy team
  interactively in a licensed Affinity Photo 2.6.5 install; no third-party
  content. Used by the 2.x extension importer tests.
- `test-fixtures/af/tiny-v2-stale-dfsz.afphoto` and `tiny-lazy-placed.af`:
  deterministic byte-level derivations of the two fixtures above
  (local-test-fixtures/af-spike/author_derived_fixtures.py); no new content.
- `test-fixtures/aseprite/*.aseprite` and
  `aseprite-blend-modes-reference.png` (Aseprite's own flattened render of the
  blend-mode fixture): authored locally with Aseprite 1.3.17 via a batch script
  (self-authored art; no third-party content).
- `test-fixtures/pat/hue.pat`: a real Photoshop pattern fixture from Jaroslav
  Bereza's `jardicc/pat-parser` repository, included under the MIT License.
  The pinned source URL, checksum, copyright notice, and full license text are
  in `test-fixtures/pat/NOTICE.txt`.
- `test-fixtures/heif/quadrants.heic`: encoded from a self-authored
  quadrant-color PNG with Apple's `sips` tool on macOS (self-authored art; no
  third-party content).
- `test-fixtures/heif/hdr-pq-10bit.heic`, `hdr-hlg-10bit.heic` and `p3-10bit.heic`: 80x48
  10-bit HEIC images written by macOS ImageIO from self-authored 16-bit values (PQ and HLG
  test patches in BT.2020, and Display P3 SDR colors); synthetic values only, no
  third-party content. Used by the full-depth HEIF tests.
- `test-fixtures/jxr/hdr-ramp.jxr`: an 8x2 lossless 128bppRGBAFloat scRGB
  gradient (row 0 walks the SDR range 0..1, row 1 walks the HDR range 1..12.5)
  generated by the Patchy team with Windows' own JPEG XR encoder through the
  .NET `WmpBitmapEncoder`; synthetic values only, no third-party content. Used
  by the HDR tone-map regression test.
- `test-fixtures/rttex/*.rttex`: textures from the Proton SDK sample apps (Amigo,
  RTSimpleApp, BlipArcade, RTWhisperApp, RTDScroll), written by Seth Robinson, Patchy's
  author, with his own RTPack tool; no third-party content. Used by the Proton texture
  reader tests.
- `test-fixtures/dds/*.dds`: procedural gradients and solid colours generated by the Patchy
  team with `scripts/dev/dds/make_dds_fixtures.py`; Pillow 12 wrote the `pillow-*` files,
  the script assembled the `synth-*` files, and Microsoft's DirectXTex `texconv` (MIT,
  release binary, not distributed with Patchy) encoded the `texconv-*` BC4/BC7/BC6H files
  from the same self-authored art. No third-party content. Used by the DDS reader tests.


## Built-in color palette presets

The palette presets bundled for the palettized editing mode fall into two
groups. Hardware palettes (NES/2C02, Commodore 64 in Pepto's calibrated
rendering, Game Boy, CGA/EGA, the DOS/VGA mode-13h default DAC table, ZX
Spectrum, MSX/TMS9918, Amstrad CPC) are RGB renderings of hardware color
generation and are factual data, not copyrightable works. Community palettes
are included only where the author allows free use: the PICO-8 palette
(Lexaloffle explicitly permits using the PICO-8 palette in any work),
DawnBringer's DB16/DB32 palettes (published freely by their author on the
Pixelation forums and mirrored on Lospec), and the Dink Smallwood game palette
(RTsoft's own title, included by its author). All preset tables are generated
in code at `src/core/palette_presets.cpp`; no palette files are redistributed.

## Bundled UI themes

The theme files under `themes/bundled/` (compiled into the application through
`src/ui/themes.qrc`) are generated by `scripts/dev/generate-bundled-themes.py`
from Patchy's own Dark palette. Four of them place that palette onto the tone
scales of community color schemes, used under their MIT licenses: Solarized by
Ethan Schoonover, Nord by Sven Greb (Arctic Ice Studio), Dracula by Zeno Rocha,
and Gruvbox by Pavel Pertsev. Only the color values are used; no files, code,
or artwork from those projects are redistributed. Darkest, Medium Gray, and
High Contrast are Patchy's own.

The Dink Smallwood theme package (`themes/bundled/dink-smallwood/`) uses art from
Dink Smallwood and Dink Smallwood HD (github.com/SethRobinson/RTDink): the original
game data, released under a zlib-style license in 2008, and HD media under the
Dink Smallwood HD License. Its sounds are Seth A. Robinson's own recordings or
synthesized for the theme. Dink Smallwood is (C) Robinson Technologies Corporation;
the name is used with Seth A. Robinson's permission. This product includes
software developed by Seth A. Robinson ( www.rtsoft.com ). The package README
lists the source of every file.

## libwebp 1.6.0 (including SharpYUV)

Animated WebP import/export statically links the codec sources from
https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz
(SHA-256 `e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564`).
Source: `src/formats/libwebp`; Patchy's build prefixes symbols to isolate Qt's codec.
The two namespace-only source changes are documented in `src/formats/libwebp/PATCHY.md`.
The upstream BSD license and additional patent grant follow.

```text
Copyright (c) 2010, Google Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.

  * Neither the name of Google nor the names of its contributors may
    be used to endorse or promote products derived from this software
    without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


Additional IP Rights Grant (Patents)
------------------------------------

"These implementations" means the copyrightable works that implement the WebM
codecs distributed by Google as part of the WebM Project.

Google hereby grants to you a perpetual, worldwide, non-exclusive, no-charge,
royalty-free, irrevocable (except as stated in this section) patent license to
make, have made, use, offer to sell, sell, import, transfer, and otherwise
run, modify and propagate the contents of these implementations of WebM, where
such license applies only to those patent claims, both currently owned by
Google and acquired in the future, licensable by Google that are necessarily
infringed by these implementations of WebM. This grant does not include claims
that would be infringed only as a consequence of further modification of these
implementations. If you or your agent or exclusive licensee institute or order
or agree to the institution of patent litigation or any other patent
enforcement activity against any entity (including a cross-claim or
counterclaim in a lawsuit) alleging that any of these implementations of WebM
or any code incorporated within any of these implementations of WebM
constitute direct or contributory patent infringement, or inducement of
patent infringement, then any patent rights granted to you under this License
for these implementations of WebM shall terminate as of the date such
litigation is filed.
```

The `test-fixtures/webp` animation and reference PNGs are Patchy-authored geometric
test images, encoded with Pillow/libwebp: three 32 by 24 RGBA frames, 33/67/101 ms,
three total plays, lossless. They exercise partial alpha and offset rectangles.

`test-fixtures/gif/pillow-animated-transparent.gif` is a Patchy-authored geometric
fixture encoded with Pillow: three 32 by 24 transparent frames with red, green and
blue rectangles, delays 100/230/50 ms, disposal to background and infinite looping.
It verifies GIF to lossless WebP to GIF conversion alongside the original GIF fixture.

## JPEG XL conformance fixtures (tests only)

`test-fixtures/jxl/conformance-patches.jxl`, `conformance-patches-lossless.jxl` and their
`-ref.png` renderings are the `patches` and `patches_lossless` test cases of the libjxl
conformance suite (https://github.com/libjxl/conformance, commit
b1d0f990b03e57bf6d137c365cd5dc8b470b9191, BSD-3-Clause repository; the suite's README lists
both source images as CC0). SHA-256: `3d8d3847ba40844b1d829366065261f34cc4b4acc3d9006342c64127c1052a3e`
(patches), `2a47b6766264ac6243a43bbb85684688717591bb3bd9b4c2db56226192418b4e`
(patches_lossless). They are test data only and are not distributed with Patchy.

## tinyexr 1.0.9

`src/formats/tinyexr/tinyexr.h` vendors tinyexr 1.0.9 (https://github.com/syoyo/tinyexr),
the single-header OpenEXR reader and writer, compiled into `patchy_formats` for the
.exr format (docs/hdr-formats.md) with its ZIP codec routed to the vendored miniz.
tinyexr is BSD-3-Clause; the license is included at `src/formats/tinyexr/LICENSE`.

```text
BSD 3-Clause License

Copyright (c) 2014 - 2021, Syoyo Fujita and many contributors.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

The OpenEXR portions of `tinyexr.h` carry the following notices. The 2004 notice
identifies the PIZ compressor and decompressor.

```text
// Copyright (c) 2002, Industrial Light & Magic, a division of Lucas
// Digital Ltd. LLC
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *       Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *       Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// *       Neither the name of Industrial Light & Magic nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

```text
// Copyright (c) 2004, Industrial Light & Magic, a division of Lucas
// Digital Ltd. LLC)
// (3 clause BSD license)
```

## OpenJPEG 2.5.3

`src/formats/openjpeg/` vendors the core library (src/lib/openjp2) of OpenJPEG 2.5.3
(https://github.com/uclouvain/openjpeg), the JPEG 2000 Part 1 codec, built as the
static `patchy_openjpeg` archive for the .jp2/.j2k formats (docs/hdr-formats.md). No
tools, plug-ins or JPIP code are included. OpenJPEG is BSD-2-Clause; the license is
included at `src/formats/openjpeg/LICENSE`.

```text
/*
 * The copyright in this software is being made available under the 2-clauses 
 * BSD License, included below. This software may be subject to other third 
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2003-2009, Francois-Olivier Devaux
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2011-2012, Centre National d'Etudes Spatiales (CNES), France 
 * Copyright (c) 2012, CS Systemes d'Information, France
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
```

Individual codec files carry these additional copyright notices under the same BSD terms.

```text
Copyright (c) 2003-2007, Francois-Olivier Devaux
Copyright (c) 2006-2007, Parvatha Elangovan
Copyright (c) 2007, Callum Lerwick <seg@haxxed.com>
Copyright (c) 2007, Jonathan Ballard <dzonatas@dzonux.net>
Copyright (c) 2008, 2011-2012, Centre National d'Etudes Spatiales (CNES), FR
Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
Copyright (c) 2010-2011, Kaori Hagihara
Copyright (c) 2012, Carl Hetherington
Copyright (c) 2015, Mathieu Malaterre <mathieu.malaterre@gmail.com>
Copyright (c) 2015, Matthieu Darbois
Copyright (c) 2016, Even Rouault
Copyright (c) 2017, IntoPIX SA <support@intopix.com>
Copyright (c) 2017, IntoPix SA <contact@intopix.com>
```

The HT block decoder in `src/formats/openjpeg/ht_dec.c` carries this notice.

```text
// Copyright (c) 2021, Aous Naman
// Copyright (c) 2021, Kakadu Software Pty Ltd, Australia
// Copyright (c) 2021, The University of New South Wales, Australia
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
// TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## libjpeg-turbo 3.1.4.1

`src/formats/libjpeg-turbo/` vendors the decompression sources of libjpeg-turbo 3.1.4.1
(https://libjpeg-turbo.org, https://github.com/libjpeg-turbo/libjpeg-turbo), built as the
static `patchy_libjpeg` archive that opens 12-bit and lossless JPEG files
(docs/file-formats.md). No compressor, tools, TurboJPEG API or SIMD code are included, and
the sources are unmodified (Patchy's build renames the library's global symbols with a
forced-include header). libjpeg-turbo is covered by the IJG License, the Modified (3-clause)
BSD License and, for parts of it, the zlib License; the license text is included at
`src/formats/libjpeg-turbo/LICENSE.md` and the IJG README at
`src/formats/libjpeg-turbo/README.ijg`.

This software is based in part on the work of the Independent JPEG Group.

## JPEG test fixtures (tests only)

`test-fixtures/jpeg/testorig12.jpg` is the 12-bit test image of libjpeg-turbo 3.1.4.1
(`testimages/testorig12.jpg`, IJG License; the same file is
`jpeg-conformance/valid/testorig12.jpg` in https://github.com/imazen/codec-corpus, whose
README lists that suite's licences as MIT / IJG+BSD per source). SHA-256
`34790770f76db8e60a7765b52ca4edf5f16bc21bcb8c6045ca2efef39a8a013e`.
`lossless16-rgb.jpg` and `lossless12-gray.jpg` are 32 x 24 synthetic gradients written by
libjpeg-turbo for Patchy's tests (the formulas are in the test). They are test data only and
are not distributed with Patchy.

## dav1d 1.5.4

`src/formats/dav1d/` vendors the library sources of dav1d 1.5.4
(https://code.videolan.org/videolan/dav1d/-/archive/1.5.4/dav1d-1.5.4.tar.gz, SHA-256
`a1d5b63d2d38ec9bd03acf643caa51fa22edd1e89c5a109c4807717216bbec07`), VideoLAN's AV1
decoder, built as the static `patchy_dav1d` archive without its assembly directories,
tools, tests or examples (cmake/patchy_avif.cmake, docs/avif.md). Sources are unmodified.
dav1d is BSD-2-Clause; the license is included at `src/formats/dav1d/COPYING` and follows.

```text
Copyright © 2018-2025, VideoLAN and dav1d authors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

Individual source files carry these additional notices under the same terms.

```text
Copyright © 2018, Niklas Haas
Copyright © 2018, Two Orioles, LLC
Copyright © 2018-2019, Two Orioles, LLC
Copyright © 2018-2022, Two Orioles, LLC
Copyright © 2018-2023, Two Orioles, LLC
Copyright © 2020, Two Orioles, LLC
Copyright © 2023, Two Orioles, LLC
Copyright © 2024, Two Orioles, LLC
```

## libavif 1.4.2 with its libyuv subset

`src/formats/libavif/` vendors the library sources of libavif 1.4.2
(https://github.com/AOMediaCodec/libavif/archive/refs/tags/v1.4.2.tar.gz, SHA-256
`2b645287340ba5a631d268b551dc2d72bd73ac33335962dd36dcdb6d8366921d`), the AV1 Image File
Format library, built as the static `patchy_avif` archive with dav1d for decoding and libaom
for encoding AVIF images (docs/avif.md). Sources are unmodified. libavif is BSD-2-Clause; its `LICENSE` is at
`src/formats/libavif/LICENSE` and the BSD text follows. `src/formats/libavif/third_party/libyuv`
is the trimmed libyuv scaling subset libavif bundles (libyuv revision
def473f501acbd652cd4593fd2a90a067e8c9f1a, BSD-3-Clause; license at
`src/formats/libavif/third_party/libyuv/LICENSE`, reproduced after libavif's).

```text
Copyright 2019 Joe Drago. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

```text
Copyright 2011 The LibYuv Project Authors. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.

  * Neither the name of Google nor the names of its contributors may
    be used to endorse or promote products derived from this software
    without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

Additional BSD-2-Clause copyright notices from the libavif sources follow.

```text
Copyright 2020 Joe Drago. All rights reserved.
Copyright 2021 Joe Drago. All rights reserved.
Copyright 2022 Google LLC
Copyright 2023 Google LLC
Copyright 2024 Brad Hards. All rights reserved.
Copyright 2024 Google LLC
```

The OBU parser adapts code from dav1d. `src/formats/libavif/src/obu.c` also credits Two Orioles, LLC.

```text
Copyright © 2018, VideoLAN and dav1d authors
Copyright © 2018, Two Orioles, LLC
```

```text
Copyright © 2018-2019, VideoLAN and dav1d authors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## libaom 3.15.2

`src/formats/libaom/` vendors the encoder library sources of libaom 3.15.2
(https://storage.googleapis.com/aom-releases/libaom-3.15.2.tar.gz), the Alliance for Open
Media's AV1 codec, built as the static `patchy_aom` archive for the generic CPU with the
decoder, SIMD directories, apps, tests and tools left out (cmake/patchy_avif.cmake,
docs/avif.md). Sources are unmodified; `src/formats/libaom/patchy-config` holds the
generated headers and source list harvested from upstream's build. libaom is BSD-2-Clause
and its patents are licensed under the Alliance for Open Media Patent License 1.0; both
texts are at `src/formats/libaom/LICENSE` and `src/formats/libaom/PATENTS` and follow, then
the licenses of the bundled FAST feature detector (`third_party/fastfeat`, BSD-3-Clause,
Edward Rosten) and vector (`third_party/vector`, MIT, Peter Goldsborough).

```text
Copyright (c) 2016, Alliance for Open Media. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in
   the documentation and/or other materials provided with the
   distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

```text
Alliance for Open Media Patent License 1.0

1. License Terms.

1.1. Patent License. Subject to the terms and conditions of this License, each
     Licensor, on behalf of itself and successors in interest and assigns,
     grants Licensee a non-sublicensable, perpetual, worldwide, non-exclusive,
     no-charge, royalty-free, irrevocable (except as expressly stated in this
     License) patent license to its Necessary Claims to make, use, sell, offer
     for sale, import or distribute any Implementation.

1.2. Conditions.

1.2.1. Availability. As a condition to the grant of rights to Licensee to make,
       sell, offer for sale, import or distribute an Implementation under
       Section 1.1, Licensee must make its Necessary Claims available under
       this License, and must reproduce this License with any Implementation
       as follows:

       a. For distribution in source code, by including this License in the
          root directory of the source code with its Implementation.

       b. For distribution in any other form (including binary, object form,
          and/or hardware description code (e.g., HDL, RTL, Gate Level Netlist,
          GDSII, etc.)), by including this License in the documentation, legal
          notices, and/or other written materials provided with the
          Implementation.

1.2.2. Additional Conditions. This license is directly from Licensor to
       Licensee.  Licensee acknowledges as a condition of benefiting from it
       that no rights from Licensor are received from suppliers, distributors,
       or otherwise in connection with this License.

1.3. Defensive Termination. If any Licensee, its Affiliates, or its agents
     initiates patent litigation or files, maintains, or voluntarily
     participates in a lawsuit against another entity or any person asserting
     that any Implementation infringes Necessary Claims, any patent licenses
     granted under this License directly to the Licensee are immediately
     terminated as of the date of the initiation of action unless 1) that suit
     was in response to a corresponding suit regarding an Implementation first
     brought against an initiating entity, or 2) that suit was brought to
     enforce the terms of this License (including intervention in a third-party
     action by a Licensee).

1.4. Disclaimers. The Reference Implementation and Specification are provided
     "AS IS" and without warranty. The entire risk as to implementing or
     otherwise using the Reference Implementation or Specification is assumed
     by the implementer and user. Licensor expressly disclaims any warranties
     (express, implied, or otherwise), including implied warranties of
     merchantability, non-infringement, fitness for a particular purpose, or
     title, related to the material. IN NO EVENT WILL LICENSOR BE LIABLE TO
     ANY OTHER PARTY FOR LOST PROFITS OR ANY FORM OF INDIRECT, SPECIAL,
     INCIDENTAL, OR CONSEQUENTIAL DAMAGES OF ANY CHARACTER FROM ANY CAUSES OF
     ACTION OF ANY KIND WITH RESPECT TO THIS LICENSE, WHETHER BASED ON BREACH
     OF CONTRACT, TORT (INCLUDING NEGLIGENCE), OR OTHERWISE, AND WHETHER OR
     NOT THE OTHER PARTY HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

2. Definitions.

2.1. Affiliate.  "Affiliate" means an entity that directly or indirectly
     Controls, is Controlled by, or is under common Control of that party.

2.2. Control. "Control" means direct or indirect control of more than 50% of
     the voting power to elect directors of that corporation, or for any other
     entity, the power to direct management of such entity.

2.3. Decoder.  "Decoder" means any decoder that conforms fully with all
     non-optional portions of the Specification.

2.4. Encoder.  "Encoder" means any encoder that produces a bitstream that can
     be decoded by a Decoder only to the extent it produces such a bitstream.

2.5. Final Deliverable.  "Final Deliverable" means the final version of a
     deliverable approved by the Alliance for Open Media as a Final
     Deliverable.

2.6. Implementation.  "Implementation" means any implementation, including the
     Reference Implementation, that is an Encoder and/or a Decoder. An
     Implementation also includes components of an Implementation only to the
     extent they are used as part of an Implementation.

2.7. License. "License" means this license.

2.8. Licensee. "Licensee" means any person or entity who exercises patent
     rights granted under this License.

2.9. Licensor.  "Licensor" means (i) any Licensee that makes, sells, offers
     for sale, imports or distributes any Implementation, or (ii) a person
     or entity that has a licensing obligation to the Implementation as a
     result of its membership and/or participation in the Alliance for Open
     Media working group that developed the Specification.

2.10. Necessary Claims.  "Necessary Claims" means all claims of patents or
      patent applications, (a) that currently or at any time in the future,
      are owned or controlled by the Licensor, and (b) (i) would be an
      Essential Claim as defined by the W3C Policy as of February 5, 2004
      (https://www.w3.org/Consortium/Patent-Policy-20040205/#def-essential)
      as if the Specification was a W3C Recommendation; or (ii) are infringed
      by the Reference Implementation.

2.11. Reference Implementation. "Reference Implementation" means an Encoder
      and/or Decoder released by the Alliance for Open Media as a Final
      Deliverable.

2.12. Specification. "Specification" means the specification designated by
      the Alliance for Open Media as a Final Deliverable for which this
      License was issued.
```

```text
Copyright (c) 2006, 2008 Edward Rosten
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:


	*Redistributions of source code must retain the above copyright
	 notice, this list of conditions and the following disclaimer.

	*Redistributions in binary form must reproduce the above copyright
	 notice, this list of conditions and the following disclaimer in the
	 documentation and/or other materials provided with the distribution.

	*Neither the name of the University of Cambridge nor the names of
	 its contributors may be used to endorse or promote products derived
	 from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

```text
The MIT License (MIT)
Copyright (c) 2016 Peter Goldsborough

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## libjxl 0.12.0 with Highway, Brotli and skcms

JPEG XL import and export statically links the libjxl 0.12.0 codec from
https://github.com/libjxl/libjxl/archive/refs/tags/v0.12.0.tar.gz
(SHA-256 `03e9be69a30be4011f559da75328b6d7cea8ad921fabfbd551ce10bf45cdc992`) with the
dependency revisions that release pins:
Highway https://github.com/google/highway/archive/457c891775a7397bdb0376bb1031e6e027af1c48.tar.gz
(SHA-256 `5124b0501c98d9930dbb065bfa1a5bbbd59ce0f12facb7e1e33aaef01a5f1f1a`),
Brotli 1.2.0 https://github.com/google/brotli/archive/028fb5a23661f123017c060daa546b55cf4bde29.tar.gz
(SHA-256 `0afe09a53c8bad9861c8dd1fc1284308d54f19d2979ba3541cfdcc9b05fe360f`) and
skcms https://github.com/google/skcms/archive/96d9171c94b937a1b5f0293de7309ac16311b722.tar.gz
(SHA-256 `9bb4b5bba0b7c04f6c2bce9ff713d61e23c9a20c4945161ae16290498ad74627`).
Source: `src/formats/libjxl` (library sources only; `src/formats/libjxl/PATCHY.md` lists
what was left out). Patchy's build prefixes the global symbols and changes no source.
The libjxl BSD license and Google's additional patent grant follow, then the Highway
license (Patchy uses Highway under its BSD-3-Clause option), the Brotli MIT license and
the skcms BSD license.

```text
Copyright (c) the JPEG XL Project Authors.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

```text
Additional IP Rights Grant (Patents)

"This implementation" means the copyrightable works distributed by
Google as part of the JPEG XL project.

Google hereby grants to You a perpetual, worldwide, non-exclusive,
no-charge, royalty-free, irrevocable (except as stated in this section)
patent license to make, have made, use, offer to sell, sell, import,
transfer and otherwise run, modify and propagate the contents of this
implementation of JPEG XL, where such license applies only to those patent
claims, both currently owned or controlled by Google and acquired in
the future, licensable by Google that are necessarily infringed by this
implementation of JPEG XL.  This grant does not include claims that would be
infringed only as a consequence of further modification of this
implementation.  If you or your agent or exclusive licensee institute or
order or agree to the institution of patent litigation against any
entity (including a cross-claim or counterclaim in a lawsuit) alleging
that this implementation of JPEG XL or any code incorporated within this
implementation of JPEG XL constitutes direct or contributory patent
infringement, or inducement of patent infringement, then any patent
rights granted to you under this License for this implementation of JPEG XL
shall terminate as of the date such litigation is filed.
```

```text
Copyright (c) The Highway Project Authors. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1.  Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

2.  Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

3.  Neither the name of the copyright holder nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

```text
Copyright (c) 2009, 2010, 2013-2016 by the Brotli Authors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

```text
// Copyright (c) 2018 Google Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//    * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//    * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

--------------------------------------------------------------------------------
```

## Patchy's own GEGL (the gegl folder)

Patchy's packages carry GEGL 0.4.72 and babl 0.1.128 (<https://gegl.org>,
<https://gegl.org/babl>; GNU LGPL version 3 or later, except GEGL's `gegl-common-gpl3`
operation module, which is under the GNU GPL version 3 or later), as separate libraries that
`patchy-gegl-host` loads. On Windows and macOS they come with GLib and json-glib (GNU LGPL
version 2.1 or later), libintl from the gettext runtime and, on Windows, libiconv (GNU LGPL
version 2.1 or later), libffi (MIT), PCRE2 (BSD) and zlib (zlib license); the Linux AppImage
carries GLib, json-glib and PCRE2 built the same way and Ubuntu's libffi, and the Flatpak uses its runtime's
GLib and json-glib. GEGL is built from its release tarball without a few
operations, removed from its build lists by `scripts/dev/gegl/common.sh`; nothing else is
modified. The license texts (one per library, libintl's LGPL included) and the exact
versions are in the package's GEGL `licenses` folder (`gegl\licenses` beside `patchy.exe`
on Windows, with `SOURCES.txt`; in the macOS app, `Contents/Resources/gegl/licenses`; in the Flatpak,
`/app/share/licenses/com.rtsoft.patchy`; in the AppImage, `usr/lib/patchy/gegl/licenses`). The complete
corresponding source of all of them, with the scripts that built them, is
`PatchyGeglSources.zip`, an asset of every Patchy release at
<https://github.com/SethRobinson/Patchy/releases>, next to the downloads. Patchy's helper only calls these libraries
and you may replace them with your own builds. The helper's source is part of Patchy's own
source (MIT, a license compatible with the GPL), in the release's tag of
<https://github.com/SethRobinson/Patchy>.

## GEGL, babl and GLib interface declarations (patchy-gegl-host)

Patchy's GEGL helper (`src/plugins/gegl/host/`) declares for itself the function
signatures, structure layouts (`GParamSpec`, `GParamSpecInt`, `GParamSpecDouble`,
`GeglParamSpecInt`, `GeglParamSpecDouble`, `GEnumClass`, `GEnumValue`, `GValue`,
`GeglRectangle`) and type and enumeration constants it needs from the installed
public headers of GEGL 0.4, babl 0.1 and GLib/GObject 2.0, as installed with
GIMP 3.2.6. GEGL and babl are Copyright (C) the GEGL and babl authors, GLib is
Copyright (C) the GLib authors; all three are licensed under the GNU Lesser
General Public License (GEGL and babl version 3 or later, GLib version 2.1 or
later). LGPL-3 section 3 permits this use of a header's numerical parameters
and data structure layouts. No GEGL, babl or GLib code is compiled into the helper or any
other Patchy program; the helper loads the libraries above at run time.
