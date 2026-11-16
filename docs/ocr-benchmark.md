# Text recognition benchmark (November 2026)

Split from [ocr.md](ocr.md): what was measured before the downloadable engine was written, the settings those numbers fix for it, and what a second Japanese model would buy. The scripts are `scripts/dev/ocr-bench/` (Python, run with `-I`; models and pages stay out of the repo, under `build\ocr-bench` and `local-test-fixtures\ocr-bench`).

## What was run

- **Machine**: Intel Core i9-14900HX (24 cores, 32 threads), Windows 11 build 26300, ONNX Runtime 1.30.0 on the CPU.
- **Pages**: ten A4 pages at 300 ppi (2480 x 3508), typeset by Patchy's own text engine through a headless script (`make_pages.py` writes the script and the ground truth), so the expected text is exact. English (Times New Roman 44 px, Arial 33 px), Latin with diacritics (German, French, Spanish, Portuguese, Polish, Czech, Hungarian, Italian), Russian, Korean, Japanese horizontal (Yu Mincho 44 px, Yu Gothic 33 px) and Japanese vertical (Yu Mincho 44 and 60 px, Yu Gothic 33 px, with real vertical forms of punctuation and the long vowel mark). The Japanese is the opening of Natsume Soseki's "Wagahai wa Neko de Aru" (1905, public domain) plus sentences written for the test that carry long vowel marks, small kana, brackets, digits and Latin words.
- **Variants** of each page: `clean300` as typeset; `scan300`, a plain simulated scan (turned 0.6 degrees, Gaussian blur 0.9, noise, JPEG quality 60); `clean150` and `scan150`, the same at half size.
- **Score**: character error rate, the edit distance between the read text and the page's text over the page's length, all whitespace removed, lines taken in reading order (columns right to left on a vertical page).
- **Engines**: PP-OCR small through `ppocr_ref.py`, written the way the C++ engine is planned (below); the Windows engine through `doc.recognizeText` in a headless run, with the two recognizer languages this PC has (`en-US`, `ja`); the NDL OCR-Lite recognizer on the line boxes PP-OCR's detector found (its own layout model is never used: [legal-licensing.md](legal-licensing.md)).
- **Not covered**, because it needs real scans: furigana, multi-column magazine pages, manga, handwriting, curved or stained pages. Generated pages flatter every engine; the comparison between engines is what carries over.

