# Format-law, codec, and trademark research record

Research record for Patchy's PSD format and copyright position, the HEIF codec boundary, the JPEG XL codec review, and its trademark usage; each entry carries its date. Split from [patent-research.md](patent-research.md) and written to the same conventions: an engineering record of the designs and of the published patents and licences considered. The binding summary lives in [legal-constraints.md](legal-constraints.md) ("Format, copyright, trademarks, and assets"); read that first and never act on a patent-adjacent feature from this record alone.

- **Format and copyright**: Adobe's "Photoshop File Formats Specification" (https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/, November 2019 edition, confirmed live July 2026) is a public page with no click-through whose preface states it is provided for third parties to read and write the format; the restrictive 2004-2006 SDK EULA bound only its signatories. File formats are not copyrightable expression (Feist; 17 USC 102(b); Lotus v. Borland; Google v. Oracle; CJEU SAS Institute v. World Programming C-406/10), and Patchy's COM byte-diff method observes a licensed Photoshop's output without decompiling, so the reverse-engineering safe harbors (Sega v. Accolade, Sony v. Connectix, DMCA 1201(f), EU 2009/24/EC art. 5(3)/6) are belt-and-suspenders on top. Rules that keep this clean: no verbatim Adobe spec text in the repo (link it instead), self-authored fixtures only, no PSDC cloud-document support. Enforcement record: no documented Adobe action against any third-party PSD implementation or Photoshop clone (GIMP, Krita, Affinity, Corel, Apple, Photopea all ship PSD support; Photopea has sold a direct in-browser clone since 2013 untouched); Adobe's actual enforcement targets are piracy, DRM circumvention, cybersquatting, and trademark misuse.
- **HEIF-in-WASM boundary reassessed 2026-07-31**: the earlier blanket
  "never vendor libheif" rule conflated a container parser with an HEVC codec.
  Patchy's browser build may distribute libheif under LGPL-3.0-or-later when
  the complete modified source and license are conveyed, Patchy's MIT-licensed
  corresponding application source and build instructions permit relinking,
  and the combined work carries the required notices. The reviewed build
  enables only libheif's Emscripten WebCodecs adapter. That adapter parses HEIF
  and HEVC configuration/NAL framing but delegates picture decoding to the
  browser's `VideoDecoder`; no libde265, x265, FFmpeg decoder, encoder, or
  other software codec backend is linked. The
  [W3C HEVC WebCodecs registration](https://w3c.github.io/webcodecs/hevc_codec_registration.html)
  makes HEVC support optional and defines `hvc1.*`/`hev1.*` configuration, so
  Patchy checks `VideoDecoder.isConfigSupported()` and performs no fallback.
  [Apple documents WebCodecs HEVC in Safari 17.4](https://developer.apple.com/documentation/safari-release-notes/safari-17_4-release-notes);
  [Chromium documents platform HEVC](https://groups.google.com/a/chromium.org/g/blink-dev/c/YJ1QijNiHeM/m/HWs-wQBfAAAJ)
  and conditions it on device/OS capabilities. This narrows Patchy's
  codec-distribution question but does not establish that browser decoding is
  royalty-free for every distributor or territory. The binding engineering
  rule is deliberately narrow: never ship the HEVC codec, never encode HEIC,
  retain LGPL compliance material, and re-review if Patchy's distribution
  model or codec boundary changes.
- **JPEG XL (ISO/IEC 18181) reviewed 2026-10-15**, before any code. Sources were fetched that day unless marked.
  - *Licence and grant*: libjxl v0.12.0 (2026-07-01) is BSD-3-Clause ([LICENSE](https://github.com/libjxl/libjxl/blob/v0.12.0/LICENSE)) plus Google's [PATENTS](https://github.com/libjxl/libjxl/blob/v0.12.0/PATENTS) grant. The grant is perpetual, worldwide, no-charge and royalty-free, and covers making, using, selling, importing, modifying and distributing. It reaches only Google's claims that this implementation necessarily uses, excludes claims caused by further modification, and terminates for anyone who brings a patent claim against the implementation. It is code-scoped, so an independent JPEG XL implementation gets nothing from it. Cloudinary's contributions reach users through Google's [corporate CLA](https://cla.developers.google.com/about/google-corporate) (claims covering the contributed code), not through `PATENTS`.
  - *Dependencies*: the library needs only Brotli (MIT), Highway (Apache-2.0 or BSD-3; elect BSD-3), and skcms (BSD-3, default) or lcms2 (MIT, `JPEGXL_ENABLE_SKCMS=OFF`; Patchy already vendors it). None is copyleft and all permit commercial static linking. libpng, zlib, sjpeg and libjpeg-turbo serve the tools only. A library-only build turns off tests, tools, benchmark, examples, docs, sjpeg, OpenEXR, JNI, fuzzers, plug-ins and `BUILD_SHARED_LIBS`, keeps `JPEGXL_FORCE_SYSTEM_*` off, and sets `JPEGXL_BUNDLE_LIBPNG=OFF` for Emscripten (on by default there).
  - *Standard declarations*: the ISO and IEC patent databases block automated fetches, so the 18181 declarations are **unverified**. The spec editor told [The Register (2021-03-13)](https://www.theregister.com/2021/03/13/microsoft_ans_patent/) that the only declarations came from Google and Cloudinary, both royalty-free, and no RAND declaration was found. [jpeg.org/jpegxl](https://jpeg.org/jpegxl/) calls the reference software royalty-free. No assertion, litigation or pool against JPEG XL was found (news and web only, no docket search).
  - *Microsoft US 11,234,023 B2* ([Google Patents](https://patents.google.com/patent/US11234023B2/en)): filed 2019-06-28, granted 2022-01-25, expiry about 2039-06-28. Independent claims 1, 19 and 20 claim only a two-phase rANS *decoder*. Phase one updates the state with the *previous* iteration's symbol probability, depending on whether that symbol was generated. Phase two merges input into the state and generates the current symbol only when the state passes a sufficiency threshold. libjxl's `ANSSymbolReader` ([dec_ans.h](https://github.com/libjxl/libjxl/blob/main/lib/jxl/dec_ans.h)) derives every symbol unconditionally from the low 12 state bits through a static per-context alias table. It then updates the state with that same symbol's frequency and offset and refills 16 bits when the state falls below 2^16. There is no deferred update, no generated-or-not test, no conditional output, and no probability adaptation; dependent claim 12's byte refill and 32-bit state also differ. No independent claim element matches. The structures also differ: the claimed structure is a hardware pipeline that can stall symbol output, which the invariant-driven software loop never does. Continuation US 17/552,295 ([US20220109891A1](https://patents.google.com/patent/US20220109891A1/en), still shown pending on 2026-10-20; status not confirmed at USPTO) claims a header syntax element that signals whether to flush and re-initialize the rANS state. JPEG XL has no such element: each stream reads a fixed 32-bit initial state and must end at a fixed value. Microsoft made no ISO declaration and has made no public statement on JPEG XL. Prior art: Duda 2013-2014, Giesen 2015.
  - *Other families*: Cloudinary [US 12,243,274 B2](https://patents.google.com/patent/US12243274B2/en) (priority 2021-12-07, expiry about 2043-11-15) claims encoder-side steps only: classify pixels as photographic or not, build a cluster map, and code the two classes with different techniques. It is outside `PATENTS`, and whether libjxl's patch or dot detection has those steps was not determined; the condition is that those stay off and Patchy never adds such a split. Google [US 12,236,554 B2](https://patents.google.com/patent/US12236554B2/en) (adaptive quantization, expiry about 2040) is covered for unmodified libjxl. No patent specifically claiming XYB, MA trees or lossless JPEG recompression was found.
  - *Platform alternative rejected*: Windows reads and writes JPEG XL only through the Store "JPEG XL Image Extension" on Windows 11 24H2 ([PCWorld 2025-03-03](https://www.pcworld.com/article/2625216/microsoft-adds-jpeg-xl-support-to-windows-11-with-a-quick-download.html)); it is not in Microsoft's [native WIC list](https://learn.microsoft.com/en-us/windows/win32/wic/native-wic-codecs). macOS 14 ImageIO decodes but no encode path was found. Linux has nothing, and Qt's qtimageformats has no plug-in (KDE kimageformats' `jxl.cpp` is BSD-2 over libjxl). Browsers decode (Safari 17, [Chrome 155](https://developer.chrome.com/blog/jpeg-xl-in-chrome), [Firefox 158](https://www.firefox.com/en-US/firefox/158.0/releasenotes/)) but cannot encode. A JXR-style platform-only path would therefore leave most users without the format, and vendoring was chosen.
  - *Trademark*: no registration of "JPEG" or "JPEG XL" was found (USPTO, EUIPO and Justia searches were partly blocked). [jpeg.org/terms](https://jpeg.org/terms.html) treats committee names and logos as proprietary. Naming the format is nominative; ship no committee logos.
  - *Proposed implementation, for a later code task*: `cmake/patchy_jxl.cmake` modelled on `patchy_webp.cmake` (a pinned private static archive with prefixed symbols), a Qt-free `src/formats/jxl_document_io.{hpp,cpp}`, a registry row with a writer, and a new `docs/jxl.md`, because `file-formats.md` has room only for a pointer.
- **Trademarks**: "Smart Object(s)", "Smart Filter(s)", "Filter Gallery", "Healing Brush", "Content-Aware", and "PSD" are absent from Adobe's published trademark list (May 21, 2026 edition; also absent from the 2009 edition) and from all 601 Adobe-owned US trademark records; the existing SMART FILTER / SMART OBJECT registrations belong to unrelated companies. PHOTOSHOP is registered (e.g. Reg. 1,850,242, renewed) and policed against generic-verb use and cybersquatting. Photopea uses the feature names verbatim commercially; Krita/Affinity rename theirs (File/Filter Layers, Live Filters), the conservative norm. Patchy's usage stays nominative (the terms describe Photoshop's own stored data), product branding contains no Adobe marks, and README.md carries the attribution notice in Adobe's preferred format. UI look-and-feel claims are weak doctrine (Apple v. Microsoft, 35 F.3d 1435 (9th Cir. 1994); Lotus v. Borland, 49 F.3d 807 (1st Cir. 1995)) and no Adobe UI trade-dress enforcement was found; original tool icons keep extra distance. The gallery's display name was deliberately changed from "Visual Filters & Looks" to "Filter Gallery" in July 2026 after this sweep confirmed "Filter Gallery" is not an Adobe mark (nominative use of a descriptive feature name, the Photopea precedent); persisted identifiers kept their original names.
- **JPEG 2000, OpenEXR and Radiance HDR reviewed 2026-10-16**, before the code landed.
  - *JPEG 2000*: ISO/IEC 15444-1 (2000) was standardized under a royalty-free baseline
    agreement among the Part 1 patent holders (the JPEG committee's stated goal for Part 1;
    Part 2 extensions were never covered). The core Part 1 patents, filed 1996-2000, have
    expired by their 20-year terms. OpenJPEG (UCLouvain, BSD-2-Clause) is the ISO reference
    implementation and ships in Chrome's PDF viewer, GDAL, Ghostscript and most Linux
    distributions, which is the distribution precedent Patchy relies on. Only the Part 1
    decoder/encoder in `src/lib/openjp2` is vendored; HTJ2K (Part 15, `ht_dec.c`) comes with
    it as upstream ships it and is decode-only. Unverified: the ISO patent database itself
    (blocked to automated fetches, as with JPEG XL).
  - *OpenEXR*: an open ILM format since 2003 (BSD-3 reference library, Academy Software
    Foundation since 2019); no patent declarations are known for the format or the PIZ/ZIP/RLE
    codecs Patchy reads. The B44 and DWA codecs are not implemented by tinyexr and not read.
  - *Radiance HDR*: Greg Ward's 1980s RGBE encoding, published in Graphics Gems II (1991);
    no patents. Patchy's reader and writer are its own code.
  - Binding engineering rule in [legal-constraints.md](legal-constraints.md).
- **AVIF / AV1 decoding reviewed 2026-10-16** (GitHub issue 120), decided the same day.
  - *Grant*: the AV1 specification is published by the Alliance for Open Media under the
    [AOM Patent License 1.0](https://aomedia.org/license/patent-license/), a royalty-free grant
    from every member (Google, Amazon, Apple, Microsoft, Netflix, Mozilla, Intel, Nvidia,
    Samsung and others) to implementers of the specification, with a defensive termination
    clause. libavif (AOMediaCodec, BSD-2-Clause) and dav1d (VideoLAN, BSD-2-Clause) carry no
    separate patent file; dav1d relies on the AOM grant like every AV1 decoder.
  - *Counter-claims*: Sisvel's "AV1 patent pool" (announced 2019, licensors including Philips,
    JVCKenwood, Dolby, Orange, Toshiba) asserts that non-member patents cover AV1; AOMedia
    disputes this and no AV1 decoder distributor has been sued in the reviewed record.
    Precedent relied on: GIMP, Krita, Firefox, Chrome, ImageMagick, GNOME and KDE image
    plug-ins all ship software AV1 decoders (dav1d or libaom) without a Sisvel licence.
  - *Scope chosen*: dav1d decodes; libaom 3.15.2 (AOMedia's reference encoder, BSD-2 plus the
    AOM Patent License 1.0, which grants royalty-free rights to its members' Necessary Claims
    for any implementation of the specification and terminates for a licensee who sues over
    AV1) encodes stills. Writing was added the same day on the GIMP precedent (GIMP encodes
    AVIF through libheif with libaom or rav1e). libavif's gain-map and sample-transform code
    compiles but nothing calls it. No AV2 (avm) code.
- **PQ and HLG decoding read 2026-10-21** (HDR Track A: `formats/hdr_transfer`, the AVIF
  reader, the macOS HEIF reader), an engineering read, not a claim-level review.
  - *What Patchy does*: decode only. It converts samples a decoder already produced (libavif,
    ImageIO) from the PQ or HLG signal to linear light with the published formulas, plus a
    primaries matrix. Nothing encodes PQ or HLG, nothing maps HDR to a display, and nothing
    reads dynamic HDR metadata (HDR10+, Dolby Vision) or applies gain maps.
  - *PQ*: SMPTE ST 2084 (2014), originated at Dolby, also ITU-R BT.2100 (2016). The SMPTE and
    ITU patent declarations were not fetched in this read, so PQ's patent status is
    unverified. Precedent relied on: the same decode already ships inside Patchy through
    libjxl (JPEG XL PQ files) and Qt 6.8's QtGui (`QColorSpace::TransferFunction::St2084`),
    and ships in Chrome and Skia (skcms), Firefox, ffmpeg/zimg, libplacebo, Krita and darktable,
    and in the macOS and Windows color pipelines; no assertion against a decoder was found.
  - *HLG*: ARIB STD-B67 (2015) and BT.2100, developed by the BBC and NHK, which describe it as
    royalty-free.
  - *Gain maps* (ISO 21496-1, Apple's HEIC gain map, Ultra HDR JPEG, libavif's gain-map code):
    not applied anywhere; adding them needs its own review first.
  - Binding rule in [legal-constraints.md](legal-constraints.md).
