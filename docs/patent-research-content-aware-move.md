# Patent research: Content-Aware Move and the Patch tool's drag preview

Claim-level record behind the Content-Aware Move bullet in [legal-constraints.md](legal-constraints.md) (GitHub issues 599 and 369; review of 2026-11-03, claim text read on Google Patents). Companion to [patent-research.md](patent-research.md). An engineering record of the design and of the published claims considered; act on the binding bullet, not on this record alone.

## Patents read

- **US 9575641 B2, Adobe, "Content aware image editing"** (Shechtman, Goldman; priority 2012-03-20; Active, expires 2033-10-23). Photoshop's Content-Aware Move. Every independent claim (1, 8, 13, and 18, 20) needs both (a) during the drag, the original location is updated (displayed) with the content of the image at the selection's current location, and (b) a patch-based optimization that fills the original location on release. The adaptation ("structure"/"color") setting appears only in dependent claims.
- **US 10332291 B2, Adobe** (continuation of the above; Active, expires 2033-04-02). Independent claims 1, 8 and 13 drop the patch-based optimization: select a region, drag it, during the drag show in the original location the content from where the selection currently is, and on release fill the original location from the new location and the area around it, by any fill method. Element (a) is still in every independent claim.
- **Patch-synthesis family** (US 8861868 to 2032, US 9317773 with Poisson voting, US 8285055 PatchMatch to 2031, US 9396530): already blocked by the "Content-aware fill and exemplar inpainting" bullet; see [patent-research-inpainting.md](patent-research-inpainting.md).
- **US 8050498** (live classify-and-display, to 2029-11-03): all processing runs once on release; nothing is healed or classified per move ([patent-research.md](patent-research.md), Spot Healing and Patch entry).
- No Adobe filing was found on Transform on Drop or on the Structure/Color settings as such.

## Design that stays outside

- During the drag the original spot stays exactly as it is; only a raw ghost of the moved content follows the pointer with the dashed outline (the drag-and-drop ghost is generic prior art, already used by the Patch tool's Destination mode). Without element (a), no independent claim of either patent is met.
- On release, once: the copy is healed in with the membrane of the expired US 6587592, and in Move mode the old spot is filled only with Remove Object's shape-derived rigid map (`core/spot_heal.cpp`; geometry only, no pixel content read, no patch or exemplar search), with the drop region excluded as a shape. This also keeps it outside element (b) of US 9575641.
- Left out pending their own checks: Structure and Color settings, Transform on Drop, any exemplar or patch-search fill of the old spot.

## Side finding: the Patch tool's Source preview

Patchy's Patch tool in Source mode used to show the dragged-to content inside the original spot during the drag and heal on release: the steps of US 10332291 claim 1. Photoshop's own Patch tool did the same years before 2012 (the tool dates from Photoshop 7, 2002, and Photoshop CS4 books describe the preview), which is strong prior art, but the preview was changed to the outline only on 2026-11-03 (Seth's choice) so the question does not arise. Destination mode keeps its ghost at the drop point, which shows nothing in the original spot.

## Sources

- https://patents.google.com/patent/US9575641B2/en
- https://patents.google.com/patent/US10332291B2/en
- https://patents.google.com/patent/US8861868B2/en
- https://patents.google.com/patent/US9317773B2/en
- https://www.oreilly.com/library/view/adobe-photoshop-cs4/9780321619457/9780321619457_ch11lev1sec1.html
