#pragma once

#include "core/layer.hpp"

#include <atomic>
#include <cstdint>
#include <functional>

namespace patchy {

// Classic exemplar-based inpainting (Criminisi, Perez, Toyama 2003; Microsoft
// US 6987520 expired 2023-03-30; Efros-Leung 1999 prior art): the hole is
// filled from its boundary inward in a fixed priority order (confidence times
// isophote strength, integer math, lexicographic tie-breaks), and every target
// patch takes the best source patch found by an EXHAUSTIVE scan of a bounded
// window of the same image at the working resolution (integer L1 distance
// over the already-known pixels plus a fixed per-pixel penalty on the source
// offset, first-in-scan-order tie-break). Variation N > 0 (`attempt`) is
// Efros-Leung's own epsilon selection: the same exhaustive scan finds the
// best distance, and the copy comes from ONE of the candidates within a fixed
// slack of it, chosen by a splitmix64 draw seeded from (attempt, target cell).
// The near-best set is recomputed per target from that fresh full scan and
// discarded; it never persists, seeds another target, or narrows a later
// search. Each target's search is independent of every other patch's chosen
// offset: there is no offset field, no propagation from neighbouring patches,
// no perturbation of an offset, no stored or pruned candidate lists, no belief
// propagation, no coarse-to-fine map, and no patch rotation, scale, mirror, or
// color adaptation. Those are the claimed elements of Adobe's active
// US 8285055 / US 8340463 / US 8355592 (into 2031) and US 9396530 (to 2034),
// which this implementation deliberately does not practice. Read
// docs/legal-constraints.md ("Content-aware fill and exemplar inpainting")
// and docs/patent-research-inpainting.md before changing the search, the
// variation pick, or the fill order. Deterministic across toolchains: integers
// only, fixed scan orders, SIMD and scalar distance paths that produce the
// same sums, a parallel candidate scan whose reduction picks the smallest
// distance and then the smallest scan index, and a variation pick that counts
// the near-best candidates in scan order (strip counts sum in row order, so
// the k-th candidate is the same however the rows are split).
struct ExemplarInpaintOptions {
  std::int32_t patch_size{9};      // odd; the target and source patch side
  std::int32_t search_radius{96};  // half-size of the candidate window around a target
  // Added to a candidate's distance per pixel of Manhattan offset between the
  // source and target centres: a fixed geometric preference for nearby
  // sources that keeps a texture's phase continuous across a large hole.
  std::int32_t offset_penalty{2};
  // 0: every target copies the best candidate (byte-stable output). N > 0:
  // variation N, a deterministic near-best pick per target (see above); each
  // N is reproducible run to run and different N usually give different fills.
  std::int32_t attempt{0};
  bool single_threaded{false};     // skip the candidate-scan fan-out (PATCHY_RENDER_SINGLE_THREADED)
  // Polled once per copied patch; a set flag stops the fill (the image is
  // left unchanged and the result reports `cancelled`). Lets a host run the
  // fill on a worker thread and drop it when the settings change.
  const std::atomic<bool>* cancel{nullptr};
};

struct ExemplarInpaintResult {
  bool filled{false};        // false when some target patch had no usable source, or when cancelled
  bool cancelled{false};     // the cancel flag was seen
  std::int64_t patches{0};   // patches copied
};

// `image` is interleaved RGBA8, row-major, `width * 4` bytes per row, and is
// filled in place. `hole` is row-major 8-bit coverage over `bounds` (already
// clipped to the image): non-zero cells are filled. `progress(done, total)`
// runs once per copied patch with the pixel counts. Pixels outside the hole
// are never modified; when the result is not `filled`, the image is left
// unchanged.
[[nodiscard]] ExemplarInpaintResult exemplar_inpaint(
    std::uint8_t* image, std::int32_t width, std::int32_t height, const std::uint8_t* hole, Rect bounds,
    const ExemplarInpaintOptions& options, const std::function<void(std::int64_t, std::int64_t)>& progress = {});

// Tone match after the fill: the filled hole's low-pass band (a box blur of
// radius `blur_radius` over the HOLE cells of the fill only, normalized by
// their count) is replaced by the harmonic interpolation, across the hole, of
// that same low-pass read at the ring just outside it, leaving the fill's
// high-pass detail untouched. This is classic frequency separation plus the
// healing membrane of the expired US 6587592 (Dirichlet interpolation of
// boundary values, core/heal_membrane.hpp): no gradients are composited and
// no guidance field exists (US 9058699 stays untouched). It removes the
// brightness steps where fill fronts from differently lit edges meet. Nothing
// outside the hole is read, so content past the hole's edge cannot pull the
// fill toward itself (the issue #23 lift; see the implementation comment).
// `strength_percent` scales the per-pixel correction (100 = the full
// replacement, 0 = no change). `filled` is an RGBA8 image; only hole cells
// change.
void exemplar_match_tone(std::uint8_t* filled, std::int32_t width, std::int32_t height, const std::uint8_t* hole,
                         Rect bounds, std::int32_t blur_radius, std::int32_t strength_percent = 100);

}  // namespace patchy
