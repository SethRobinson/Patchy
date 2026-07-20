/* Implementation TU for the vendored stb_image (v2.30, public domain / MIT).
 * Decode-only and deliberately restricted to the two formats Affinity embeds
 * as placed-image originals that Patchy decodes in Qt-free core code (JPEG,
 * PNG); every other stb format stays compiled out. zlib for PNG comes from
 * stb's own inflate so this TU stays self-contained.
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
/* Matches the importer-side dimension cap (kMaxLayerSide / the PSB limit). */
#define STBI_MAX_DIMENSIONS 300000
#include "stb_image.h"
