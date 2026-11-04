# File Info: watermarks

Split from [file-info.md](file-info.md): watermarks declared in C2PA metadata and the on-request pixel scan for the open Stable Diffusion invisible watermarks (known payloads, the decoder and its readings, the match rule, limits).

## Watermarks

Patchy never removes or masks a watermark (docs/legal-constraints.md, "Watermark detection").
Adobe TrustMark, read by the same Scan for Watermarks once its models are downloaded and added
by Image > Add Invisible Watermark, is in [watermarks.md](watermarks.md).

- Declared in metadata: a C2PA `c2pa.soft-binding` assertion (its `alg`, such as
  `com.adobe.trustmark.Q`) or an action named `c2pa.watermarked*` (`summarize_jumbf`) adds the
  observed item "Watermark declared in Content Credentials", privacy `watermark`. IPTC
  DigitalSourceType says how an image was made, not that it carries a mark: aiPrompt for a
  trained-model source, none otherwise.
- Pixel scan: the Watermarks page's Scan for Watermarks (`fileInfoScanWatermarksButton`,
  per-kind results in [watermarks.md](watermarks.md#scan-results)) and `doc.scanForWatermarks()`. It runs only on that
  request, never at open or per repaint: `Compositor::flatten_rgb8` of the document as it is
  now, then `scan_invisible_watermarks` (`src/core/invisible_watermark.cpp`). A hit adds a
  "Pixel scan" / "Invisible watermark" row (privacy watermark) to All Metadata and a banner
  sentence saying stripping metadata does not remove it. Nothing is stored or saved.
- Known payloads (ids permanent), bits MSB first as `numpy.unpackbits`: `stableDiffusion1`, the
  17 bytes "StableDiffusionV1" (CompVis/stable-diffusion `scripts/txt2img.py`, `img2img.py`);
  `stableDiffusion2`, "SDV2" (Stability-AI/stablediffusion scripts); `stableDiffusionXL`, the
  48-bit 0xB3EC907BB19E (Stability-AI/generative-models `WATERMARK_MESSAGE`, also diffusers'
  `StableDiffusionXLWatermarker`).
- Decoder: a Patchy-owned reimplementation of the decode side of ShieldMnt's
  invisible-watermark (MIT; `imwatermark/maxDct.py` `EmbedMaxDct.decode`, `watermark.py`, read
  October 2026); no code copied. OpenCV's 8-bit BGR-to-YUV in fixed point (Y = (1868 B + 9617 G
  + 4899 R + 2^13) >> 14, U = ((B - Y) 8061 + 128 * 2^14 + 2^13) >> 14), the level-1 Haar
  approximation of U over the top-left (h/4*4) x (w/4*4) pixels, then per 4 x 4 block of
  coefficients (8 x 8 pixels, row-major) the largest of the 15 non-DC coefficients (first on
  ties): bit 1 when its value mod 36 exceeds 18. Block n votes for bit n mod L; a bit is 1
  when ones * 255 > votes * 127, as the library's `mean * 255 > 127`. A coefficient is half a
  2 x 2 sum of U (never negative), so the test is `sum % 72 > 36` in integers: exact and the
  same on every toolchain. Only U is read: the library's scales are [0, 36, 36] but it loops
  `range(2)`. Images under 256 x 256 pixels (the library's minimum) are not scanned.
- Eight readings per payload (`DwtDctDecodeOptions`): both channel orders (callers that hand
  the library RGB as BGR); U measured from 0 (the SD scripts embed in 8 bits, U centered on
  128) or from OpenCV's float chroma zero, |sum - 510| (SDXL embeds in float BGR, where U is
  centered on 0.5 and coefficients can be negative, so an 8-bit reading flips those blocks);
  and two bit rules. The library's `mean * 255 > 127` misreads many 0 bits: a marked 1 reads 1
  in nearly every block, but setting a 0 can lower the block's largest coefficient below
  another one, which then reads 1 about half the time. Stability's own SDXL `detect.py` allows
  for this and calls 36 or more of 48 matching bits "very likely watermarked". The second rule
  (`TwoCluster`) puts the threshold where the per-bit averages split into two groups (largest
  between-group variance, exact integers). On the test images the library rule leaves 4 to 36
  bits wrong and the two-group rule none.
- Match rule: a payload is found when any reading has at most L/12 bits (rounded down)
  different: 11 of 136, 4 of 48, 2 of 32. On an unmarked image the decoded word is close to
  random, so it lands that close to a given payload with odds of about 1.2e-7 (32 bits),
  7.6e-10 (48) and far less (136) per reading; over the eight readings and three payloads the
  worst case stays near 1e-6 (the 32-bit SD 2.x payload). The core tests check photo-like,
  flat, gradient, checkerboard, noise and other-payload images. The embedding side exists only
  as a test reference (`tests/invisible_watermark_reference.hpp`) of both pixel paths.
- Limits: the 8-pixel block grid must line up with the original, so a crop off that grid, a
  resize, rotation or heavy repainting removes the mark. JPEG recompression does too: the mark
  lives in fine chroma detail, and in `ui_file_info_scans_for_watermarks` a marked SDXL image
  still decodes exactly after Qt's JPEG at quality 100 but keeps 6 of 48 bits wrong at 95 and
  19 at 90 (not found). Many watermarks have no public
  decoder (Google SynthID, Digimarc); the page says so under the results.
