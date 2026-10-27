# Patent research: perspective planes and perspective warp

Research record for two features Patchy does not have yet: perspective planes with clone, paste and paint in perspective (Photoshop's Vanishing Point filter), and a quad-based perspective warp (Photoshop's Perspective Warp), requested in #457. The binding summary is the "Perspective editing" section of [legal-constraints.md](legal-constraints.md); this file is an engineering record and follows the conventions of [patent-research.md](patent-research.md). Read October 27, 2026 from Google Patents claim pages. Status and expiry dates are Google's computed values; re-check them before extending either feature. Perspective planes were built on October 27, 2026 within the rule below ([perspective-planes.md](perspective-planes.md)); the warp is not built.

## Perspective planes (clone, paste and paint in perspective)

- Adobe US 7425958 (Berger, Troppoli, "Perspective editing tools for 2-D images", priority 2004-10-25/26): Expired - Lifetime. Its continuation US 7808502 is expired (Google gives 2024-10-26).
- Adobe US 7525555 ("Facilitating image-editing operations across multiple perspective planes", a continuation-in-part, priority 2004-10-26): expired, Google gives 2025-06-02. Independent claims 1, 4, 10, 13, 19 and 22 orient a source or destination area to a perspective plane, clip it at the plane boundary, continue it onto one or more adjacent planes, and apply the edit per partial area. Its continuation US 7999829 expired 2024-11-05.
- European counterpart EP 2038848 B1 (from PCT WO2007143371, filed 2007; granted 2019-07-24): listed as active. Twenty years from the PCT filing puts its end at about May 2027; the exact date and the per-country validation and fee status are unconfirmed. As the counterpart of US 7525555 it covers the same idea: an edit area clipped at one plane's boundary and continued onto an adjacent plane.
- Published earlier work: GIMP's Perspective Clone tool (GIMP 2.4, 2007) clones through a user-set perspective, and the expired Adobe family above is itself prior art for single-plane perspective editing.
- Design rule while EP 2038848 may be active: plane definition by user-placed corners, a homography per plane, and clone, paste and paint mapped through one plane are clear. An edit that reaches a plane's edge stops there: never clip it at the boundary and continue the remainder onto an adjacent plane. Re-check the EP status before lifting that rule.

## Perspective warp

- Adobe US 9117253 B2 (Intwala, Agarwala, "Perspective warp", filed 2012-07-20): Active, Google gives 2033-06-17; maintenance fees paid in 2019 and 2023.
- Independent claims 1, 15, 19 and 22 each need all of: a portion of the image, or a user-defined quadrilateral, to warp; a sub-portion that the user marks through the interface to stay unchanged (constraint information); and a real-time preview in which that sub-portion stays unchanged. Claims 1 and 15 also need a visual indicator for the marked sub-portion.
- Dependent claims: point, line and quad constraints (3, 11 to 13), as-rigid-as-possible deformation outside the quads (7), automatic detection that helps pick the portion (10), and default automatic manipulation (6).
- Patchy's form: quads drawn by the user, shared edges between quads, and corner drags, warped by a homography per quad. No edge, line or point the user marks to stay fixed, no pins, and no marker for a fixed part. Each feature beyond that needs a new claim read.

## Automatic straightening

- Adobe "Automatic adjustment of images using a homography", US 9008460 and its continuation US 9582855 (priority 2012-04-27; US 9582855 active to 2033-06-25), and "Camera calibration and automatic adjustment of images", US 9519954 (active to 2033-04-26). They cover automatic line or vanishing-point detection followed by an optimized homography (Lightroom and Camera Raw Upright; the CVPR 2012 paper by Lee, Shechtman, Wang and Lee).
- Their claim texts were not reviewed in detail. Any automatic straighten (detected lines, auto-level, auto-vertical) needs a claim-level review first.

## Names

- "Vanishing Point" and "Perspective Warp" are Adobe menu names. No trademark register record was found either way. Use descriptive labels (for example "Perspective Planes", "Perspective Quads") until someone checks Adobe's published trademark list.
