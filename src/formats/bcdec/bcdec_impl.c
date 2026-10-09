/* Implementation TU for the vendored bcdec (v0.985, MIT / Unlicense), the block
 * decoder behind the DDS reader (BC1-BC7, BC6H included). BCDEC_BC4BC5_PRECISE
 * selects the signed/unsigned-aware BC4/BC5 entry points; every consumer defines
 * the same macro before including bcdec.h so the declarations agree.
 */
#define BCDEC_BC4BC5_PRECISE
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"
