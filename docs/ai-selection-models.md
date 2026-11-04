# Automatic selection: models and runtime

Split from [ai-selection.md](ai-selection.md): the model files with their provenance, licenses and release rules, the High Quality model download, the int8 and fp16 measurements, build provisioning and run-time loading of ONNX Runtime, measured performance and platform packaging.

## Models and runtime

BiRefNet's `ai-models-1` asset has been withdrawn. Installed apps never fetch it; existing local copies still work in opt-in builds. Older source checkouts must omit its setup entry. The original release's license and checksum records remain for prior downloads.

Patchy ships the MobileSAM int8 pair, pinned in `scripts/setup-ai-deps.ps1` / `.sh`, `cmake/patchy_ai.cmake` and the Flatpak manifest (keep all four in sync). The files are assets of the GitHub release **`ai-models-1`** (`https://github.com/SethRobinson/Patchy/releases/download/ai-models-1/<file>`), a release that must never be marked Latest (the README and `latest_version.json` download through `/releases/latest/`) and whose assets are never replaced: a new model set gets a new tag (`ai-models-2`) and new pins. Nothing is committed to git.

| File (in `ai-models/`) | Size | Converted from | License | SHA-256 |
|---|---|---|---|---|
| `mobile_sam_image_encoder_int8.onnx` | 14.4 MB | huggingface.co/Acly/MobileSAM `mobile_sam_image_encoder.onnx` (28.2 MB; preprocessing built in: HWC float 0..255, longest side 1024, the graph normalizes and pads) | MIT export of Apache-2.0 MobileSAM | `6af3f249...930cd7` |
| `sam_mask_decoder_multi_int8.onnx` | 4.8 MB | same repo, `sam_mask_decoder_multi.onnx` (16.5 MB; standard SAM decoder, 4 masks + IoU predictions) | Apache-2.0 (SAM decoder) | `aa1410be...c10a6` |
| `birefnet_lite_general_1024_int8.onnx` (held back; not provisioned or packaged) | 143.2 MB | github.com/ZhengPeng7/BiRefNet release v1 `BiRefNet-general-bb_swin_v1_tiny-epoch_232.onnx` (224 MB) | MIT | `0374fa92...27fed5e` |
| ONNX Runtime 1.30.0 CPU | | github.com/microsoft/onnxruntime releases (win-x64, win-arm64, osx-arm64, linux-x64, linux-aarch64, SHA-256 pinned per package) | MIT (ThirdPartyNotices.txt ships beside it) | per package in the scripts |

`scripts/ai/quantize_models.py` reproduces the three int8 files byte for byte from the pinned upstream fp32 files (it checks their SHA-256): dynamic int8, weights int8 per channel, MatMul/Gemm only. BiRefNet's export routes its MatMul weights through Identity nodes; the script removes those first, otherwise the quantizer keeps every float weight beside its int8 copy and the file grows to 253 MB. The script is dev tooling (Python, `onnxruntime==1.30.0`, `onnx==1.23.2`); builds only download.

Rejected for licensing: EdgeSAM (S-Lab, non-commercial), RMBG-1.4/2.0 (non-commercial), SAM 3 (restricted), CorridorKey (CC BY-NC-SA-based, no redistribution). MobileSAM's decoder graph still has SAM's `mask_input`/`has_mask_input` inputs; the engine always feeds zeros and 0 (a legal rule, not an optimization).

### The High Quality model (desktop download)

