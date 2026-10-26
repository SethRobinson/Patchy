#ifndef PATCHY_JPEG_DEEP_GLUE_H
#define PATCHY_JPEG_DEEP_GLUE_H

/* The C side of jpeg_deep_reader.cpp: the only file that calls the vendored libjpeg-turbo.
 * It is C so libjpeg's setjmp/longjmp error exit never crosses a C++ frame. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PatchyJpegDeepHeader {
  unsigned int width;
  unsigned int height;
  int components; /* 1 (gray) or 3 (RGB, or YCbCr when ycc is set) */
  int precision;  /* the frame's sample precision, 2 to 16 bits */
  int lossless;   /* SOF3/SOF11 */
  int ycc;        /* a lossless YCbCr image: libjpeg-turbo converts no color in lossless mode */
} PatchyJpegDeepHeader;

/* Called once, after the header: returns room for width * height * components samples,
 * or NULL when there is no memory. */
typedef unsigned short* (*PatchyJpegDeepAlloc)(void* context, const PatchyJpegDeepHeader* header);

enum {
  PATCHY_JPEG_DEEP_OK = 0,
  PATCHY_JPEG_DEEP_DAMAGED = 1,   /* libjpeg-turbo's reason is in `message` */
  PATCHY_JPEG_DEEP_CMYK = 2,      /* four-channel CMYK or YCCK samples */
  PATCHY_JPEG_DEEP_NO_MEMORY = 3,
  PATCHY_JPEG_DEEP_TOO_LARGE = 4  /* more than max_pixels */
};

/* Decodes the whole image as raw sample values (0 to 2^precision - 1), rows top down,
 * components interleaved. `lossless` says the frame is SOF3/SOF11 (the caller's marker scan
 * found it; libjpeg keeps the flag internal). */
int patchy_jpeg_deep_decode(const unsigned char* data, size_t size, int lossless, unsigned long long max_pixels,
                            PatchyJpegDeepAlloc alloc, void* context, char* message, size_t message_size);

#ifdef __cplusplus
}
#endif

#endif
