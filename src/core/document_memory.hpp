#pragma once

#include <cstddef>
#include <functional>
#include <unordered_set>

namespace patchy {

class Document;
class PixelBuffer;

// Identity set for copy-on-write pixel storage: the address of the first byte
// of the shared byte vector (PixelBuffer::data().data()). Buffers sharing
// storage through copy-on-write share one pointer, so a set of these pointers
// deduplicates storage across documents and history snapshots. Empty buffers
// are never inserted (they alias one shared sentinel vector).
using PixelStorageSet = std::unordered_set<const void*>;

// Invokes `visit` for every pixel-bearing buffer reachable from the document:
// layers (recursive through group children), layer masks, smart filter stack
// masks, alpha/spot channels, and metadata().psd_flat_composite. Const access
// only; never bumps a revision. Deliberately excluded: smart object store
// payloads, smart filter effect caches, pattern tiles, vector shapes/masks,
// and raw PSD blocks - all shared_ptr-held and shared across undo snapshots
// by design, so their marginal per-snapshot cost is approximately zero.
void visit_pixel_buffers(const Document& document,
                         const std::function<void(const PixelBuffer&)>& visit);

// Inserts the storage identity of every non-empty reachable buffer into `out`.
void collect_pixel_storage(const Document& document, PixelStorageSet& out);

// Sum of byte_size() over reachable buffers whose storage is in neither
// `exclude` nor `seen`. Each newly counted storage is added to `seen`, so a
// buffer shared by several documents counts once across repeated calls.
[[nodiscard]] std::size_t accumulate_unique_pixel_bytes(const Document& document,
                                                        const PixelStorageSet& exclude,
                                                        PixelStorageSet& seen);

}  // namespace patchy
