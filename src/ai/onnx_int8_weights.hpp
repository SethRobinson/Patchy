#pragma once

// The int8 models' weights as uint8 for x64 CPUs without VNNI (docs/ai-selection-models.md,
// "Older x64 CPUs"). Qt-free and independent of ONNX Runtime: it edits the serialized model.
//
// MobileSAM and the subject model are dynamically quantized: each MatMulInteger multiplies an
// unsigned activation by a signed constant weight. On an x64 CPU without VNNI (AVX2, AVX-512
// before VNNI) ONNX Runtime does that with an instruction that adds two products in 16 bits
// and saturates, so the decoder's positional encoding of a click or box came out wrong (an
// int32 result 31748 off, its sine and cosine up to 2 off) and the masks were worse than on
// every other CPU. Stored as uint8 with the zero point moved by the same 128, (w - zp) and
// every product stay the same integers, and ONNX Runtime multiplies unsigned by unsigned,
// which does not saturate: the result is bit for bit what a VNNI CPU computes.

#include <optional>
#include <string>
#include <string_view>

namespace patchy::ai {

// The model with every MatMulInteger's constant int8 weight (input 1) and zero point (input 3)
// as uint8, value + 128. A weight or zero point is converted only when every use of it is such
// a MatMulInteger whose other one is converted too, so the model stays consistent. Nothing (an
// empty optional) when nothing qualifies or anything is outside what this handles: a node with
// a subgraph, an initializer that is also listed as a graph input, output or value_info entry,
// external data, a malformed message. Never throws.
[[nodiscard]] std::optional<std::string> matmul_integer_weights_as_uint8(std::string_view model);

// True on an x64 CPU whose int8 matrix kernels saturate: no AVX-VNNI and no usable
// AVX-512 VNNI. False on every other CPU (ARM, x64 with VNNI).
[[nodiscard]] bool cpu_int8_matmul_saturates();

// Whether the ONNX layer loads an int8 model (a file named *_int8.onnx) through
// matmul_integer_weights_as_uint8: on a CPU that saturates, unless a test decided otherwise.
// CPUs with VNNI keep the files as they are, since their signed kernels are exact and faster.
[[nodiscard]] bool int8_weight_rewrite_enabled();
// nullopt: the CPU decides. The caller releases loaded sessions around a change.
void set_int8_weight_rewrite_for_testing(std::optional<bool> enabled);

}  // namespace patchy::ai