Models as evaluated, all from `huggingface.co/PaddlePaddle` (each repository's metadata says `license: apache-2.0`): `PP-OCRv6_small_det_onnx` at commit `28fe5895`, `PP-OCRv6_small_rec_onnx` at `b8f84f0b`, `korean_PP-OCRv5_mobile_rec_onnx` at `5c6f574b`, `cyrillic_PP-OCRv5_mobile_rec_onnx` at `2cef8814`, `eslav_PP-OCRv5_mobile_rec_onnx` at `9a32171f`, `PP-LCNet_x1_0_textline_ori_onnx` at `7fdcf3cf`. The fourteen repositories of the planned download are 117.5 MB together. NDL: `github.com/ndl-lab/ndlocr-lite` at commit `a25d4564`, the three `parseq-ndl-*` files (117 MB; the one this benchmark ends up using is 42.6 MB).

## Character error rate

Percent, detector long side 1536. PP-OCR uses its main recognizer for English, Latin and Japanese, the Cyrillic recognizer for Russian and the Korean one for Korean.

| Page | Engine | clean300 | scan300 | clean150 | scan150 |
|---|---|---|---|---|---|
| English, Times 44 | PP-OCR | 0.18 | 0.14 | 0.14 | 0.14 |
| | Windows | 0.14 | 0.14 | 0.21 | 1.82 |
| English, Arial 33 | PP-OCR | 0.09 | 0.12 | 0.14 | 0.16 |
| | Windows | 0.00 | 0.02 | 0.77 | 17.11 |
| Latin with diacritics | PP-OCR | 1.94 | 1.49 | 1.49 | 3.03 |
| | Windows (`en-US`) | 22.82 | 22.94 | 22.78 | 23.30 |
| Russian | PP-OCR | 0.43 | 0.62 | 0.47 | 1.24 |
| Korean | PP-OCR | 0.54 | 0.54 | 0.54 | 0.61 |
| Japanese horizontal, Mincho 44 | PP-OCR | 0.27 | 1.00 | 0.73 | 0.93 |
| | NDL | 4.93 | 7.67 | 5.27 | 1.13 |
| | Windows | 5.27 | 9.33 | 4.00 | 3.93 |
| Japanese horizontal, Gothic 33 | PP-OCR | 0.35 | 0.19 | 0.81 | 0.78 |
| | NDL | 4.27 | 0.39 | 2.91 | 0.58 |
| | Windows | 4.77 | 9.50 | 3.18 | 19.63 |
| Japanese vertical, Mincho 44 | PP-OCR | 12.03 | 12.11 | 12.25 | 2.65 |
| | NDL | 0.50 | 0.57 | 0.43 | 0.50 |
| | Windows | 4.30 | 9.60 | 9.10 | 6.16 |
| Japanese vertical, Gothic 33 | PP-OCR | 12.68 | 7.88 | 12.88 | 3.64 |
| | NDL | 6.20 | 2.92 | 3.72 | 6.12 |
| | Windows | 4.40 | 6.04 | 5.24 | 16.60 |
| Japanese vertical, Mincho 60 | PP-OCR | 11.85 | 11.48 | 11.73 | 8.70 |
| | NDL | 4.67 | 0.38 | 0.63 | 0.50 |
| | Windows | 3.91 | 3.91 | 3.91 | 3.53 |

Folding width forms (NFKC, so full-width Latin counts as plain) moves these by 0.3 points at most.

## Speed and memory

Seconds for one 300 ppi A4 page (38 to 46 lines), PP-OCR, detector long side 1536, one line per recognizer run:

| Threads | English, Times 44 | Japanese, Gothic 33 |
|---|---|---|
| 1 | 4.7 | 7.9 |
| 2 | 2.5 | 4.3 |
| 4 | 1.4 | 2.4 |
| 8 | 1.1 | 1.8 |
| ONNX Runtime's default | 1.5 | 2.1 |

The recognizer is 80 to 90% of that; the detector takes 0.1 to 0.4 s. The script recognizers (Korean, Cyrillic) are about a third slower than the main one. The Windows engine reads the same pages in 0.1 to 0.5 s. NDL's recognizer on the same boxes takes 1.3 to 3.1 s a page.

Peak memory of the whole Python process: 0.8 GB with one line per run, 1.6 GB with batches of eight lines (no faster), 5.0 GB when the detector also ran at the page's full size (2496 x 3520).

## What the numbers fix for the engine

- **The detector runs with the image's long side capped at 1536** (sides rounded to multiples of 32), and the lines are cropped from the full-size image. At 960 it broke small vertical columns into pieces (50 boxes for 32 columns) and lost a line of 33 px English; 2496 found the same lines as 1536 for a third more time and three times the memory.
- **One line per recognizer run.** Batching eight lines doubled memory and saved no time.
- **The detector's own settings** come from its `inference.yml` (threshold 0.2, box threshold 0.45, unclip ratio 1.4) and gave every line on every page from 1536 up.
- **A tall crop is turned a quarter turn counterclockwise** (its top goes to the left). The other direction read nothing (96% error).
- **The text line orientation classifier is left out of the first build.** On these upright pages it changed no result and added 0.3 to 0.6 s a page; it exists for lines printed upside down, which nothing here tests. It can join later without changing anything else.
- **The main recognizer does not read Korean or Cyrillic** (99% and 95% error): the language pick has to choose the script recognizer, as planned. For Russian the Cyrillic recognizer (0.4 to 1.2%) did better than the East Slavic one (1.0 to 1.4%).
- **Four or more threads** make a page a matter of one to two seconds. The engine should use ONNX Runtime's threads up to a cap rather than one.

## Vertical Japanese

PP-OCR small reads the characters of a vertical column and drops what sits small in the cell. On the clean 300 ppi Mincho page it lost every comma and full stop (83 of 83), every long vowel mark (23 of 23), every bracket, exclamation and question mark (12 of 12) and 46 of 48 small kana, while losing 2 of 1174 other kana and kanji. The orientation classifier does not change that. At half size with scan noise the same model keeps the punctuation and loses 43% of the upright Latin letters instead, so the result depends on stroke weight at the recognizer's 48 px line height. This matches the two open PaddleOCR reports about vertical Japanese punctuation that the planning research found.

The NDL recognizer, given the same line boxes, reads that page at 0.4 to 0.6% in all four variants, with no comma, full stop, long vowel mark or small kana lost. It is worse than PP-OCR on horizontal Japanese (0.4 to 7.7% against 0.2 to 1.0%) and on very long columns (78 characters at 33 px: 2.9 to 6.2%). Two things about how it is run matter:

- NDL picks one of its three recognizers (about 30, 50 or 100 characters) from a character count its own layout model predicts. Without that model its code falls back to the 100-character recognizer, which is what the table shows. Choosing by the box's shape gave erratic results (0.4 to 16%), and starting from the smallest and escalating by output length, without the count, was far worse (22 to 71%).
- Its code splits a line it reads as 98 characters or more in two. A build that used it would split long columns the same way.

The Windows engine's Japanese recognizer is in between on vertical pages (3.5 to 9.6%, 16.6% on the small noisy Gothic page) and well behind PP-OCR on horizontal ones.

## Recommendation (Seth decides)

- **PP-OCR small as the one download: yes.** It is the best of the three on every horizontal page, including Japanese, it brings Korean, Cyrillic and accented Latin that the Windows engine only reads with a language pack per language, and it holds up on small noisy scans where the Windows engine falls apart (English at 150 ppi: 0.16% against 17%).
- **A second model for vertical Japanese is worth supporting, and NDL's recognizer is the candidate**: one 42.6 MB file, CC BY 4.0 with credit to the National Diet Library, used only for the lines the detector found vertical when the language pick is Japanese, with PP-OCR still reading the horizontal lines. It is a follow-up with its own steps first: the question to NDL about the recognizer weights alone ([legal-licensing.md](legal-licensing.md)), the long-column split, and a check on real scanned book pages, furigana above all, which these generated pages cannot stand in for.
- **Until then the engine should say so**: with Japanese picked, a result that contains vertical lines can carry a note that punctuation and small kana in vertical text are often missed.
- **manga-ocr was not measured.** The ONNX repository approved for download (`onnx-community/manga-ocr-base-ONNX`) has the two networks but not the vocabulary the decoder's output needs, which is in the original author's repository; and a fair test needs real manga pages. Its smallest usable pair is about 110 MB, the full-precision pair 440 MB, and it is the weakest of the three on provenance.

## Running it again

```
python -I scripts\dev\ocr-bench\fetch_ppocr.py build\ocr-bench\models\ppocr
python -I scripts\dev\ocr-bench\make_pages.py local-test-fixtures\ocr-bench
build\opt-dev\patchy.exe --headless --run-script local-test-fixtures\ocr-bench\make_pages.js --script-arg out=<absolute pages folder>
python -I scripts\dev\ocr-bench\bench_ppocr.py local-test-fixtures\ocr-bench build\ocr-bench\models\ppocr build\ocr-bench\ppocr.json --dump build\ocr-bench\dump
```

`make_variants.py` then writes the variant images and `bench_windows.js` for the Windows engine (`score_windows.py` scores its output), `fetch_ndl.py` and `bench_ndl.py` do the NDL run, `rescore.py` prints the table above from the saved outputs and `error_kinds.py` the lost characters by kind. The Python environment is `onnxruntime` 1.30, `numpy`, `opencv-python-headless` and `PyYAML`. Downloaded model files are data: keep them in their own folder and never run anything from it.
