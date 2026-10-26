#include "formats/jpeg_deep_glue.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "patchy_libjpeg_symbols.h"

#include "jpeglib.h"

/* The jmp_buf lives in the caller's frame: inside this struct its alignment would pad it. */
typedef struct ErrorManager {
  struct jpeg_error_mgr pub;
  jmp_buf* jump;
  char message[JMSG_LENGTH_MAX];
} ErrorManager;

static void error_exit(j_common_ptr cinfo) {
  ErrorManager* manager = (ErrorManager*)cinfo->err;
  (*cinfo->err->format_message)(cinfo, manager->message);
  longjmp(*manager->jump, 1);
}

/* Warnings (corrupt data the decoder recovered from) stay silent: the image still opens. */
static void output_message(j_common_ptr cinfo) {
  (void)cinfo;
}

static void copy_message(const char* text, char* message, size_t message_size) {
  if (message == NULL || message_size == 0) {
    return;
  }
  size_t i = 0;
  for (; i + 1 < message_size && text[i] != '\0'; ++i) {
    message[i] = text[i];
  }
  message[i] = '\0';
}

/* Everything that can raise libjpeg's error exit runs here, below the setjmp frame, so no
 * local of the caller is live across the longjmp. */
static int decode_body(j_decompress_ptr cinfo, const unsigned char* data, size_t size, int lossless,
                       unsigned long long max_pixels, PatchyJpegDeepAlloc alloc, void* context, char* message, size_t message_size) {
  PatchyJpegDeepHeader header;
  unsigned short* out = NULL;
  void* row = NULL;
  size_t row_samples = 0;

  memset(&header, 0, sizeof header);
  jpeg_mem_src(cinfo, data, (unsigned long)size);
  jpeg_read_header(cinfo, TRUE);
  header.width = cinfo->image_width;
  header.height = cinfo->image_height;
  header.precision = cinfo->data_precision;
  header.lossless = lossless;
  switch (cinfo->jpeg_color_space) {
    case JCS_GRAYSCALE:
      cinfo->out_color_space = JCS_GRAYSCALE;
      header.components = 1;
      break;
    case JCS_CMYK:
    case JCS_YCCK:
      return PATCHY_JPEG_DEEP_CMYK;
    case JCS_YCbCr:
      header.ycc = header.lossless;
      cinfo->out_color_space = header.lossless ? JCS_YCbCr : JCS_RGB;
      header.components = 3;
      break;
    default:
      cinfo->out_color_space = JCS_RGB;
      header.components = 3;
      break;
  }
  if ((unsigned long long)header.width * header.height > max_pixels) {
    return PATCHY_JPEG_DEEP_TOO_LARGE;
  }

  jpeg_start_decompress(cinfo);
  if (cinfo->output_components != header.components || cinfo->output_width != header.width ||
      cinfo->output_height != header.height) {
    copy_message("unexpected output layout", message, message_size);
    return PATCHY_JPEG_DEEP_DAMAGED;
  }
  out = alloc(context, &header);
  if (out == NULL) {
    return PATCHY_JPEG_DEEP_NO_MEMORY;
  }
  row_samples = (size_t)header.width * (size_t)header.components;
  row = (*cinfo->mem->alloc_large)((j_common_ptr)cinfo, JPOOL_IMAGE, row_samples * sizeof(J16SAMPLE));
  while (cinfo->output_scanline < cinfo->output_height) {
    unsigned short* dest = out + (size_t)cinfo->output_scanline * row_samples;
    size_t i;
    if (cinfo->data_precision <= 8) {
      JSAMPROW rows[1];
      rows[0] = (JSAMPROW)row;
      if (jpeg_read_scanlines(cinfo, rows, 1) != 1) {
        break;
      }
      for (i = 0; i < row_samples; ++i) {
        dest[i] = rows[0][i];
      }
    } else if (cinfo->data_precision <= 12) {
      J12SAMPROW rows[1];
      rows[0] = (J12SAMPROW)row;
      if (jpeg12_read_scanlines(cinfo, rows, 1) != 1) {
        break;
      }
      for (i = 0; i < row_samples; ++i) {
        dest[i] = (unsigned short)(rows[0][i] < 0 ? 0 : rows[0][i]);
      }
    } else {
      J16SAMPROW rows[1];
      rows[0] = (J16SAMPROW)row;
      if (jpeg16_read_scanlines(cinfo, rows, 1) != 1) {
        break;
      }
      memcpy(dest, rows[0], row_samples * sizeof(unsigned short));
    }
  }
  if (cinfo->output_scanline < cinfo->output_height) {
    copy_message("the image data ends early", message, message_size);
    return PATCHY_JPEG_DEEP_DAMAGED;
  }
  jpeg_finish_decompress(cinfo);
  return PATCHY_JPEG_DEEP_OK;
}

int patchy_jpeg_deep_decode(const unsigned char* data, size_t size, int lossless, unsigned long long max_pixels,
                            PatchyJpegDeepAlloc alloc, void* context, char* message, size_t message_size) {
  struct jpeg_decompress_struct cinfo;
  ErrorManager errors;
  jmp_buf jump;
  int result = PATCHY_JPEG_DEEP_DAMAGED;

  copy_message("", message, message_size);
  memset(&cinfo, 0, sizeof cinfo);
  cinfo.err = jpeg_std_error(&errors.pub);
  errors.pub.error_exit = error_exit;
  errors.pub.output_message = output_message;
  errors.message[0] = '\0';
  errors.jump = &jump;
  if (setjmp(jump)) {
    copy_message(errors.message, message, message_size);
    jpeg_destroy_decompress(&cinfo);
    return PATCHY_JPEG_DEEP_DAMAGED;
  }
  jpeg_create_decompress(&cinfo);
  result = decode_body(&cinfo, data, size, lossless, max_pixels, alloc, context, message, message_size);
  jpeg_destroy_decompress(&cinfo);
  return result;
}
