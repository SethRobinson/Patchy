# Artboards: patent research record

Engineering research record for the artboard feature ([artboards.md](artboards.md)), October 18, 2026. The binding summary lives in [legal-constraints.md](legal-constraints.md) ("Artboards"); read that first. This is an engineering record of the design and of the published patents considered.

## Sweep

- Google Patents full text, exact phrase "artboard" / "artboards" with assignee Adobe (24 results), the phrase with nest/nesting/nested terms (no design-tool hit), and reparent/drag/child phrasing across assignees (no hit). Web searches for Adobe and Figma claims on auto-nesting and canvas growth found only product documentation and forum threads.
- Prior art for the shipped behaviors is broad and old: Photoshop CC 2015 artboards (auto-nesting a layer dragged inside an artboard, canvas growth, artboard tool with add buttons), Illustrator multiple artboards (CS4, 2008), Sketch artboards (2010 onward), Figma frames with reparenting on drop.

## Findings

- **US 8667406 B1, "Artboard creation and preview" (Adobe; priority 2008-10-01; adjusted expiry 2031-02-04; active).** Independent claims 1, 14 and 26 share one core: display a first artboard on a canvas, DETERMINE WHICH ARTBOARD'S CONTENT WAS MOST RECENTLY EDITED and treat it as active, and when the user asks for a second artboard linked to the project, read the active artboard's dimensions and generate and place the new artboard with them. Design: Patchy never derives an artboard, its size, or its placement from edit history. A new artboard takes its size from the user's explicit input (the New Artboard dialog or the Artboard tool's W/H fields, remembered as settings) or, for an add button, from the artboard whose button the user clicked; the tool never tracks which artboard was edited last.
- **US 11250607 B1 and US 10540741 B2** (Adobe; angular snapping guides in digital artboards; active): Patchy artboards snap edges and centers only, through the existing alignment rules ([legal-constraints.md](legal-constraints.md) "Alignment guides"); no angular snapping.
- **US 2019/0130548 A1** (Adobe; propagating changes across similar shapes in multiple artboards): Patchy has no cross-artboard linked shapes or change propagation. Do not add one without a new review.
- Responsive or constraint-based resizing of artboard content (Adobe "Responsive Resize" AU 2019205973 B2, US 2018/0059919 A1 Responsive Design Controls): Patchy resizes only the artboard rectangle; content never reflows or scales with it. Do not add content constraints without a new review.
- No in-force claim was found on: auto-nesting a dropped layer into the artboard under it, growing the canvas to contain artboards, artboard background colors, clipping content to the artboard, artboard name labels, per-artboard export to files or PDF pages, or the PSD `artb`/`artd` structures (observed from a licensed Photoshop's output).
