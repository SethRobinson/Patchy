#pragma once

#include "formats/pdf_file.hpp"

#include <memory>
#include <span>
#include <vector>

// PDF functions (ISO 32000-1 clause 7.10): the m-input n-output maps behind
// shading colour ramps and Separation/DeviceN tint transforms.
//
//   Type 0  sampled       (a stream of quantized samples, interpolated)
//   Type 2  exponential   C0 + (C1 - C0) * t^N
//   Type 3  stitching     subfunctions spliced over /Bounds
//   Type 4  calculator    a small PostScript expression
//
// Evaluation is deterministic double arithmetic with explicit clamping, so results
// are stable across toolchains (the shading stops sampled from these end up in
// documents and in tests).

namespace patchy::pdf {

class PdfFunction {
public:
  virtual ~PdfFunction() = default;

  // Clamps inputs to /Domain, evaluates, clamps outputs to /Range when one was
  // declared. `outputs` is resized to the function's output count.
  void evaluate(std::span<const double> inputs, std::vector<double>& outputs) const;

  [[nodiscard]] int input_count() const noexcept { return static_cast<int>(domain_.size() / 2); }
  [[nodiscard]] int output_count() const noexcept { return output_count_; }

protected:
  virtual void evaluate_clamped(std::span<const double> inputs, std::vector<double>& outputs) const = 0;

  std::vector<double> domain_;  // pairs: low, high per input
  std::vector<double> range_;   // pairs per output; empty when undeclared
  int output_count_{0};

  friend std::unique_ptr<PdfFunction> load_function(const File& file, const Object& spec);
};

// Returns nullptr when the object is not a function or its type is unusable.
// An /Identity name (legal where a function is optional) returns nullptr too;
// callers treat that as the identity map.
[[nodiscard]] std::unique_ptr<PdfFunction> load_function(const File& file, const Object& spec);

// The common "one function with n outputs OR an array of n one-output functions"
// form shadings use. Evaluates at scalar t.
class FunctionSet {
public:
  static FunctionSet load(const File& file, const Object& spec);

  [[nodiscard]] bool valid() const noexcept { return !functions_.empty(); }
  [[nodiscard]] std::vector<double> evaluate(double t) const;

private:
  std::vector<std::unique_ptr<PdfFunction>> functions_;
};

}  // namespace patchy::pdf
