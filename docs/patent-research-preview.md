# Patent research: prior-art element map for the composite-preview patents

Companion to item 1 of [patent-research-audit.md](patent-research-audit.md). Adobe US 9058699 B2
(claims 1, 11, 18; Active to 2029-02-02) and its parent US 8380005 B1 (claim 1; Active to
2031-12-23) claim a composite image holding an object taken from another image, previewed at a
lower resolution and then rendered at a higher one. This map lists the dated evidence that the
same thing was published or sold before the 2009-02-02 priority date, element by element, as
input to the decision whether Patchy's reduced-resolution Move and Free Transform previews stay.
Compiled 2026-10-20 from the public record as an engineering inventory of published earlier work.

## Evidence inventory

Every item is dated before 2009-02-02 and can be checked at the citation.

- **E1. WO 94/22101 A2**, "Method and system for image processing", FITS Imaging (inventor Bruno
  Delean), priority FR 93 03455 of 1993-03-25, published 1994-09-29. Same specification as
  US 5907640 A (filed 1994-03-25 as a continuation-in-part of 08/085,534 of 1993-06-30, granted
  1999-05-25, expired) and US 6181836 B1 (granted 2001-01-30, expired 2013; the "non-destructive
  editing" patent the main record already lists). Teaches: a pyramid of subimages at several
  resolutions; a screen view computed "appropriate to display resolution" for the current pan and
  zoom by selecting the subimage of appropriate resolution; "Additional images can be inserted at
  any time and, if desired, with transparency and masking"; an edit stored as an expression tree,
  each modification "a layer on top of the previous image"; an output that "can have any
  resolution", produced by "combining the expression tree with a full resolution image to produce
  a high resolution output image" for a high-resolution printer. Google Patents pages:
  [WO1994022101A2](https://patents.google.com/patent/WO1994022101A2/en),
  [US5907640A](https://patents.google.com/patent/US5907640A/en),
  [US6181836B1](https://patents.google.com/patent/US6181836B1/en).
- **E2. Photo Electronic Imaging, vol. 37 no. 4 (1994)**, E. Sapwater's interview with Bruno
  Delean (transcript at grantsymon.com/Share/BrunoDeleanPEI-Interview.txt; the printed issue is
  the citable copy): the Live Picture prototype was shown in Paris in February 1993 and at
  Seybold Boston in April 1993; "There is no fixed resolution until the image is output"; the
  program "builds or rasterizes data as a TIFF file for output"; the workspace lets the user
  "merge images of varying resolutions"; silhouetted objects moved to a new background get
  automatic edge correction ("Chrominance Compensation").
- **E3. MacUser, October 1995 (vol. 11 no. 10), pp. 57 to 59**, review "Live Picture 2.0"
  (HSC Software, $995; scan at vintageapple.org/macuser/pdf/MacUser_9510_October_1995.pdf,
  pages 23 and 24 of the file): establishes the product on sale and reviewed in 1995, with a
  price cut from $3,995 reported "several months ago". The review's statements about the
  screen view and output were not extracted for this map.
- **E4. Stanford GSB case SM12, "FITS Imaging Inc. (A)" (1994)**: FITS Imaging founded 1993 by
  Delean to sell Live Picture.
- **E5. Live Picture 1.x (1994, publisher HSC Software)** and 2.x binaries are preserved at
  Macintosh Repository and Macintosh Garden; the software itself can be run in an emulator to
  document behavior, but a product is prior art only with corroborated dates of sale, which E3
  and E4 give for 1994 and 1995.
- **E6. Macromedia xRes (Fauve Software 1995, Macromedia xRes 2.0 1996, 3.0 1997)**: "Speeding Up
  the Big Picture", review by Lynn Ginsburg, October 1996 (kaisernet.org/library/1996/1096/10r34.htm):
  xRes mode works on a manageable slice of the data so that editing runs in real time; the
  edits are applied to the full-resolution file at the end. A second product with the
  screen-proxy-then-full-render model, weaker on the "object from another image" element.
- **E7. Photoshop's image cache**: Macworld, "Optimizing Photoshop CS2", Shangara Singh,
  2006-02-12: "Photoshop can use low-resolution cached versions of the 100 percent view for
  speedier redraws." Dated before 2009 but it shows a display cache derived from the full
  composite, the reverse order of the claims; a Photoshop 4.0 or 5.0 manual quote (1996, 1998)
  was not verified for this map.
- **E8. What the examiners saw** (citation lists read on Google Patents, 2026-10-20): neither
  Adobe prosecution cited E1 or any Live Picture document. The closest cited art was
  US 5666475 (University of Washington, editing multiresolution wavelet images), US 7032179 /
  US 2002/0054115 (Peer Image, composite images from diverse sources on a display), US 5577179
  (Imageware image editing) and Microsoft's drag-and-drop pasting application.

## Element map

Claim 11 of US 9058699 is the broadest; claims 1 and 18 add one element each.

| Claim element | Evidence | Described in the earlier work? |
|---|---|---|
| 11(a) a composite image including an object extracted from one image as included in another image | E1 (additional images inserted with transparency and masking, each a layer); E2 (merge images, silhouetted objects moved to a new background) | Yes |
| 11(b) displaying a preview of the composite image in a first resolution | E1 (screen view computed at display resolution from the subimage pyramid, regenerated for each pan and zoom); E2 (no fixed resolution until output) | Yes on the resolution; "preview" is the live editing view before output, which is the ordinary meaning |
| 11(c) rendering, after displaying the preview, a refined version including the object in a second, higher resolution | E1 (output at any resolution by combining the expression tree with the full-resolution image; high-resolution print); E2 (build or rasterize a TIFF for output at the chosen size) | Yes |
| 1 adds: receiving an input to initiate the preview | E1 (the screen view is regenerated whenever a new view is needed, after each edit or pan) | Partly: any edit input initiates the next view |
| 1 adds: the refined render is "a result of a compositing operation" | E1 (the output RIP evaluates the expression tree with the inserted images) | Yes |
| 18 adds: displaying the refined version | Not documented: E1 and E2 send the output to a file or printer | Not found; would need evidence that Live Picture (or xRes) showed the built result on screen |
| US 8380005 claim 1: access the image on selection; display a copy superimposed over the other image; extract the object from that copy; merge for a preview; refine on a higher-resolution representation of the object's portion | E1 (inserting an image places it as a layer over the composition; masking applies to the inserted image; the pyramid holds its higher-resolution data); E2 (silhouetting inside the program) | Partly: the superimposed-copy-then-extract order is implied, not stated, in the sources read |
| Dependents 6, 7, 15, 16, 19, 20 (extraction by a drawn perimeter or by foreground and background classification) | E2 (silhouetting with automatic edge correction) | Partly; the manual would settle which tools existed |

## Summary

E1, one published document from 1994 that neither examiner cited, describes every element of
US 9058699 claims 1 and 11 in one system, and of claim 18 except its final display element; a
product on sale in 1994 and 1995 (E2 to E5) corroborates it. For claim 18's display element and
for the extra elements of US 8380005 claim 1, the sources read do not state them in so many words.

What would complete the record: (1) a copy of the Live Picture 1.x or 2.0 User Guide
(1994 or 1995) with page citations for inserting an image file as a layer, the mask and
silhouette tools applied to that layer, the screen-resolution view, and the Build command
writing a full-resolution file and showing it; (2) the full text of the MacUser October 1995
review and a 1994 review (MacWEEK, Publish, MacUser) saying the same; (3) column and line
citations into US 5907640 for each element; (4) for claim 18, any dated source showing the
rendered output displayed in the program; (5) a per-element chart in a standard format. The same
documents would cover
US 8380005 claim 1 if they describe masking an inserted image after placing it.

## Not verified

The MacUser review text beyond its header; the Live Picture user guides; whether Live Picture
displayed the built output; the dates and content of the Photoshop 4.0 and 5.0 manuals on the
image cache; the prosecution histories themselves (only the citation lists were read).
