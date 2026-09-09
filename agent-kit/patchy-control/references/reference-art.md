# Drawing from a reference

Use this workflow when a user asks the assistant to interpret a photo or image
as editable artwork in Patchy. Patchy supplies drawing and inspection tools;
the assistant supplies the visual decisions. A downscaled photo alone is not
a deliberately drawn pixel-art interpretation.

## Establish the visual target

Inspect the supplied reference. Identify the few features that must survive at
the target size: silhouette, pose, expression, a distinctive accessory, and
major color relationships. For a 64x64 portrait this might be the cap lettering,
the direction of the eyes, the smile, and the raised arm. Simplify background
clutter that competes with those features. Say when the composition is cropped
or simplified.

An image attached to a conversation is not automatically a file Patchy can
open. If a local path is available, open it as a separate reference document
and retain its document ID. Otherwise work from the visible attachment without
claiming it was imported; ask for a path only if the operation needs its pixels.
Do not search unrelated personal folders for a matching photo.

For a palette based on labeled samples, preserve readable printed names exactly,
including languages and abbreviations. Distinguish estimated photo colors from
measured samples; leave unreadable names empty and report that uncertainty
instead of inventing labels or translations. Attach the colors and parallel
names with `doc.setPalette`, or load a named GPL with `doc.loadPalette`.
See [Named palettes and PSDs](workflow.md#named-palettes-and-psds) for embedding
them in the editable deliverable and saving a reusable palette file.

## Work in editable passes

1. Create the target-sized document and choose a compact palette. Use named
   layers for background, subject, accessories, and details that may change.
2. Block out the silhouette and large value/color regions. For pixel art use
   integer coordinates, opaque palette clusters, `fillRect`, or RGBA buffers
   with `setPixels`. Avoid antialiasing and uncontrolled brush softness.
3. Call `get_preview` at native size, then at an integer enlargement such as
   512x512 for a 64x64 image, using `nearestNeighbor: true`. Inspect the returned
   images against the source. Prioritize recognition and expression over
   matching every source pixel. Cropped previews help with eyes or lettering.
4. Save a checkpoint, then revise the relevant layers using their IDs. When
   patching a buffer, read `getPixels`, modify that copy, and send the complete
   block back: `setPixels` replaces the layer, so a small patch erases the rest.
5. Compare the new preview to the previous one and the reference. Use undo if
   the revision reduces clarity. Re-query IDs after undo/reopen. End when the
   important features read clearly at native size and the requested size,
   palette, and file requirements are satisfied.

Keep each request a meaningful, reversible batch. Do not send one tool call per
pixel or restart the connector between drafts. In visible mode let tool calls
return between passes so the user sees progress.

## Verify delivery

Save an editable PSD and a PNG at the requested dimensions. Also provide a
nearest-neighbor enlarged preview if useful, labeled with its display size.
Check `saveAs` results and verify the saved PSD by reopening it and comparing
its preview with the final image. If a palette is required, also compare its
colors and names after reopening. Follow the workflow's
[Photoshop compatibility requirement](workflow.md#save-and-deliver) for every
PSD. Return actual files and the MCP image; do not
describe an uninspected or unsaved drawing as finished. Keep user photos and
personal artwork out of source-control commits.
