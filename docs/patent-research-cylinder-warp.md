# Patent research: cylinder warp

Research record for the Cylinder style of the Warp Transform tool (GitHub issue 719). The binding summary is the "Cylinder warp" section of [legal-constraints.md](legal-constraints.md#cylinder-warp); this file is an engineering record and follows the conventions of [patent-research.md](patent-research.md). Read November 13, 2026: the claims and description from the Google Patents page of the patent below. Status and expiry dates are Google's computed values; re-check them before changing the feature. The design is in [warp.md](warp.md#cylinder).

## The patent

- Adobe US 12307626 B2, "Warping artwork to perspective cylinders with bicubic patches" (application 18/057,374, filed 2022-11-21, published as US 2024/0169477 A1, granted 2025-05-20): Active, Google gives 2043-07-12.
- Independent claims 1 (method), 10 (stored instructions) and 19 (system) each need all of:
  1. a request to wrap an image onto a cylindrical surface;
  2. a set of adjacent warping patches that includes a first zero-width patch for the left edge of the image and a second zero-width patch for the right edge;
  3. a silhouette edge of the cylindrical surface that is determined;
  4. the image mapped to that set of patches, with the portions of the image the silhouette edge obscures mapped to the first or second zero-width patch.
- Dependent claims add: control handles on the set of patches (2, 11, 20); a handle for the top and bottom curvature and one for the bottom curvature (3, 12); handles for the top right and bottom left position (4, 13); a handle for the perspective distortion (5, 14); changing the patches by the difference between two handle positions (6, 15); the silhouette edge found from a viewing distance (7, 16); a top or bottom curve built from points sampled on a circular arc at angular steps, their foreshortened positions and a Bezier segment (8, 17), and both curves made from that segment by transforms (9, 18).
- The description: four Bezier patches side by side (two zero-width end patches and two middle patches), the silhouette found from the viewing distance, the content behind it kept on the end patches so nothing is destroyed, and a viewing distance that runs exponentially from 1 to 50 cylinder radii.

## What Photoshop 2026 does (measured)

Measured over COM on November 13, 2026 (shape layers warped with `warpStyle` `warpCylinder`, their anchors read back): the warp is four cubic patches across, the outer two zero width at the silhouettes, and the contents wrap half a cylinder seen from 1 + 50^g radii, so a strip of the contents on each side (a sixth at g = 0) lands on the zero-width patches. That is the arrangement the independent claims describe. Photoshop stores such a warp in the smart object's `SoLd` as the style and seven `warpValues`, with no mesh.

## Patchy's form

- The Cylinder style is one ordinary 4x4 Bezier patch from `generate_cylinder_wrap_warp_mesh`, the same kind as the fifteen bend styles: a fixed formula of the content size and the bend percent.
- Against element 2: there is one patch. No patch sits beside it and none has zero width.
- Against elements 3 and 4: no silhouette is computed and no viewing distance exists. The whole content stays in view between the two side edges (x rises across the whole patch), so no portion is obscured and none is mapped to an edge patch.
- Against the dependent claims: the style has no handles of its own (the regular warp cage and the Bend field), no perspective control, and its edge curve is a parabola written directly into the control points, with no arc sampling.
- A commit bakes the patch to a custom mesh, as every style does. Patchy never writes `warpCylinder` or `warpValues`.
- A smart object that Photoshop warped with its Cylinder keeps Photoshop's stored preview under the "warp" lock and its record is preserved byte for byte. Patchy does not render it again.

## Rule

Until 2043-07-12, without a new claim read: no warp or wrap of an image onto a cylinder that uses more than one patch with a zero-width patch at a side edge, that hides or collapses part of the image behind a computed silhouette, or that re-renders a Photoshop `warpCylinder` record; and no cylinder-specific curvature, position or perspective handles on such patches. A change to the Cylinder style that adds a second patch, a viewing distance, hidden content or handles of its own needs that read first.

## Published earlier work

Mapping an image onto a cylinder through a 3D projection is long published (texture mapping onto cylinders, Bier and Sloan 1986; GIMP's Map Object filter maps a layer onto a cylinder), and a single Bezier patch as an image warp is the form of the warp styles image editors have shipped since the early 2000s. Neither uses zero-width patches.

## Names

"Cylinder" is a descriptive word for the shape. Patchy's style id (`patchyCylinderWrap`) is its own and is never written to a file.
