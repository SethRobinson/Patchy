# Testy PSD compatibility run 20260807-153700

This is the image-free public export of the completed Testy run used for the
compatibility figures in Patchy's README. Source documents, rendered images,
difference heatmaps, and resaved PSD files are not included.

- Run date: August 7, 2026
- Corpus: 64 PSD files, 1,056,362,619 bytes
- Patchy build: `879a3a8`
- Photoshop reference: 27.8.0
- Comparison mode: perceptual
- Scan threshold: flag a file when any editor differs perceptually on more than
  10% of pixels or a test step fails
- Scan result: 45 flagged, 19 passed
- Source integrity check: passed

## Browse the results

- [Interactive HTML results table](results.html), with search, filters, sortable
  columns, and focused rendering or preservation views
- [Complete CSV data](results.csv), with one row for every file and editor

Both files exclude local source paths, source hashes, layer names, application
logs, and all image or document artifacts.

## Rendering and round trips

Perceptual and byte match are the mean of files that produced a render comparison.
Photoshop round-trip match compares Photoshop's render of an editor's resaved PSD
with the original Photoshop render.

| Editor and build | Opened | Renders scored | Perceptual match | Byte match | Photoshop round-trip match | PSD saves rejected by Photoshop |
|---|---:|---:|---:|---:|---:|---:|
| Photoshop 27.8.0 | 64 / 64 | 63 | 100.00% | 100.00% | 100.00% (n=63) | 0 |
| **Patchy `879a3a8`** | **64 / 64** | **63** | **98.83%** | **96.45%** | **99.98% (n=63)** | **0** |
| Photopea, web build | 63 / 64 | 62 | 97.13% | 95.35% | 99.76% (n=62) | 0 |
| Affinity 3.2.3.4646 | 61 / 64 | 60 | 88.30% | 87.28% | 88.02% (n=60) | 0 |
| GIMP 3.2.4 | 62 / 64 | 61 | 88.11% | 81.37% | 85.45% (n=61) | 0 |
| PhotoDemon 2026.01.0251 | 64 / 64 | 63 | 81.97% | 78.73% | 80.81% (n=63) | 0 |
| Krita 5.3.2.1 | 56 / 64 | 56 | 81.17% | 77.46% | 88.47% (n=47) | 9 |

## Native PSD data

The dashboard's native-data score is the mean of each inspectable file's retained
layer-object fraction. Aggregate counts have different denominators when an editor
could not open a source or Photoshop could not inspect its save.

| Editor | Dashboard data-kept score | Native layer objects | Editable text | Live effects | User masks | Vector masks |
|---|---:|---:|---:|---:|---:|---:|
| Photoshop | 100.00% | 1,788 / 1,788 | 312 / 312 | 412 / 412 | 133 / 133 | 406 / 406 |
| **Patchy** | **100.00%** | **1,788 / 1,788** | **312 / 312** | **412 / 412** | **122 / 133** | **406 / 406** |
| Photopea | 99.62% | 1,777 / 1,784 | 306 / 312 | 409 / 411 | 133 / 133 | 404 / 404 |
| Krita | 65.77% | 737 / 1,001 | 187 / 205 | 71 / 108 | 47 / 102 | 128 / 130 |
| Affinity | 59.98% | 1,039 / 1,752 | 0 / 305 | 230 / 403 | 95 / 122 | 318 / 396 |
| GIMP | 54.62% | 611 / 1,736 | 0 / 312 | 0 / 344 | 107 / 107 | 0 / 283 |
| PhotoDemon | 52.93% | 678 / 1,788 | 0 / 312 | 0 / 280 | 44 / 102 | 0 / 213 |

See the [benchmark description and methodology](../../../docs/psd-compatibility-benchmark.md)
for how Testy stages files, detects baked-composite use, scores renders, and checks
PSD editability.
