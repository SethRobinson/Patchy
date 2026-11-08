# Patent research: Scan a Book to PDF

Engineering claim comparison for File > Import > Scan a Book to PDF ([book-scan.md](book-scan.md)), read on Google Patents on 2026-11-08 (status labels are Google's; claim text was read as paraphrase, so re-read the exact wording before quoting it). Binding boundary: "Book scanning" in [legal-constraints.md](legal-constraints.md#book-scanning).

Patchy's design as compared: still flatbed scans taken only when the user presses Scan; the book outline found by the Divide Scanned Photos detector (border-seeded background model, components, min-area rectangle; [patent-research.md](patent-research.md), scanned-photo division); the gutter as the darkest column of a smoothed brightness profile in the central 30% of the straightened spread; every page cut at one uniform size anchored at its fore-edge by rotation resampling only; global levels, gray conversion and one global B&W threshold; image-page PDF output.

## Gutter detection and page splitting

- **US 6188779 B1** (L&H Applications, now Kurzweil Educational Systems; priority 1998-12-30; Expired - Lifetime 2018-12-30): text regions found in a two-page image set the page boundary for the split. Patchy splits on a brightness profile, not on text regions.
- **US 8675969 B2** (Canon; priority 2010-03-01; Expired - Fee Related, lapse effective 2026-03-18): edge image, straight top and bottom segments picked by colinearity and distance rules, curved boundary parts, binding center line, page width, left and right boundaries. Patchy has no edge-segment selection and no curvature step. Watch: the lapse is recent; re-check the USPTO record for reinstatement at the next review.
- **US 9066038 B2** (Kyocera Document Solutions; priority 2012-12-25; Expired - Fee Related, lapse effective 2023-07-31): local density maxima counted into a distribution whose peak is the binding, tilt from peaks in partial ranges. Patchy takes a column mean minimum and the tilt from the outer rectangle.
- **US 7800795 B2** (Xerox; priority 2004-11-18; Expired - Lifetime 2026-02-20): two-page original on the platen, two opposite corners located from a platen reference, page size and position computed, crop. Every element of Patchy's spread step appears here, so this expired patent is prior art for it.
- **US 8306335 B2 / US 8457403 B2** (Seiko Epson; priorities 2011-03-30 and 2011-05-19; Expired - Fee Related, lapses effective 2024-12-31 and 2025-06-04): a block feature image of per-block minima and maxima, then a page map; the sibling adds column-wise background-drop tracking for shadow corners. Patchy has neither.
- **US 7508978 B1** (Google; priority 2004-09-13; Expired - Fee Related 2021-04-26): infrared projector, pattern mask and stereo camera build a 3D surface whose groove is the spine. Patchy has no projector, stereo camera or 3D data.
- **US 10140691 B2** (ABBYY; priority 2016-04-26; Active, to 2036-08-23): straight binding line, sets of near-vertical lines on each page, a vanishing point per page, a perspective quadrangle per page meeting at the binding, per-page correction from the quadrangle. Patchy has the binding line and two boxes but no line sets, vanishing points or quadrangles, and corrects by rotation only.
- **US 12705755 B2** (Adobe; priority 2023-05-31, granted 2026-08-11; Active, to 2044-02-09): a convolutional segmentation network outputs thick masks and offset tensors per page boundary, thinned and fit to a quadrangle per page. Patchy uses no neural network.
- **US 8730535 B2** (Canon; priority 2008-08-21; Active, to 2029-08-19): a bright area near the binding (reflection off the curved page) is found above a threshold and its luminance lowered, decided by the binding's orientation. Patchy changes tone only globally.

## Uniform page size, size from a cover

- **US 7454697 B2** (Eastman Kodak; priority 2003-03-12; Expired - Fee Related, lapse effective 2020-11-18): book scanned to PDF pages, each cropped to content, the largest as the standard, pages aligned to a shared feature, a warning when sizes differ. The closest match to Patchy's uniform size step; lapsed.
- **US 5987163 A** (Minolta; priority 1996-06-26; Expired - Lifetime 2017-06-25): the edge surface of an open book read on the table, edges on both sides found, one page width computed, the output range set from it. Resembles the page-size estimate from the cover scan.
- **US 2011/0110604 A1** (Hewlett-Packard): crop page content onto a blank page; abandoned in 2013, never granted. No active patent on setting the scan area from a measured cover was found.

## Scanner book modes

- **US 8270041 B2** (Brother; priority 2007-10-11; Active, to 2031-05-29): an open document read into two page images, the opening direction (left or right) decided, one page turned 180 degrees relative to the other by that decision, both recorded on one medium in one orientation. Patchy writes one page per PDF page and never turns one page of a spread against the other; reading direction only orders pages and is the user's setting.
- **US 8958101 B2** (Canon; priority 2012-06-11; Expired - Fee Related): preview of a split two-page read, a user-marked area on one page mirrored to the other.
- **US 9258448 B2** (PFU; priority 2013-02-28; Active, to 2034-04-26): an overhead area sensor captures repeated images, brightness-difference motion detects a page turn, and the turn starts a read. Patchy scans only on the user's button press.

## Page-curvature dewarping (excluded)

Active families claim the dewarping approaches, which is why Patchy has none: **US 11145037 B1** (VoyagerX, to 2040-12-31: camera pose and page-curl parameters fit iteratively against known markers), **US 8406476 B2** (Compulink Management Center, to 2028-12-30: text-line extraction, polynomial fits, vanishing point), **US 10798359 B2** (FUJIFILM Business Innovation, to 2033-01-18: mobile frames, disparity map, cylindrical page surface). US 12731231 ("Document dewarping") could not be read (404 on Google Patents, image-only USPTO PDF): open item.

## Trademark

No "Scan a Book" mark was found in aggregator results (a dead BOOKSCAN application for book scanners; Nielsen BOOKSCAN in Class 35 services). The command name describes what it does. A direct USPTO TSDR search is an open item before release notes name the feature.

## Design rules taken from this record

Recorded in legal-constraints.md: rotation-only extraction (no line sets, vanishing points, quadrangles or homographies for book pages, so the Divide Scanned Photos perspective mode is never reused here; ABBYY US 10140691); no neural or learned page segmentation (Adobe US 12705755); tone changes stay global, never local near the binding (Canon US 8730535); never classify the opening direction to turn one page against the other, never two pages on one PDF page (Brother US 8270041); the Scan button is the only trigger (PFU US 9258448); no curvature, 3D, camera-pose or text-line dewarping.
