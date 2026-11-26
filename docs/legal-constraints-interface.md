# Binding interface constraints

Split from [legal-constraints.md](legal-constraints.md): the binding entries for canvas context menus, the Layers panel filter and the Home Screen's recent files; the parent indexes each entry and links every claim-level record. Design docs: [canvas-context-menu.md](canvas-context-menu.md), [layer-panel-filter.md](layer-panel-filter.md), [ui-conventions-menus.md](ui-conventions-menus.md)

## Canvas context menus

- **Context menus use only Patchy's own commands** (GitHub issue 323): never another editor's menu set, order or grouping, even when an issue posts its screenshots ([canvas-context-menu.md](canvas-context-menu.md)).

## Layers panel filter

- **The filter menu is Patchy's own and filters stored layer properties only** (GitHub issue 671, decided 2026-11-08, [record](patent-research.md), [design](layer-panel-filter.md)). One button opens checkable criteria that combine (Layer Type, Properties, Row Color, Selected Only); never another editor's filter row, mode list, order, one-mode dropdown or row of kind toggles, even when an issue posts its screenshots. Criteria read what the layer already stores (kind, mask, style, blend mode, visibility, locks, link group, Smart Filter stack, color label, selection); never labels or classes a model assigns or updates for the filter (Adobe US 12056791, claim 12 builds on that, to 2042-10-15).

## Home Screen recent files

- **The grid shows Patchy's own recent list, with one cached picture per file** (GitHub issue 750, compared and decided 2026-11-26, [record](patent-research-home-screen.md), [design](ui-conventions-menus.md#recent-file-thumbnails)). The Home Screen lists the files this copy of Patchy opened or saved, newest first, as rows or as cells; the view buttons are the List and Grid pair Patchy's other pickers have, and the layout is Patchy's own, never another editor's start screen even when an issue posts its screenshots. Keep these limits: the entries stay one flat list in most-recent order, with no time groups, no command that opens several entries together and no files gathered from other applications (Apple US 10572103, to about 2036-01-31); a file has one picture of one size in a cache file of its own, never several sizes in one file with fixed regions (Adobe US 9672225, to about 2033-10-17); a picture is the whole document as Patchy opened or saved it, or the whole image the file holds or embeds, never chosen, cropped or composed by analyzing content or by the filter text (Adobe US 8046717, US 10878024, US 9679050). Grouping, sorting by anything but recency, pinned entries, and pictures of files Patchy has not opened need a new check first.
