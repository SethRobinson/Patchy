# Home Screen recent files grid: patent research record

Engineering research record for the Home Screen's Recent Files grid view with thumbnails (GitHub issue 750; design in [ui-conventions-menus.md](ui-conventions-menus.md#recent-file-thumbnails)), November 26, 2026. The binding summary lives in [legal-constraints-interface.md](legal-constraints-interface.md#home-screen-recent-files); read that first. This is an engineering record of the design and of the published patents considered.

## Design compared

The Home Screen lists the files this copy of Patchy opened or saved, newest first, as rows or (a stored choice, off by default) as equal cells with a small picture over the file name. A picture is the document's own composite, kept when Patchy opens or saves the file, or a cheap read of the file made on a worker (a PSD's embedded thumbnail, or an image Qt's readers decode). Each picture is one PNG of one size in a per-user cache folder, named from the path's text and stamped with the file's size and modified time. The name filter, the click, the context menu and the remove button are those of the list.

## Sweep

- USPTO Patent Public Search, full text of issued US patents ([patent-research.md](patent-research.md#reading-patent-text)), 2026-11-26. Title queries: recent or recently with thumbnail, preview, grid or gallery (no hit); recent or recently with document or file and opened, used or accessed (6 hits); thumbnail with assignee Adobe (23 hits); home, start, welcome or launch screen, page or workspace with document, file, thumbnail or recent (3 hits, none on this subject). Full-text queries: "recent files", "recent documents" or "recently opened" with thumbnails and a start, home, welcome or launch screen (28 hits, all read by title, the closest read by claim); the same with assignee Adobe (no hit); "recent files" or "recent documents" or "recently opened" with thumbnail and assignee Adobe (3 hits: US 12373988, US 10878040, US 10055392, on tracking image modifications, file synchronization and archive management); the same-paragraph form with a list view or grid view toggle (no hit).
- Web searches for an Adobe patent on a start or home screen showing recent files as thumbnails returned product help pages and forum threads only.
- Earlier publications of the feature itself: Microsoft US 7454706 B1 ("Multiple-page shell user interface", filed 2000-02-11) was the one full-text match for recent documents, thumbnails, a start page and both a list view and a thumbnail view (matched by its text; its claims were not read); LibreOffice 4.2's Start Center (January 2014) shows recent documents as a grid of thumbnails; Photoshop CC 2015.1 (November 2015) added a start screen whose recent files switch between a list and thumbnails; AutoCAD LT's start tab offers a Grid View and a List View of recent documents.

## Findings

Claims read from the USPTO service on 2026-11-26. End dates are the filing date plus 20 years plus the term adjustment the service reports, before any terminal disclaimer; fee status was not read.

- **Apple US 10572103 B2, "Timeline view of recently opened documents"** (filed 2014-09-30, adjustment 488 days, to about 2036-01-31). Independent claims 1, 15 and 27 require all of: a display of files previously opened on the device from a plurality of applications; the files arranged by time in a plurality of functional groups; a first and a second group each selectable to open the several files of that group so that they are open at the same time. Patchy lists only the files Patchy itself opened or saved, as one flat list in most-recent order, and a click opens one file. It has no groups and no command that opens several recent files together.
- **Adobe US 9672225 B2, "Management of thumbnail data associated with digital assets"** (filed 2010-07-06, adjustment 1199 days, to about 2033-10-17). Independent claims 1, 14 and 15 require multiple sets of thumbnail data for one asset at different resolutions, stored in one thumbnail file that is a single unit of storage partitioned into preallocated regions of different sizes, with common offsets across the thumbnail files of different assets, read by offset for the resolution of interest. Patchy stores one picture per file, in one size, as an ordinary PNG of its own; a smaller size for the screen is scaled from it in memory.
- **Adobe US 8046717 B1, "Method to automatically determine the thumbnail of a document"** (filed 2007-03-08, adjustment 609 days, to about 2028-11-06). The independent claims analyze the pages of a multi-page document for rich content, score each page and designate the highest-scoring page or pages as the thumbnail. Patchy's picture is the composite of the document as opened, and for a file read on the worker the image the file holds or embeds; nothing scores or picks pages.
- **Adobe US 10878024 B2, "Dynamic thumbnails"** (filed 2017-04-20, adjustment 751 days, to about 2039-05-11). The independent claims generate image tags and a rectangle per tag by analyzing the image, create several thumbnails from combinations of those rectangles, and select the one matching a search query. Patchy keeps one whole-image picture per file; the name filter only hides cells and never changes a picture.
- **Adobe US 9679050 B2, "Method and apparatus for generating thumbnails"** (filed 2014-04-30): claims weigh the assets of a document, search a second source for related assets and compose a thumbnail from both. Nothing of the kind is in the design.
- Read by title and found unrelated to the design: Oracle US 9710443 (recent lists shared between two installations of an editor), Microsoft US 9501490 and US 9455939 (most recently used lists synchronized across devices, and for attaching files), Samsung US 10078421 and US 10915225 (a rotating touch menu), the Apple placeshifting and multiple-display families.
- No claim was found on: an application's own recent files shown as a grid of thumbnails, a control that switches that list between rows and cells, a per-user cache of one picture per recent file, or deleting a picture with its recent entry.

## Pictures for every kind (follow-up, 2026-11-26)

The worker now makes a picture for every kind of file Patchy opens: the preview a file embeds (OpenRaster, Krita, Paint.NET, camera raw), the file through Patchy's own reader, an SVG drawn whole, and page 1 of a PDF. Checked against the findings above, with no new search:

- **Page 1 of a PDF, and the first frame of an animation.** US 8046717's independent claims need the pages analyzed for rich content, a score per page and the highest-scoring page designated. Taking page 1 because it is first analyzes nothing and scores nothing; the page a viewer opens on is the oldest convention there is for a document's picture. The same holds for the first frame of an animated GIF, AVIF or JPEG XL (what Patchy shows when it opens the file). The limit is now written out: by position alone.
- **One picture, one size, one file** is unchanged (US 9672225): the "no picture" marker beside the cache files is a few lines of text, not a second set of thumbnail data, and nothing is stored in regions of one file.
- **Nothing is composed or chosen from content** (US 10878024, US 9679050): every source is the whole image, the whole document, or the whole first page.
- **Files Patchy has not opened** stay out: the worker only looks at entries of Patchy's own recent list.

## Limits recorded

Kept in [legal-constraints-interface.md](legal-constraints-interface.md#home-screen-recent-files): no time groups and no command that opens several recent entries together, and no files gathered from other applications (US 10572103); one picture of one size per file, each in a file of its own (US 9672225); a picture is the whole document or the whole image, never chosen, cropped or composed by analyzing content or by the filter text (US 8046717, US 10878024, US 9679050).

## Watch items

- The service answers title and full-text queries only; claim-text and class queries returned nothing ([patent-research.md](patent-research.md#reading-patent-text)), and Google Patents' result pages were not read. Repeat the sweep with class G06F 3/0482 and G06F 16/16 result pages before the grid gains grouping, sorting by anything but recency, pinned entries, or pictures of files Patchy has not opened.
- Published applications (US-PGPUB) were not searched.
