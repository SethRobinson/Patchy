# Adobe TrustMark watermarks

Patchy reads and adds Adobe TrustMark invisible watermarks. It never removes one
([legal-constraints.md](legal-constraints.md#watermark-detection)). The open Stable Diffusion
watermark scan, the `watermark` privacy class and C2PA watermark declarations are in
[file-info.md](file-info.md#watermarks); File Info's Watermarks page shows both scans.

## Models (one optional download)

Adobe's own ONNX exports from github.com/adobe/trustmark (MIT for code and models; trained on
licensed Adobe Stock, per its FAQ), rehosted unchanged, under the `trustmark_` names, as assets of
the Patchy GitHub release **`ai-models-3`**
(`https://github.com/SethRobinson/Patchy/releases/download/ai-models-3/<name>`), with the same
rules as `ai-models-1`/`-2` ([ai-selection.md](ai-selection.md#models-and-runtime)): never marked
Latest, assets never replaced. Each file's fallback (`DownloadableModelFile::fallback_urls`, tried
only when the release link fails) is the Content Authenticity Initiative's model host
`https://cai-watermark.adobe.net/watermarking/trustmark-models/<upstream name>`, where
`scripts/setup-ai-deps.*` still fetch them. Adobe's MIT `LICENSE` travels with every copy (MIT's
one condition): the release carries it as `trustmark_LICENSE.txt` and the download saves it beside
the models (fallback: raw.githubusercontent.com at adobe/trustmark commit 6df5a0a2). The release
was staged October 23, 2026 for Seth to upload (desk card local-009); until then every download
uses the fallback. Pinned by size and SHA-256 in `src/ui/watermark_model_download.cpp`, keep in
sync with `scripts/setup-ai-deps.*`, `PATCHY_AI_WATERMARK_MODEL_FILES` (cmake/patchy_ai.cmake),
`trustmark_model_files()` and `packaging/windows/UninstallPatchy.cs` (which also lists the
license). "Adobe" and "TrustMark" only describe the files: no Adobe logo, nothing implying Adobe
endorses Patchy. The offer texts say "Adobe's, MIT License" and name the release
(`model_download_source_text`).

| File | Upstream | Size | SHA-256 |
|---|---|---|---|
| `trustmark_decoder_Q.onnx` | `decoder_Q.onnx` | 47,401,222 | `ee3268f057c9dabef680e169302f5973d0589feea86189ed229a896cc3aa88df` |
| `trustmark_decoder_P.onnx` | `decoder_P.onnx` | 47,400,467 | `be6d7c33f8a7b376f179e75f3f7c58ff816a9ac7bb6d37fd0a729a635f624c35` |
| `trustmark_decoder_C.onnx` | `decoder_C.onnx` | 22,457,887 | `3d92f4a278bcf87ac7dc40248c36ae664fe5fdedfd19ca265f7ca730b271100a` |
| `trustmark_encoder_Q.onnx` | `encoder_Q.onnx` | 17,312,208 | `19b3d1b25836130ffd78775a8f61539f993375d1823ef0e59ba5b8dffb4f892d` |
| `trustmark_LICENSE.txt` | `LICENSE` | 1,218 | `58d47a5cdfaaceda98925d845e8207d90336c62347a2383f95962b667b1cdaf5` |

134.6 MB in all (the host's files are dated September 28, 2026). Why these: Q is TrustMark's
default and the variant of Adobe's C2PA example; P (perceptual, higher quality) and C (compact)
are the other variants on the C2PA soft-binding algorithm list (`com.adobe.trustmark.Q/.P/.C`); B
only reproduces the paper. A mark decodes only with its own variant's decoder. Patchy embeds with
Q only. A host's files can change; a replaced file fails its hash and the download refuses it
(the release's assets never change, so only the fallback can drift).

**Not int8.** The graphs compute in float16 throughout (activations included, not just weights
behind Cast nodes), so ONNX Runtime's dynamic quantizer rejects them; int8 would need a float32
re-export and a Patchy-hosted copy. The float16 files decode the reference images exactly.

Download, folder, lookup and deletion follow the High Quality model
([ai-selection.md](ai-selection.md#the-high-quality-model-desktop-download)): `start_model_download`
into `downloaded_models_folder()` (`<AppDataLocation>/ai-models`), sessions in the one ONNX
translation unit (`segmentation_engine_onnx.cpp`, slots `OrtState::trustmark`), released before a
delete. Unlike the High Quality model, only the download folder is searched, never the bundled
`ai-models` folder: no package may carry these files. Offered from File Info's Watermarks page, the Add Invisible Watermark dialog, Preferences >
Metadata and Watermarks (Download and Delete, `preferencesWatermarkModelsGroup`) and `app.downloadWatermarkModels()`;
nothing fetches them unasked. The web build has no TrustMark. Build trees stage them into
`<build>/ai-models-download` when setup-ai-deps provisioned all four (all or none, separate from the
High Quality pair).

## Packet

100 bits: payload, BCH parity filling bits 0..95, two reserved zeros, two bits of schema code
(`core/trustmark_payload.*`, Patchy's own implementation of TrustMark's datalayer.py/bchecc.py).
BCH over GF(2^7), primitive polynomial 137, the payload zero-padded to whole bytes before the
parity is computed (bchlib's convention).

| Schema (code) | Payload bits | Text characters | Corrects |
|---|---|---|---|
| `BCH_SUPER` (0) | 40 | 5 | 8 bits |
| `BCH_5` (1, default) | 61 | 8 | 5 |
| `BCH_4` (2) | 68 | 9 | 4 |
| `BCH_3` (3) | 75 | 10 | 3 |

Text is 7-bit ASCII, 7 bits per character, most significant first; a payload that does not read
as printable text is shown as an ID (`bits_to_hex`, "0x" and the bits padded to whole hex
digits). Decoding reads the schema from the last two bits and tries only that code, as the
reference does. The tests pin packets the reference built ("mysecret", "PATCHY", a binary
payload, all four schemas).

## Scan

`scan_trustmark` (`src/ai/trustmark_watermark.cpp`) runs the Q, P and C decoders on the composite
(`Compositor::flatten_rgb8`), only when Scan for Watermarks or `doc.scanForWatermarks()` asks.
Per variant: the region is the whole image, or the centered square when one side is more than
twice the other (always for P); it is resized as PIL's bilinear resize does (a triangle filter
widened by the reduction factor, horizontal pass then vertical, 8-bit rounding after each) to
256 x 256 (224 for P), scaled to [-1, 1], NCHW. A bit is 1 when its logit is positive. Python's
PyTorch reference decodes Q at 245; the ONNX graphs take 256, as Adobe's Rust and C++ runners do.

A packet counts only when its BCH check passes **and** its mean |logit| is at least 3
(`kTrustMarkMinimumConfidence`). The BCH check alone passed on 2 to 3 of 120 random crops of
two unmarked photos per variant (always the weak `BCH_3` code, which accepts about 7% of random
words that claim it). Measured October 2026: marked images 9.3 to 10.9 (JPEG quality 90
included), unmarked crops at most 1.26.

## Scan results

Scan for Watermarks and `doc.scanForWatermarks()` share `scan_watermarks` /
`watermark_checks` (`src/ui/watermark_scan_report.*`): one row per kind, always all seven, in this
order (ids permanent): `stableDiffusion1`, `stableDiffusion2`, `stableDiffusionXL`, `trustmarkQ`,
`trustmarkP`, `trustmarkC`, `c2paDeclared` (a `c2pa.soft-binding` or `c2pa.watermarked*`
declaration in the file's metadata, [file-info.md](file-info.md#watermarks)). Status `found`,
`notFound` or `notChecked`; a `notChecked` row has a reason (permanent tokens): `imageTooSmall`
(the Stable Diffusion rows under 256 x 256), `modelsNotDownloaded`, `notSupported` (a build
without ONNX Runtime, the web build) or `failed` (the decoder threw). Found rows show the bits
matched (Stable Diffusion) or the payload, schema and confidence (TrustMark).

File > Scan for Watermarks... (`fileScanForWatermarksAction`, hotkey id
`file.scan_for_watermarks`, no default key, under File Info) and the document tab's menu
([document-tabs.md](document-tabs.md), for the clicked document) open File Info on this page
with the scan already run, as the check-on-open notice does.

File Info: a summary line (`fileInfoWatermarkScanResult`) over a three-column grid
(`fileInfoWatermarkScanResults`; labels `fileInfoWatermarkCheckName`, `...Status`, `...Detail`
with properties `watermarkCheck` = id and `watermarkStatus` = token). Status is a word; Found
also gets the warning banner roles as a tag and Not checked `@hint_text`, so nothing relies on
color. The Download button sits under the grid while the models are missing, and downloading
reruns a scan already shown. The rows are rebuilt on every repopulate, so the metadata row
follows a staged Strip All Metadata. The page ends with `watermark_scan_limits_note()`: SynthID
and Digimarc have no public reader, so Not found everywhere is not "no watermark".

## Embed

`embed_trustmark_q`, as the Python reference's `encode`: the variant Q region (whole image unless
past 2:1), resized to 256 like the decoder input; one encoder run with the 100 packet bits
(inputs `onnx::Concat_0`, `onnx::Gemm_1`, output `image`); residual = clamp(stego, -1, 1) minus
the cover, minus its per-channel mean (no color shift); bilinear upsampling to the region (torch
`interpolate`, align_corners false); added at `strength` (1.0 is TrustMark's default; Adobe uses
1.5 for print), clipped; the region's border feathered over 1% of its shorter side (1 to 50 px),
columns overriding rows at the corners. Pixels round to nearest (the reference truncates, which
darkens by half a level). Alpha is untouched. The round-trip test changes pixels by 0.34 levels on
average and reads the payload back.

Image > Add Invisible Watermark... (`image.add_invisible_watermark`, no default key,
`imageAddInvisibleWatermarkAction`; `main_window_watermarks.cpp`, `invisible_watermark_dialog.cpp`)
marks the active pixel layer as one undo step. It needs an 8-bit document and an unlocked pixel
layer that fills the image (the mark is read back from the whole composite). The dialog: what
TrustMark does and that it has no key (anyone can read the payload), error correction (default
`BCH_5`), text with the characters left or a random ID (`QRandomGenerator::system`, New ID),
strength 50 to 150%. Without the models it shows the download and keeps Add Watermark disabled.

## Preferences

Preferences > Metadata and Watermarks, the "Watermarks" group under "Saved metadata"
([file-info.md](file-info.md#saved-metadata-preference)) (`main_window_preferences.cpp`, applied
on OK; `ui/watermark_settings.*`).
Settings keys, permanent: `watermarks/payloadText` (printable ASCII, at most 8 characters, the
`BCH_5` capacity), `watermarks/addOnSave` (`never` default, `ask`, `always`; also the scripting
tokens) and `watermarks/checkOnOpen` (false). The models row moved here from Preferences > Tools; a
note under it appears when a chosen option needs the models and they are missing (the Stable
Diffusion check needs none).

**Mark on save** (`watermark_for_save`, `watermarked_save_document`): `save_document_to_path` decides
before the write, so Save, Save As and Export all follow it, and passes a `SaveWatermark` to
`DocumentSaveCoordinator::write_prepared`. On the writer's worker the flat branch flattens
(`flat_export_qimage`, alpha kept where the format keeps it), embeds the Q mark at strength 1.0 and
hands the writer a one-layer stand-in with the document's file info, color state and print
settings. The open document, its undo history and its saved state never change. Marked: PNG, JPEG,
WebP, BMP, flat TIFF, JPEG XL, AVIF (`save_format_takes_watermark`). Not marked, by decision
(October 23, 2026): layered saves (PSD, PSB, layered TIFF, Aseprite) because the mark would change
layer pixels the user never sees; SVG, PDF, GIF, palette and 16/32-bit documents, animations,
textures and icons. Skipped with a status note: no text set, images under 128 px on a side, a
composite that already carries the same text (a Q decode first), or models missing. Recovery
snapshots never pass through this path. Ask shows `saveWatermarkPrompt` (Add Watermark, Save
Without, "Remember this choice" turns the mode into Always or Never); scripted and unattended saves
never ask and skip in Ask mode, while Always marks them too.

**Check on open** (`start_open_watermark_check`): after `open_document_path` and the multi-file
open add a session, a tracked worker scans a copy-on-write snapshot (`document_for_write`):
`flatten_rgb8`, the Stable Diffusion scan, then TrustMark when downloaded, cancellable through an
`InferenceCancel`. Nothing runs on the UI thread or per repaint. `watermarkCheckIndicator` (an
`ActivitySpinner` and "Checking for watermarks...") sits in the status bar while any check runs.
A hit opens `watermarkFoundNotice`, a non-modal self-deleting message box naming each mark, whose
Show in File Info opens File Info on the Watermarks page with the scan already run
(`FileInfoWatermarkSupport::open_watermarks_and_scan`). Closing the document cancels its check
and drops its result; CLI runs (`unattended_automation`) never check.

## Scripting

`doc.scanForWatermarks().trustmark` = `{available, marks: [{variant, schema, text, id, bits,
correctedBits, confidence}]}`, and `.checks` the rows above (`{id, name, status, reason,
detail}`, plus `match` or `mark` when found and `declarations` on the C2PA row; a TrustMark
failure still throws); `doc.addInvisibleWatermark({text?, id?, schema?, strength?})` returns
`{schema, text, id}` (neither text nor id: a random ID); `app.downloadWatermarkModels()`,
`app.watermarkModelsAvailable`; `app.watermarkPreferences` (`{text, addOnSave, checkOnOpen}`) and
`app.setWatermarkPreferences({...})`. See [scripting-api-changes.md](scripting-api-changes.md).

## Limits

TrustMark scales the whole image (or its center square) to 256, so it survives resizing and
JPEG (the reference images still decode exactly at quality 90) but not a crop that changes the
region much, heavy repainting, or a mark from another variant's model (B is not downloaded).
Adobe's watermark remover model is never downloaded or run.

## Tests

Core: `trustmark_*` in `tests/core/trustmark_tests.cpp` (packets, BCH up to its limit, text and
hex, region and resize, residual; with the models staged: the reference fixtures and an embed
round trip). Fixtures: `local-test-fixtures/trustmark/` (README.txt there; made by Adobe's Python
package 0.9.2 with `make_fixtures.py` from the CC0 photos in `test-fixtures/readme`, variants Q,
P and C, all four schemas, PNG and JPEG quality 90, plus an unmarked photo). The tests skip
without the models or fixtures. UI: `ui_watermark_scan_report_rows` (every status and reason,
synthetic), `ui_file_info_trustmark_*`, `ui_invisible_watermark_*`,
`ui_watermark_preferences_page_persists`, `ui_watermark_on_save_marks_flat_files_only`,
`ui_watermark_check_on_open_shows_a_notice`;
scripting: `ui_script_trustmark_round_trip`.
