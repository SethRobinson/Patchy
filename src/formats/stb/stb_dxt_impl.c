/* Implementation TU for the vendored stb_dxt (v1.12, public domain / MIT), the
 * BC1/BC3 block encoder behind the DDS writer. Built with floating-point
 * contraction disabled (see CMakeLists.txt) so the encoder's PCA produces the
 * same bytes on every toolchain; dds_writer_bytes_are_stable pins the output.
 */
#include <string.h> /* stb_dxt uses memcpy but does not include it */

#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"
