# Patent research record: Crop with the image moving behind the box (GitHub #699)

Scan of November 11, 2026. US claim text, filing dates and term adjustments were read from the
USPTO Patent Public Search full text; the Apple family's member list and fee payments from its
Google Patents pages; the Dutch member from the Netherlands patent register. The binding rules
are the "Crop interaction" section of [legal-constraints.md](legal-constraints.md#crop-interaction);
the tool is described in [crop-tool.md](crop-tool.md). This is an engineering record of a design
and of the published patents considered, in the conventions of
[patent-research.md](patent-research.md): a claim reading by an engineer, not a legal opinion.

A second scan of the same day, on straightening by a drawn line (GitHub #702), is the section
[Straighten by a drawn line](#straighten-by-a-drawn-line-github-702).

Question: may the Crop tool gain a mode where the crop box stays centered and upright in the view
while the image pans, zooms and turns behind it (the newer of Photoshop's two crop modes, with
its Auto Center Preview option), and within what limits?

## The mode that was compared

- The box stays where it is on screen. Dragging inside it pans the view, so the image slides
  under the box; dragging outside it turns the view about the box center, so the image turns
  under an upright box ([view-navigation-rotation.md](view-navigation-rotation.md)). Handle
  drags, Ratio, Size, the overlay list and the shield work as today.
- Zoom stays the ordinary view zoom: box and image scale together, the crop region is unchanged.
- After a handle drag the view pans so the box is centered again (the part of Auto Center
  Preview that was compared).
- The document is not touched until Enter. The commit is today's `crop_document` result.
- Mouse and keyboard only. P would switch between this mode and today's box-moves mode.

## The Apple family, priority July 12, 2011

"Multifunctional environment for image cropping", inventors Bhatt and Cherna, application
13/181,458, assigned to Apple Inc. on 2011-08-02. Six US patents and one Dutch patent. The PCT
application WO 2013/009828 ceased without a European phase (EPO notice of 2014-08-06) and no EP,
JP or CN member is listed. No US application later than US 11532072 appears in the family list.
All seven are in force on the scan date.

| Patent | Ends | Fees paid | What every independent claim requires |
|---|---|---|---|
| US 9158455 | 2033-09-21 (802 days of term adjustment) | 4th and 8th year; the 12th is due by 2027-10-13 | A crop region set by the user; a cropping panel showing that part of the image; a rotate request; a determination that the rotation would show an edge of the image inside the panel; in response, rotating and zooming the part so that no edge shows (claims 1, 15, 25; 25 adds a second panel with the whole image). |
| US 9767535 | 2031-07-12 | 4th and 8th year | A displayed image and a request to rotate it; the image is rotated by the requested amount and zoomed by an amount determined from that rotation amount, a different rotation giving a different zoom (1, 10, 19). No crop box is recited. |
| US 10546366 | 2031-07-12 | 4th year; the 8th is due by 2028-01-28 | A visual indication of the cropping region over the image; a request to change the region; the indication is resized for the new region, and then the indication and the image are both resized while the indication still marks the same region (1, 10, 19). |
| US 10762601 | 2031-07-12 | 4th year | An image transformed under a displayed grid: for a rotation the grid has one density, for a pan a second, different density (1, 9, 17). |
| US 11120525 | 2031-07-12 | 4th year | A request to crop based on a crop-box around part of the image; in response that part is emphasized and the image outside the box is shown under a translucent overlay (1, 8, 15). Dependent claims 4 to 6 add panning, rotating or zooming the image so that content crosses the box edge; 7 adds that the box keeps its size and orientation and the overlay stays while the image transforms. |
| US 11532072 | 2031-07-12 | 4th year | An image with a crop-box; interface elements for several predetermined aspect ratios; choosing one gives the box that ratio, and a resize request gives it an arbitrary ratio (1, 21, 41). Dependent claims 14 and 15 add a request to pan, zoom or rotate the image, by touch, keyboard or mouse, during which the box keeps its size and orientation with respect to the display; 16 a move of the box that keeps its size and orientation; 12 snapping to the preset ratios during a resize; 17 and 18 a resize of box and image after a change. |
| NL 2009172 | 2032-07-11 | 15th annual fee, 2026-06-15 | The 32 claims as filed in 2012; a Dutch patent is registered without examination. Claim 1: a crop region set by the user, a cropping panel showing that part of the image, and a user request for at least one of a zoom, a rotate or a shift of the crop region shown in the panel. Claim 16 is the system form (transform the part shown in the window, then crop it); 26 adds a whole-image panel. |

The claims are not limited to touch screens: the description and claim 15 of US 11532072 name
keyboard and mouse input.

## Element by element against the mode

- **US 9158455 and US 9767535.** The mode would have the box, the view that shows it and the
  rotate request. It would not have the edge determination or any zoom that follows from a
  rotation: a Patchy crop box may reach past the canvas (that is how Crop extends a canvas), so
  nothing tests for edges, and a rotation changes neither the zoom nor the box size. Rotate View
  and Image > Rotate Arbitrary do not zoom either (the latter recenters the view at the zoom it
  had).
- **US 10546366.** Photoshop's Auto Center Preview re-centers the box after a change and zooms
  the view to fit it. A re-centering that only pans resizes neither the box nor the image on
  screen, so the last element is absent as long as no zoom follows a box change by itself.
- **US 10762601.** Absent while one overlay, the user's choice from the Overlay list, is drawn
  for every gesture. Showing a finer grid for a rotation than for a pan is the claimed step.
- **US 11120525.** The crop tool Patchy ships already has every element of claim 1 as worded:
  drawing or adjusting the box dims the outside under the translucent crop shield. The mode
  would add the elements of claims 4 to 7. No design limit separates the mode from those claims
  short of an opaque or absent shield. What differs is the date: see E1 to E3 below.
- **US 11532072.** The crop tool Patchy ships already has every element of claim 1 as worded
  (the Ratio preset list, and handle drags that give any ratio), and of claim 16 (a box moved
  over the image keeps its size and orientation). The mode would add the elements of claims 14
  and 15. No design limit separates the mode from those claims. What differs is the date: E1.
  Claims 12, 17 and 18 stay absent (no ratio snapping, no automatic resize).
- **NL 2009172.** Claim 1 as worded covers a view that shows the crop region and lets the user
  zoom, turn or shift it; the mode would have each element. The register file holds the search report and written opinion drawn up for the
  Dutch office on 2012-10-26: novelty "no" for claims 1 to 3, 16, 17 and 19, inventive step "no"
  for claims 1 to 32, over US 5473740, US 6883140 and US 6281872. The patent was registered on
  2013-01-17 with claim 1 in the form above. It applies in the Netherlands only.

## Published earlier work

Each item is dated before July 12, 2011 and can be checked at the citation.

- **E1. Adobe Photoshop Lightroom 1.0 User Guide** (copyright 2007; the product shipped
  2007-02-19; the guide's PDF was archived by the Wayback Machine on 2007-02-23 from
  `livedocs.adobe.com/en_US/Lightroom/1.0/help.pdf`), page 70, "Crop and straighten a photo":
  "Lightroom crop and straighten controls work by first setting a crop boundary, and then moving
  and rotating the image in relation to the crop boundary." "Reposition the photo within the crop
  frame by dragging the photo." "Rotate the image by moving the pointer outside of a corner crop
  handle to display the rotate icon, and then drag to rotate the image." "Choose an aspect ratio
  from the pop-up menu next to the padlock ... Choose Enter Custom to specify an aspect ratio
  that is not listed." "As you adjust the crop overlay or move the image, Lightroom displays a
  grid of thirds ... As you rotate an image, a finer grid appears". Four years and five months
  before the priority date, this describes the planned mode (image moved and turned under the
  crop frame, by mouse), the ratio list with free handle drags, and the two grid densities.
- **E2. Photoshop Elements 1.0 help, "Cropping images"** (on the cover disc of Practical
  Internet, July 2001; `discmaster.textfiles.com`, file 16084, `Help/c06tr2.htm`): "select the
  Shield cropped area option to show the cropping shield. To adjust the color and opacity of the
  cropping shield, use the color selection box and the opacity pop-up slider"; "To rotate the
  marquee, position the pointer outside the bounding box ... and drag." The translucent shield
  and the rotatable crop box, ten years before the priority date and older than 20 years.
- **E3. US 5473740** (IBM, filed 1993-12-29, granted 1995-12-05, expired): while the crop
  boundary is dragged, the excluded part is continuously obscured; the description offers a
  plain background, patterns, black and white, or "various shades of a single color".
- **E4. US 7679625** (Apple, "Straightening digital images", filed 2005-01-07, granted
  2010-03-16, 488 days of term adjustment, so its term ended 2026-05-10): claim 1 has the
  picture's content turn relative to the display while the frame's outline does not, with the
  resulting crop indicated as it turns; claim 8 has a grid that stays still over the turning
  content. An expired patent's express teaching, and Apple's own.
- Photoshop CS6, which introduced the mode in Photoshop, came out in 2012, after the priority
  date, so it is not earlier work for this family.

## Other owners

| Patent | Ends | What its independent claims require | The mode |
|---|---|---|---|
| BlackBerry US 8671361, US 9201566 (priority 2012-05-24) | 2032-10-26; 2033-02-24 at the latest | A selection frame over the image is rotated by a gesture and resized so that it stays inside the image. | Absent: no resize follows a rotation. |
| Brother US 11699416 (US filing 2015-09-29) | 2035-10-12 | One control turns image and cropping frame together, a second turns the image under a frame that does not turn; the image outside the frame is shown during that turn and no longer shown once it ends. | Absent while the outside is drawn the same way during a gesture and after it. |
| ByteDance US 12008684 (PCT filing 2021-02-18) | 2041-02-18 | During a crop-box drag that reaches a set position of the display area the image in the box is resized, and after the drag it is shown at a second position; plus a resize speed from the drag, or image and box centers kept together during the drag. | Absent: a handle drag never changes the zoom. |
| Google US 10101891 (filed 2016-03-24) | 2036-12-10 | A resized crop window snaps to a predetermined aspect ratio, with a threshold that depends on the ratio the drag started from. | Absent: no ratio snapping. |
| Apple US 9569078, US 10552016, US 10942634 (priority 2012-03-06) | 2033-03-28; 2032-09-27 | A detected horizon line shown as a selectable item that levels the image; a partial-circle dial, or a separate item with a stationary marker, turned by touch to rotate the image. | Absent: no horizon detection, no dial. |

Read and unrelated: Google US 9279983 (head-mounted display), Facebook US 9177362 (motion sensor
data from capture), Adobe US 9805445 (saliency-scored zoomed croppings), Adobe US 11294556
(split-screen editing), Microsoft US 8161379 (picture placeholders), vivo US 12469101 (two
screens), Fujifilm US 8963868 and Samsung US 9524040 (touch traces), VistaPrint US 7675528 and
US 7961200 (product design containers), Corel US 7386190 (panorama crop rectangle). USPTO
full-text searches for Adobe as assignee (crop with rotate, straighten, pan or canvas in the
claims, filed from 2009) returned no patent on the Photoshop CS6 crop interaction; searches for
Corel, Serif, Skylum, Macphun, Pixelmator and Affinity returned no crop-interaction patent.

## Outcome (decided 2026-11-11)

Decided 2026-11-11: the mode is built within the limits of group 1. For group 2 the build rests
on the earlier publications E1 to E4, the same kind of decision as the one of 2026-10-20 on the
composite-preview patents ([patent-research-audit.md](patent-research-audit.md), item 1). Built
the same day as Move Image Under Crop Box, off by default
([crop-tool.md](crop-tool.md#move-image-under-crop-box)); the group 1 limits have tests, named in
the binding entry. The comparison leaves two groups:

1. Separated by design, so these bind any build and the crop tool as it is today:
   - A rotation never changes the zoom or the box size, and nothing tests whether a canvas edge
     shows inside the box (US 9158455, US 9767535, the BlackBerry pair).
   - No automatic zoom after or during a box change; a re-centering pans only (US 10546366,
     ByteDance US 12008684).
   - One overlay for every gesture, the user's choice (US 10762601).
   - The outside of the box is drawn the same way during a gesture and after it (Brother
     US 11699416).
   - No ratio snapping, no horizon detection, no rotation dial (the last table).
2. Not separated by design: claims 14 and 15 of US 11532072, claims 4 to 7 of US 11120525, and
   NL 2009172. The independent claims of the first two already describe the Ratio list and the
   crop shield Patchy ships, so declining the mode changes nothing about those. A build rests,
   for this group, on E1 to E4 being earlier than the family: the decision above, not a finding
   of this record.

Re-check dates: 2031-07-12 (the five US continuations end), 2032-07-11 (NL 2009172),
2033-09-21 (US 9158455). After the last one the limits in group 1 that come from this family
can be dropped; the other owners' dates are in the table.

## Straighten by a drawn line (GitHub #702)

Scan of November 11, 2026, the same day. Claim text from the USPTO Patent Public Search full
text; legal status, fee events and the Japanese and Chinese members from their Google Patents
pages.

Question: may the Crop tool gain a Straighten button? While it is on, the user drags a line
along something that should be level or upright; on release the pending box turns about its own
center so the line becomes level or upright, whichever is nearer, and nothing is applied until
Enter. The Ruler's Straighten Image ([pick-tool.md](pick-tool.md#ruler)), which ships, already
turns and crops the image by a drawn line in one step.

| Patent | Status | What its independent claims require | The gesture |
|---|---|---|---|
| Apple US 7679625 (filed 2005-01-07) | Term ended 2026-05-10 (E4 above) | The picture's content turns relative to the display as the user's input continues, while the frame does not, with the crop indicated as it turns. | Expired. |
| Apple US 9569078, US 10552016, US 10942634 (priority 2012-03-06) | In force, to 2033 | A detected horizon shown as a selectable item that levels the image; a partial-circle dial or a separate item with a stationary marker, turned by touch. | Absent: the line is the user's own drag; no detection, no dial. |
| Apple US 9158455, US 9767535 (priority 2011-07-12) | In force, to 2033-09-21 and 2031-07-12 | A rotation followed by a zoom (an edge test, or an amount set by the rotation). | Absent while the turn changes neither the zoom nor the box size and nothing tests for canvas edges. |
| Ancestry.com US 8249391 (filed 2007-08-24) | Lapsed 2024-08-21 (12th-year fee unpaid) | Two points of a skew line from the user, the skew angle's sine and cosine from them, and the image transformed, updated with every move of the input device. | Expired. |
| Sony US 9215374 (priority 2008-08-18) | Lapsed 2023-12-15 (8th-year fee unpaid) | A drag longer than a fixed non-zero distance; a line approximating it; a region bounded by two lines parallel to it and two orthogonal to it is cut out and rotated to correct the tilt. | Expired; CN 101656832 B is listed as expired for fees too. |
| Sony JP 4640470 B2 (filed 2008-08-18, granted 2011-03-02) | Listed as active; anticipated end 2028-08-18 | Claim 1 (machine translation): an image shown on a screen that takes operation input; detection of that input; a first approximate straight line derived from the input's trajectory as the reference for correcting the image's inclination; and correction of the displayed image's inclination from that line and a predetermined reference line. Claims 6, 7 and 8 are the method, program and camera forms; claim 9 derives the line from feature points of a subject the user points at. | Every element present: the line comes from the drag and the commit turns the image so the line matches level or upright. The Ruler's Straighten Image has every element too. No step can be left out without losing the feature. |

USPTO full-text claim searches (filed from 2006: straighten, skew, tilt or inclination with a
drawn, dragged or user-specified line, and with crop, rotate, angle, horizontal or vertical)
returned nothing else on image editing. Read and unrelated: ABBYY US 10430948 and US 10366469
(text images for OCR), PFU US 9848104 (scanned-document skew), Apple US 9507765 (rotated text in
a table cell, a citing patent of US 8249391).

Published earlier work, each dated before the JP member's 2008-08-18 filing:

- **E5. Lightroom 1.0 User Guide** (the E1 document, archived 2007-02-23), page 70, "Straighten
  an image": "Select the Straighten tool, and then drag in the photo along a line that you want
  to be horizontal or vertical." The same page sets "a crop boundary" first and then rotates
  "the image in relation to the crop boundary".
- **E6. Photoshop's measure line and Rotate Canvas > Arbitrary.** creativetechs.com, "Fast way
  to straighten a photo in Photoshop", 2007-02-17: "drag a line across part of your image that
  should be straight horizontal or vertical", "Then choose Image > Rotate Canvas > Arbitrary";
  the post says the feature "has been around since Photoshop 5.5" (1999). The Arbitrary dialog
  opens with the line's angle filled in.
- **E7. US 8249391's application**, published 2009-02-26 as US 2009/0052802: after the JP
  filing, so not earlier work for it; cited for the US record only.

Outcome (not decided on the scan date). Separated by design, and binding on any build: the turn
happens once, on release, never while the line is drawn (US 8249391's live transform, expired,
and US 7679625 both turn during the input); no zoom and no box resize follow the turn, and no
edge test (US 9158455, US 9767535); no detected line and no dial (Apple's 2012 family). Not
separated by design: claim 1 of JP 4640470 B2, which applies in Japan only and also describes
the Ruler's Straighten Image as it ships. A build rests, for that patent, on E5 and E6 being
earlier than its filing date, which is a judgment this record does not make. Re-check date
2028-08-18 (the JP member's anticipated end).

## Not verified

- Google Patents refused automated requests (HTTP 503) part way through. The Apple family's
  pages were read before that. For the other owners the end dates are computed from USPTO filing
  and term-adjustment data: terminal disclaimers, fee payments and assignments were not read.
- Lightroom 1.0's dimming of the area outside the crop frame is not in the guide's text; E1 is
  cited for the moving image, the ratio list and the grids only.
- Photoshop CS6's dates (public beta March 2012, release May 2012) are from general knowledge.
- The NL claims were read in Google's machine translation; the written opinion's reasoning pages
  were not transcribed.
- No search of Japanese, Chinese, Korean or European patents beyond this family's own members,
  and no search of pending applications by title after 2024.
- JP 4640470 B2: its claims were read in Google's machine translation, and its annual-fee
  receipts are listed through 2020-12-04 only; the Japanese register (J-PlatPat) was not read,
  so its current status is Google's. No search of Japanese patents beyond the Sony family.
- The fee lapses of US 8249391 and US 9215374 are from Google Patents' copy of the USPTO
  events (codes EXP and LAPS); a petition to revive was not looked for.
- The #702 tutorial (Artose, "Crop Tool Part- 2") was not watched; its chapter list puts
  "Straighten Option In Crop Tool" at 0:10.
- The #699 YouTube tutorial linked in the issue was not watched; Photoshop's behaviour is from its
  documentation and the reviews found.
