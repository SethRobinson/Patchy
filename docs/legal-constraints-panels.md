# Binding legal constraints: panels

Split from [legal-constraints.md](legal-constraints.md), which indexes these entries and states how they are written: what a claim requires, what Patchy does, how they differ, and dated watch items. Read that file first.

## Layers panel filter

- **The filter menu is Patchy's own and filters stored layer properties only** (GitHub issue 671, decided 2026-11-08, [record](patent-research.md), [design](layer-panel-filter.md)). One button opens checkable criteria that combine (Layer Type, Properties, Row Color, Selected Only); never another editor's filter row, mode list, order, one-mode dropdown or row of kind toggles, even when an issue posts its screenshots. Criteria read what the layer already stores (kind, mask, style, blend mode, visibility, locks, link group, Smart Filter stack, color label, selection); never labels or classes a model assigns or updates for the filter (Adobe US 12056791, claim 12 builds on that, to 2042-10-15).

## Assets panel

- **An asset is one whole local file with no tie to its source or to its placed copies** (GitHub issue 751, searched and decided 2026-11-26 before the panel was built, [record](patent-research-assets.md), [design](assets-panel.md)). The panel is the object library palette page layout programs have shipped since 1990: thumbnails of saved objects, objects dragged in, a copy dragged out into any document. Keep these limits without a new claim read:
  - **One file per asset.** Each asset is one PSD holding all of its layers, written once when it is added; rename, move and sort change the index only. Never a file per layer with a location map or a manifest, and never an asset updated layer by layer (Adobe US 11635908 and US 12327036, to 2037-06-22; US 10970302, to 2038-03-03; US 11693880, to 2037-10-05).
  - **No tie.** An asset stores no reference to the document or layer it was made from, and a placed layer stores none to its asset. Never a "show source" command, an update of placed copies, a version comparison, or a notice that an asset or a placed copy is outdated (Adobe US 12020209, to 2037-07-04; US 10416855, to 2037-11-13, which also has a lower-resolution stand-in for a remote asset).
  - **Local only.** No cloud storage, sync, sharing, team libraries or per-user access data, and no script or behavior stored with an asset (Adobe US 10204318, to 2037-05-04; US 11386393, to 2037-10-11; US 10423712, to 2037-09-27).
  - **Only what the user picks.** Assets come from the layers the user selects, one per selected root layer. Never assets found by analysing a document, suggestions, classification, automatic tags or use tracking (Adobe US 11321582, to 2040-12-10; Apple US 11295087, to 2039-06-18; Microsoft US 10339183 and US 10740349, to 2036).
  - **The search box only filters.** It narrows the grid by asset name as the user types. Typed text never inserts an asset into a document, in the panel, on the canvas or through Search Commands (Canva US 12412002, to 2043-09-05).
  - **Its own design.** The layout, wording and menus are Patchy's, never another editor's panel, even when an issue posts a recording of one.
  Watch item: the search of 2026-11-26 used title and full-text queries on US documents, with no class sweep and no foreign families; repeat it with a class sweep before the panel gains anything on this list.