Object Selection has two models. **Standard** is MobileSAM, bundled everywhere (and the only one on the web). **High Quality** is EfficientViT-SAM XL0 (MIT Han Lab, Apache-2.0; image encoder replaced, SAM's prompt encoder and decoder kept; legal record in [patent-research-selection.md](patent-research-selection.md)). It is never packaged: desktop users download it once, on request, from the release **`ai-models-2`** into `<AppLocalDataLocation>/ai-models` (`downloaded_models_folder()`). It runs when its files are present and `tools/objectSelectModel` is not `standard`; the choice is the user's, never derived from the image.

| File | Size | Converted from (huggingface.co/mit-han-lab/efficientvit-sam @ a2f0c592) | SHA-256 |
|---|---|---|---|
| `efficientvit_sam_xl0_encoder_fp16.onnx` | 226.1 MB | `onnx/xl0_encoder.onnx` (451.8 MB, `b39add8f...`) | `a28cf697...5bf101` |
| `efficientvit_sam_xl0_decoder_multi_fp16.onnx` | 8.4 MB | `onnx/xl0_decoder.onnx` (16.5 MB, `051bfae9...`) | `632613ec...ba496b` |

- **Why fp16, not int8.** Weights are float16 behind Cast nodes ONNX Runtime folds at load, so it runs in float32. Measured on 14 photo prompts (October 2026): fp16 masks match fp32 at IoU 0.9987 mean (0.995 worst); int8 weight-only per channel collapses to 0.43 (embedding cosine 0.28; 32-weight blocks still 0.84), because the ReLU linear attention amplifies weight rounding. Keeping the 33 most sensitive convolutions fp16 and the rest int8 measured 148 MB compressed at IoU 0.998, but its layer choice was fitted on one photo: unshipped until validated on a wider set.
- **The decoder** export picked the best mask in-graph; `quantize_models.py` exposes all four masks (`low_res_masks`, 256x256 over the padded frame) and `iou_predictions` instead, so the choice follows `best_sam_candidate` like MobileSAM. It has no mask input.
- **Input.** NCHW, ImageNet mean/std, longest side 1024, zero-padded to 1024 square (`padded_sam_encoder_input`). Masks map straight from the low-res grid to document pixels in one bilinear pass (`document_mask_from_low_res_logits`; padding cells never read).
- **Download** (`src/ui/object_selection_model_download.cpp`): pinned URL, size and SHA-256 per file (keep in sync with `setup-ai-deps.*` and `PATCHY_AI_DOWNLOAD_MODEL_FILES`), plus optional `fallback_urls` tried in order when a link fails (unreachable, HTTP error, wrong size or hash), streamed through `QSaveFile` with a running hash and committed only on a match; files already present and matching are skipped. Progress (`ModelDownloadProgress`) names file N of M, its size and the link being fetched, reported before each file's first byte.
- **Progress dialog** (`ModelDownloadDialog`, `src/ui/model_download_dialog.*`): every download the user starts (this model, the watermark models, a one-click update) shows the heading, "File N of M: name (size)", and under "From:" the full link (`modelDownloadLinkLabel`, before redirects, so a GitHub release link and not the storage host's signed one): selectable when it fits, elided in the middle otherwise, the full link always in the tooltip, the context menu and Copy Link. Cancel and Escape reject, which cancels the job. Offer and Preferences texts name the source before anything starts (`model_download_source_text`: "github.com/SethRobinson/Patchy (release ai-models-2)"). Redirects are followed by hand so every host, GitHub's storage host included, resolves on a detached thread first (`host_resolve.hpp`, GitHub issue 48; the update checker uses the same helper). Transfer timeout 60 s without bytes.
- **UI.** The first time the tool is chosen, or right-click Select Object is first used, on a desktop build without the model, one dialog offers the download (never during unattended automation, nor offscreen unless a test opts in through `MainWindowTestAccess`; `automaticSelection/highQualityModelOffered` remembers it, for both). Select Object then solves with whichever model the answer left in place. The options bar's Model combo shows the model that runs; choosing High Quality without the files offers the download again. The download runs under the window-modal progress dialog above. Scripts: `app.downloadObjectSelectionModel()` (status-bar progress) and `doc.selection.objectSelectionModel`.
- **Preferences > Tools** shows whether it is downloaded, with Download (progress dialog modal to Preferences) and Delete. Delete also removes `automaticSelection/highQualityModelOffered` and resets `tools/objectSelectModel` to High Quality, so the tool offers the download again on its next first use and a new download is used at once.
- **Deleting it.** `release_high_quality_model()` (sessions are shared-owned, so a run in flight finishes on its own reference), then `delete_downloaded_high_quality_model` removes exactly the two files by full name and the folder only when it is then empty (plain rmdir; never a wildcard or recursive delete). The Windows uninstaller does the same in `%LOCALAPPDATA%\RTsoft\Patchy\ai-models`, where downloads land, and in `%APPDATA%\RTsoft\Patchy\ai-models`, the roaming folder downloads used through 1.19, each in its own try/catch (`packaging/windows/UninstallPatchy.cs` repeats the names). macOS has no uninstaller; the Flatpak's copy lives in its data folder, which `flatpak uninstall --delete-data` removes.
- **Watermark models.** The TrustMark watermark models are a second, separate download into the same folder, through the same `start_model_download` and ONNX translation unit: [watermarks.md](watermarks.md).
- **Lookup.** `high_quality_model_path` tries the download folder, then the bundled `ai-models` folder (a packager may ship it). Build trees stage the files into `<build>/ai-models-download` when `setup-ai-deps` provisioned them; tests point the engine there. That folder is never packaged.

### Why int8, and which int8 (measured October 2026)

Compared with fp32 on real photos (a 3000x1820 photo, the Vermeer IFF fixture, the San Francisco readme photo), same prompts, IoU of the 50% masks against fp32, i9-12900KS:

| Variant | Size (deflate) | IoU vs fp32 | Speed |
|---|---|---|---|
| MobileSAM fp32 | 44.7 MB (36.7) | reference | encode 410 to 450 ms, decode 75 ms |
| **MobileSAM int8, MatMul/Gemm (chosen)** | **19.2 MB (12.4)** | **0.9935 to 0.9998**, mean alpha diff under 1/255 | encode about 10 to 15% faster, decode same |
| MobileSAM int8 including Conv | 16.2 MB | as low as 0.02 (clicks select the wrong region) | encode 3x slower |
| BiRefNet fp32 | 224 MB (165) | reference | 5.5 to 6.1 s |
| **BiRefNet int8, MatMul/Gemm (chosen)** | **143 MB (88)** | **0.997 to 0.9999** | about 5 to 7% faster |
| BiRefNet int8 including Conv | 93 MB (39) | 0.997 to 0.9997 | 2.3x slower (ConvInteger is slow on CPU) |
| BiRefNet static QDQ int8 (MinMax calibration) | 94 MB (41) | 0.94 on the Vermeer (bleeds into the background) | no faster, 8 s load |

Differences in the chosen variants are sub-pixel edge shifts. Not measured: Apple Silicon and pre-VNNI x86 speed (results stay correct; the speedup may differ). About 43 MB of BiRefNet is float Constant nodes (Swin position tables) that dynamic quantization does not touch.

### Build provisioning and run-time loading

`setup-ai-deps` provisions `<main checkout>/.deps/onnxruntime/<ver>/<platform>/` and `.deps/ai-models/`; worktrees find the main checkout through `git rev-parse --git-common-dir`, and CMake searches both `.deps` folders. Packagers pass `PATCHY_ONNXRUNTIME_DIR` and `PATCHY_AI_MODELS_DIR`. `PATCHY_ENABLE_AI_SELECTION` (default ON) and `PATCHY_REQUIRE_AI_SELECTION` (default OFF; release builds turn it on so a missing model is a configure error) control the build. `patchy_ai_runtime` is the copy-once target staging `ai-models/` and the runtime libraries into the build directory; `patchy_copy_ai_runtime(target)` attaches it.

ONNX Runtime is **loaded at run time, never linked** (`ORT_API_MANUAL_INIT`, `LoadLibraryExW` by full path beside the executable on Windows, since System32 can hold an unrelated older `onnxruntime.dll`; `dlopen` from beside the executable, `../Frameworks`, then by plain name elsewhere). A machine that cannot load it still runs Patchy and reports the feature unavailable. This matters on macOS: the 1.30 dylib requires macOS 14 while Patchy targets 12, so macOS 12 and 13 run without automatic selection.

At run time `configure_automatic_selection_models()` (main_window_ai_selection.cpp) points the engine at `ai-models` beside the executable, `../Resources/ai-models` (macOS bundle) or `../share/patchy/ai-models` (Linux install). Tests set the folder themselves.

## Measured performance (dev box i9-12900KS, 24 threads, Windows)

fp32 numbers come from an idle box; the int8 rows from an fp32/int8 run alternated under the same background load, so compare within a row pair, not across the table.

| Step | fp32 | int8 (chosen) |
|---|---|---|
| MobileSAM encode, 1024x768, session warm | about 200 ms idle (330 ms at 6 threads); 394 ms loaded | 320 ms loaded |
| MobileSAM session load | about 290 ms | about 290 ms |
| SAM decode per click or box (warm) | 30 to 100 ms; the first call loads the decoder (about 400 ms) | same |
| EfficientViT-SAM XL0 encode (fp16 weights, any image: the frame is always 1024 square) | about 1.5 s (1.3 to 2.2 s over 14 photos, Python harness, October 2026); session load about 0.8 s; process peak about 1.3 GB | |
| BiRefNet_lite run, warm | about 4.8 s idle (barely scales with threads); 5.9 s loaded | 5.6 s loaded |
| BiRefNet_lite session load | about 2 to 3 s idle | same as fp32 |
| BiRefNet_lite peak memory (2316x3088 photo, 24 threads) | | about 6.2 GB process working set; failed with "bad allocation" when 6.3 GB was free (October 2026) |

Published numbers for a 6-core Ryzen 5600X: MobileSAM encode 0.8 s, BiRefNet_lite 7 s (ONNX Runtime). About 60% of BiRefNet's time is Transpose/Sum/Mul/GatherND, the export's spelling of deformable convolution and Swin window shuffles.

## Platform packaging

- Windows: `onnxruntime.dll` and `onnxruntime_providers_shared.dll` beside `patchy.exe`, `ai-models\` beside them; the installer and zip pick up the build folder contents.
- macOS: the dylib goes in `Contents/Frameworks` and `ai-models` in `Contents/Resources`, both staged by the bundle step and signed with the app.
- Linux/Flatpak: an `onnxruntime` archive module installs into `/app`; models install to `share/patchy/ai-models`.
