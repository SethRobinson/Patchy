# Format-law, codec, and trademark research record

Research record for Patchy's PSD format and copyright position, the HEIF codec boundary, and its trademark usage; each entry carries its date. Split from [patent-research.md](patent-research.md), whose disclaimer applies: this is engineering research, not legal advice. The binding summary lives in [legal-constraints.md](legal-constraints.md) ("Format, copyright, trademarks, and assets"); read that first and never act on a legal-adjacent feature from this record alone.

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
  and conditions it on device/OS capabilities. This lowers Patchy's
  codec-distribution exposure but does not establish that browser decoding is
  royalty-free for every distributor or territory. The binding engineering
  rule is therefore narrower than a legal conclusion: never ship the HEVC
  codec, never encode HEIC, retain LGPL compliance material, and obtain IP
  counsel if Patchy's distribution model or codec boundary changes.
- **Trademarks**: "Smart Object(s)", "Smart Filter(s)", "Filter Gallery", "Healing Brush", "Content-Aware", and "PSD" are absent from Adobe's published trademark list (May 21, 2026 edition; also absent from the 2009 edition) and from all 601 Adobe-owned US trademark records; the existing SMART FILTER / SMART OBJECT registrations belong to unrelated companies. PHOTOSHOP is registered (e.g. Reg. 1,850,242, renewed) and policed against generic-verb use and cybersquatting. Photopea uses the feature names verbatim commercially; Krita/Affinity rename theirs (File/Filter Layers, Live Filters), the conservative norm. Patchy's usage stays nominative (the terms describe Photoshop's own stored data), product branding contains no Adobe marks, and README.md carries the attribution notice in Adobe's preferred format. UI look-and-feel claims are weak doctrine (Apple v. Microsoft, 35 F.3d 1435 (9th Cir. 1994); Lotus v. Borland, 49 F.3d 807 (1st Cir. 1995)) and no Adobe UI trade-dress enforcement was found; original tool icons keep extra distance. The gallery's display name was deliberately changed from "Visual Filters & Looks" to "Filter Gallery" in July 2026 after this sweep confirmed "Filter Gallery" is not an Adobe mark (nominative use of a descriptive feature name, the Photopea precedent); persisted identifiers kept their original names.
