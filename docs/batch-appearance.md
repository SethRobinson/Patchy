# Editing appearance across selected layers

The shape toolbar, Shape Appearance, and Layer Style edit eligible selected
layers. Controls show the active eligible layer's values, falling back to the
first eligible selected layer. A Mixed label or marker identifies differing
values. Entering the displayed value or choosing the same swatch is an edit;
opening a dialog, visiting a page, or focusing a field is not.

Ordinary edits change individual properties. Changing stroke width preserves
each shape's paint, alignment, and enablement. Editing an effect parameter
preserves its other parameters and the layer's other effects. Dialog headers
show the selection, reference, and eligible counts. Effect rows show presence
counts. Locks and preserved vector data restrict eligibility. Layer Style
targets a selected group itself; selecting a group does not select its children.

## Shape appearance and radii

Fill, stroke, stroke parameters, layer opacity, Fill opacity, feather, and
density support batch editing. Solid Color changes paint kind and color.
Choosing a gradient or pattern changes its definition, preserving placement
on targets already using that paint kind. Type-specific parameters affect
matching paints. Paint edits also apply separately to a compound layer's
painted parts, preserving their other paint properties.
When paint types differ, the Fill and Stroke sections show how many layers
can receive gradient or pattern parameters.

Radius is the only geometry property that broadcasts. The Rectangle toolbar
sets all four corners and retains the next-shape default. With no eligible
rectangle it changes only that default. Shape Appearance has independently
editable TL, TR, BR, and BL radii. Linking the corners changes no values until
an edit; a linked edit sets all four corners. The link starts on only when
every eligible rectangle has four equal corners internally.

Eligibility requires one modeled Rectangle or RoundedRectangle origination
covering the layer's path, with no compound paint/geometry or preserved custom
origination. Ellipses and arbitrary paths are skipped. Mixed selections retain
the radius section and identify its reference rectangle. Position, dimensions,
line endpoints, and other geometry controls remain single-layer operations.

Each target supplies its own live parameters. Radius edits preserve bounds,
transform, group identity, combine operation, and appearance. Positive values
promote sharp rectangles; zero makes sharp corners. The existing generator
fits oversized radii independently to each target's dimensions without resizing
the shape. Reset changes appearance only, preserving geometry and radii.

## Layer Style

Effects match by type and occurrence. Editing the first Drop Shadow changes
the first shadow on each target and preserves later shadows. Parameter edits
affect existing matching instances without creating or enabling them. Explicit
enablement creates a missing instance from the displayed template; existing
instances keep their other settings, and preceding missing slots stay disabled.
Duplicate and remove operations replay in order against each target's stack.

Presets intentionally replace the effect stacks. Blending values are replaced
only when carried by the preset; No Style removes effects while preserving
blending settings. Unsupported native Blend If and channel restrictions stay
protected. The existing explicit Blend If replacement action remains required.
Effect changes normalize preserved unsupported Satin contours, with affected
layers identified in the dialog. Opacity-only edits preserve native effects.

## Apply All, previews, and history

Both dialogs offer Apply All Settings to Selected Layers as a one-time action
inside the current transaction. Shape Appearance copies appearance and the
displayed radii to compatible rectangles, preserving all other geometry.
Layer Style copies blending settings and the complete reference effect stack,
replacing additional target effects. The native-data safeguards still apply.

Preview off restores original target states while retaining pending edits.
Cancel restores layers and the document pattern store. Accept records one undo
entry for an effective change; no-op acceptance records none. Toolbar gestures
also record one entry and finish against captured target IDs before selection,
document, or history transitions. Dialog targets remain fixed.

All transient appearance color pickers preview across the same targets.
Canceling a nested picker restores its opening edit checkpoint, retaining
earlier outer-dialog edits. Gradient ramp changes preserve each target's angle,
scale, reverse, and placement. Swatches, numeric entry, paste, and drops use
the same color-selection notification, including an explicit unchanged color.
Foreground colors, ordinary Palette clicks and palette-entry editing retain
their existing routing; Palette forwarding into an open picker still works.

## Implementation boundaries

`src/ui/appearance_properties.*` supplies typed property accessors, equality, and
edit capture. `AppearanceEdits` (`src/ui/appearance_edits.*`) holds ordered
value-owning operations; only consecutive writes to the same property coalesce.
The dialogs are `shape_appearance_dialog.*` and `layer_style_dialog.*`.
Structural operations retain their position relative to parameter edits. Dialog synchronization never
captures user intent. Single-layer dialog callers remain supported.

Previews start from each target's baseline. Shape raster batches use the
existing background worker and generation checks, publish completed results
together, reject superseded work, and reuse completed rasters on acceptance.
Pattern adoption preserves collision aliases and placement references while
restoring original resources on cancellation. Native style/vector sources are
invalidated only for corresponding modeled changes. No scripting identifiers
or file formats change.

Focused regression coverage lives in `tests/ui/batch_appearance_tests.cpp`,
alongside existing shape, style, color picker, palette, and Layers-panel tests.
