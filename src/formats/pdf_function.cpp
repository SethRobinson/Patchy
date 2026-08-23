#include "formats/pdf_function.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

namespace patchy::pdf {
namespace {

double interpolate(double value, double from_low, double from_high, double to_low, double to_high) {
  const double spread = from_high - from_low;
  if (std::abs(spread) < 1e-12) {
    return to_low;
  }
  return to_low + (value - from_low) * (to_high - to_low) / spread;
}

// --- Type 0: sampled -----------------------------------------------------------

class SampledFunction final : public PdfFunction {
public:
  std::vector<int> size;             // samples per input dimension
  int bits_per_sample{8};
  std::vector<double> encode;        // pairs per input
  std::vector<double> decode;        // pairs per output
  std::vector<std::uint8_t> samples;

  [[nodiscard]] double raw_sample(std::size_t flat_index, int output) const {
    const std::size_t sample_index = flat_index * static_cast<std::size_t>(output_count_) +
                                     static_cast<std::size_t>(output);
    const std::size_t bit_offset = sample_index * static_cast<std::size_t>(bits_per_sample);
    const std::size_t byte_offset = bit_offset / 8;
    if (byte_offset >= samples.size()) {
      return 0.0;
    }
    std::uint64_t value = 0;
    // Samples are packed big-endian, MSB first, no row padding (clause 7.10.2).
    std::size_t bits_read = 0;
    std::size_t position = byte_offset;
    int skip = static_cast<int>(bit_offset % 8);
    while (bits_read < static_cast<std::size_t>(bits_per_sample) && position < samples.size()) {
      const int available = 8 - skip;
      const int take = std::min<int>(available, bits_per_sample - static_cast<int>(bits_read));
      const int shift = available - take;
      const auto mask = static_cast<std::uint8_t>((1U << take) - 1U);
      value = (value << take) | ((samples[position] >> shift) & mask);
      bits_read += static_cast<std::size_t>(take);
      skip += take;
      if (skip >= 8) {
        skip = 0;
        ++position;
      }
    }
    const double maximum = static_cast<double>((1ULL << std::min(bits_per_sample, 32)) - 1ULL);
    return static_cast<double>(value) / maximum;
  }

protected:
  void evaluate_clamped(std::span<const double> inputs, std::vector<double>& outputs) const override {
    const int input_dimensions = input_count();
    // Encoded coordinates per input, split into a floor cell and a fraction for the
    // multilinear blend across the 2^m surrounding samples.
    std::vector<double> encoded(static_cast<std::size_t>(input_dimensions));
    std::vector<int> cell(static_cast<std::size_t>(input_dimensions));
    std::vector<double> fraction(static_cast<std::size_t>(input_dimensions));
    for (int dimension = 0; dimension < input_dimensions; ++dimension) {
      const auto d = static_cast<std::size_t>(dimension);
      const double low = domain_[d * 2];
      const double high = domain_[d * 2 + 1];
      const double encode_low = d * 2 + 1 < encode.size() ? encode[d * 2] : 0.0;
      const double encode_high = d * 2 + 1 < encode.size() ? encode[d * 2 + 1]
                                                           : static_cast<double>(size[d] - 1);
      double position = interpolate(inputs[d], low, high, encode_low, encode_high);
      position = std::clamp(position, 0.0, static_cast<double>(size[d] - 1));
      cell[d] = std::min(static_cast<int>(position), size[d] - 2 >= 0 ? size[d] - 2 : 0);
      cell[d] = std::max(cell[d], 0);
      fraction[d] = size[d] > 1 ? position - cell[d] : 0.0;
    }

    for (int output = 0; output < output_count_; ++output) {
      double accumulated = 0.0;
      const int corners = 1 << input_dimensions;
      for (int corner = 0; corner < corners; ++corner) {
        double weight = 1.0;
        std::size_t flat = 0;
        std::size_t stride = 1;
        for (int dimension = 0; dimension < input_dimensions; ++dimension) {
          const auto d = static_cast<std::size_t>(dimension);
          const bool upper = ((corner >> dimension) & 1) != 0;
          weight *= upper ? fraction[d] : 1.0 - fraction[d];
          const int coordinate = std::min(cell[d] + (upper ? 1 : 0), size[d] - 1);
          flat += static_cast<std::size_t>(coordinate) * stride;
          stride *= static_cast<std::size_t>(size[d]);
        }
        if (weight > 0.0) {
          accumulated += weight * raw_sample(flat, output);
        }
      }
      const auto o = static_cast<std::size_t>(output);
      const double decode_low = o * 2 + 1 < decode.size() ? decode[o * 2]
                                : o * 2 + 1 < range_.size() ? range_[o * 2] : 0.0;
      const double decode_high = o * 2 + 1 < decode.size() ? decode[o * 2 + 1]
                                 : o * 2 + 1 < range_.size() ? range_[o * 2 + 1] : 1.0;
      outputs[o] = interpolate(accumulated, 0.0, 1.0, decode_low, decode_high);
    }
  }
};

// --- Type 2: exponential -------------------------------------------------------

class ExponentialFunction final : public PdfFunction {
public:
  std::vector<double> c0{0.0};
  std::vector<double> c1{1.0};
  double exponent{1.0};

protected:
  void evaluate_clamped(std::span<const double> inputs, std::vector<double>& outputs) const override {
    const double t = inputs.empty() ? 0.0 : inputs[0];
    // Negative t with a fractional N would be NaN; the domain clamp already keeps t
    // inside what the file declared, so only guard the pow itself.
    const double factor = std::pow(std::max(t, 0.0), exponent);
    for (int output = 0; output < output_count_; ++output) {
      const auto o = static_cast<std::size_t>(output);
      const double from = o < c0.size() ? c0[o] : 0.0;
      const double to = o < c1.size() ? c1[o] : 1.0;
      outputs[o] = from + factor * (to - from);
    }
  }
};

// --- Type 3: stitching ---------------------------------------------------------

class StitchingFunction final : public PdfFunction {
public:
  std::vector<std::unique_ptr<PdfFunction>> parts;
  std::vector<double> bounds;
  std::vector<double> encode;  // pairs per part

protected:
  void evaluate_clamped(std::span<const double> inputs, std::vector<double>& outputs) const override {
    const double t = inputs.empty() ? 0.0 : inputs[0];
    const double low = domain_[0];
    const double high = domain_[1];
    std::size_t part = 0;
    while (part < bounds.size() && t >= bounds[part]) {
      ++part;
    }
    part = std::min(part, parts.empty() ? std::size_t{0} : parts.size() - 1);
    if (parts.empty() || parts[part] == nullptr) {
      std::fill(outputs.begin(), outputs.end(), 0.0);
      return;
    }
    const double segment_low = part == 0 ? low : bounds[part - 1];
    const double segment_high = part < bounds.size() ? bounds[part] : high;
    const double encode_low = part * 2 + 1 < encode.size() ? encode[part * 2] : 0.0;
    const double encode_high = part * 2 + 1 < encode.size() ? encode[part * 2 + 1] : 1.0;
    const double mapped = interpolate(t, segment_low, segment_high, encode_low, encode_high);
    const double sub_input[1] = {mapped};
    parts[part]->evaluate(sub_input, outputs);
    outputs.resize(static_cast<std::size_t>(output_count_), 0.0);
  }
};

// --- Type 4: PostScript calculator ---------------------------------------------

// The expression is tokenized once into a flat program; braces become Begin/End
// markers so if/ifelse can skip their procedure bodies by matching depth.
struct CalculatorToken {
  enum class Kind { Number, Operator, Begin, End } kind{Kind::Number};
  double number{0.0};
  std::string op;
};

class CalculatorFunction final : public PdfFunction {
public:
  std::vector<CalculatorToken> program;

protected:
  void evaluate_clamped(std::span<const double> inputs, std::vector<double>& outputs) const override {
    procedure_starts_.clear();  // a malformed program must not leak bodies across calls
    std::vector<double> stack(inputs.begin(), inputs.end());
    stack.reserve(100);
    // The outermost braces wrap the whole program; execution starts inside them.
    std::size_t begin = 0;
    if (!program.empty() && program[0].kind == CalculatorToken::Kind::Begin) {
      begin = 1;
    }
    run(begin, program.size(), stack, 0);
    // Outputs are the top n stack values, in order (clause 7.10.5.1).
    for (int output = 0; output < output_count_; ++output) {
      const auto o = static_cast<std::size_t>(output);
      const std::size_t from_top = static_cast<std::size_t>(output_count_) - o;
      outputs[o] = stack.size() >= from_top ? stack[stack.size() - from_top] : 0.0;
    }
  }

private:
  // Executes tokens in [start, stop). `depth` guards recursive procedures.
  void run(std::size_t start, std::size_t stop, std::vector<double>& stack, int depth) const {
    if (depth > 32) {
      return;
    }
    const auto pop = [&stack]() -> double {
      if (stack.empty()) {
        return 0.0;
      }
      const double value = stack.back();
      stack.pop_back();
      return value;
    };
    const auto skip_procedure = [this](std::size_t at) -> std::size_t {
      // `at` points at a Begin; returns the index just past its matching End.
      int nesting = 0;
      std::size_t index = at;
      for (; index < program.size(); ++index) {
        if (program[index].kind == CalculatorToken::Kind::Begin) {
          ++nesting;
        } else if (program[index].kind == CalculatorToken::Kind::End && --nesting == 0) {
          return index + 1;
        }
      }
      return index;
    };

    std::size_t index = start;
    while (index < stop && index < program.size() && stack.size() < 1000) {
      const auto& token = program[index];
      if (token.kind == CalculatorToken::Kind::Number) {
        stack.push_back(token.number);
        ++index;
        continue;
      }
      if (token.kind == CalculatorToken::Kind::Begin) {
        // A procedure body: recorded for a following if/ifelse, executed there.
        procedure_starts_.push_back(index);
        index = skip_procedure(index);
        continue;
      }
      if (token.kind == CalculatorToken::Kind::End) {
        ++index;
        continue;
      }

      const auto& op = token.op;
      ++index;
      if (op == "add") { const double b = pop(); stack.push_back(pop() + b); continue; }
      if (op == "sub") { const double b = pop(); stack.push_back(pop() - b); continue; }
      if (op == "mul") { const double b = pop(); stack.push_back(pop() * b); continue; }
      if (op == "div") { const double b = pop(); const double a = pop();
                         stack.push_back(std::abs(b) < 1e-12 ? 0.0 : a / b); continue; }
      if (op == "idiv") { const auto b = static_cast<long long>(pop()); const auto a = static_cast<long long>(pop());
                          stack.push_back(b == 0 ? 0.0 : static_cast<double>(a / b)); continue; }
      if (op == "mod") { const auto b = static_cast<long long>(pop()); const auto a = static_cast<long long>(pop());
                         stack.push_back(b == 0 ? 0.0 : static_cast<double>(a % b)); continue; }
      if (op == "neg") { stack.push_back(-pop()); continue; }
      if (op == "abs") { stack.push_back(std::abs(pop())); continue; }
      if (op == "sqrt") { stack.push_back(std::sqrt(std::max(0.0, pop()))); continue; }
      if (op == "sin") { stack.push_back(std::sin(pop() * std::numbers::pi / 180.0)); continue; }
      if (op == "cos") { stack.push_back(std::cos(pop() * std::numbers::pi / 180.0)); continue; }
      if (op == "atan") {
        const double b = pop();
        const double a = pop();
        double degrees = std::atan2(a, b) * 180.0 / std::numbers::pi;
        if (degrees < 0.0) {
          degrees += 360.0;
        }
        stack.push_back(degrees);
        continue;
      }
      if (op == "exp") { const double b = pop(); stack.push_back(std::pow(pop(), b)); continue; }
      if (op == "ln") { const double a = pop(); stack.push_back(a > 0.0 ? std::log(a) : 0.0); continue; }
      if (op == "log") { const double a = pop(); stack.push_back(a > 0.0 ? std::log10(a) : 0.0); continue; }
      if (op == "cvi" || op == "truncate") { stack.push_back(std::trunc(pop())); continue; }
      if (op == "cvr") { continue; }
      if (op == "floor") { stack.push_back(std::floor(pop())); continue; }
      if (op == "ceiling") { stack.push_back(std::ceil(pop())); continue; }
      if (op == "round") { stack.push_back(std::round(pop())); continue; }
      if (op == "dup") { const double a = pop(); stack.push_back(a); stack.push_back(a); continue; }
      if (op == "pop") { (void)pop(); continue; }
      if (op == "exch") { const double b = pop(); const double a = pop();
                          stack.push_back(b); stack.push_back(a); continue; }
      if (op == "copy") {
        const auto count = static_cast<std::size_t>(std::max(0.0, pop()));
        if (count <= stack.size() && count < 100) {
          const std::size_t base = stack.size() - count;
          for (std::size_t offset = 0; offset < count; ++offset) {
            stack.push_back(stack[base + offset]);
          }
        }
        continue;
      }
      if (op == "index") {
        const auto position = static_cast<std::size_t>(std::max(0.0, pop()));
        stack.push_back(position < stack.size() ? stack[stack.size() - 1 - position] : 0.0);
        continue;
      }
      if (op == "roll") {
        const auto shift = static_cast<long long>(pop());
        const auto count = static_cast<std::size_t>(std::max(0.0, pop()));
        if (count > 1 && count <= stack.size()) {
          const auto begin_roll = stack.end() - static_cast<std::ptrdiff_t>(count);
          const long long effective = ((shift % static_cast<long long>(count)) + static_cast<long long>(count)) %
                                      static_cast<long long>(count);
          std::rotate(begin_roll, stack.end() - effective, stack.end());
        }
        continue;
      }
      if (op == "eq") { const double b = pop(); stack.push_back(pop() == b ? 1.0 : 0.0); continue; }
      if (op == "ne") { const double b = pop(); stack.push_back(pop() != b ? 1.0 : 0.0); continue; }
      if (op == "gt") { const double b = pop(); stack.push_back(pop() > b ? 1.0 : 0.0); continue; }
      if (op == "ge") { const double b = pop(); stack.push_back(pop() >= b ? 1.0 : 0.0); continue; }
      if (op == "lt") { const double b = pop(); stack.push_back(pop() < b ? 1.0 : 0.0); continue; }
      if (op == "le") { const double b = pop(); stack.push_back(pop() <= b ? 1.0 : 0.0); continue; }
      if (op == "and") { const auto b = static_cast<long long>(pop()); const auto a = static_cast<long long>(pop());
                         stack.push_back(static_cast<double>(a & b)); continue; }
      if (op == "or") { const auto b = static_cast<long long>(pop()); const auto a = static_cast<long long>(pop());
                        stack.push_back(static_cast<double>(a | b)); continue; }
      if (op == "xor") { const auto b = static_cast<long long>(pop()); const auto a = static_cast<long long>(pop());
                         stack.push_back(static_cast<double>(a ^ b)); continue; }
      if (op == "not") {
        const double a = pop();
        // Boolean when 0/1, bitwise otherwise; comparisons here only produce 0/1.
        stack.push_back(a == 0.0 ? 1.0 : a == 1.0 ? 0.0 : static_cast<double>(~static_cast<long long>(a)));
        continue;
      }
      if (op == "bitshift") {
        const auto shift = static_cast<long long>(pop());
        const auto a = static_cast<long long>(pop());
        stack.push_back(static_cast<double>(shift >= 0 ? a << shift : a >> -shift));
        continue;
      }
      if (op == "true") { stack.push_back(1.0); continue; }
      if (op == "false") { stack.push_back(0.0); continue; }
      if (op == "if") {
        const std::size_t body = take_procedure();
        if (pop() != 0.0 && body < program.size()) {
          run(body + 1, matching_end(body), stack, depth + 1);
        }
        continue;
      }
      if (op == "ifelse") {
        const std::size_t else_body = take_procedure();
        const std::size_t then_body = take_procedure();
        const bool condition = pop() != 0.0;
        const std::size_t chosen = condition ? then_body : else_body;
        if (chosen < program.size()) {
          run(chosen + 1, matching_end(chosen), stack, depth + 1);
        }
        continue;
      }
      // An unknown operator is skipped; the domain clamp keeps results bounded.
    }
  }

  [[nodiscard]] std::size_t matching_end(std::size_t begin) const {
    int nesting = 0;
    for (std::size_t index = begin; index < program.size(); ++index) {
      if (program[index].kind == CalculatorToken::Kind::Begin) {
        ++nesting;
      } else if (program[index].kind == CalculatorToken::Kind::End && --nesting == 0) {
        return index;
      }
    }
    return program.size();
  }

  std::size_t take_procedure() const {
    if (procedure_starts_.empty()) {
      return program.size();
    }
    const std::size_t body = procedure_starts_.back();
    procedure_starts_.pop_back();
    return body;
  }

  // Pending procedure bodies awaiting their if/ifelse. Mutable because evaluation
  // is logically const; the vector is empty between evaluations.
  mutable std::vector<std::size_t> procedure_starts_;
};

std::vector<double> read_numbers(const File& file, const Object& spec) {
  return file.numbers(spec);
}

}  // namespace

void PdfFunction::evaluate(std::span<const double> inputs, std::vector<double>& outputs) const {
  std::vector<double> clamped(static_cast<std::size_t>(input_count()), 0.0);
  for (int input = 0; input < input_count(); ++input) {
    const auto i = static_cast<std::size_t>(input);
    const double value = i < inputs.size() ? inputs[i] : 0.0;
    clamped[i] = std::clamp(value, domain_[i * 2], domain_[i * 2 + 1]);
  }
  outputs.assign(static_cast<std::size_t>(output_count_), 0.0);
  evaluate_clamped(clamped, outputs);
  for (int output = 0; output < output_count_ && static_cast<std::size_t>(output * 2 + 1) < range_.size();
       ++output) {
    const auto o = static_cast<std::size_t>(output);
    outputs[o] = std::clamp(outputs[o], range_[o * 2], range_[o * 2 + 1]);
  }
}

std::unique_ptr<PdfFunction> load_function(const File& file, const Object& spec) {
  const auto& resolved = file.resolve(spec);
  // dictionary(), not is_dictionary(): sampled and calculator functions are STREAM
  // objects, and a stream answers dictionary() with its own dictionary while
  // is_dictionary() (the variant check) says no.
  if (resolved.dictionary() == nullptr) {
    return nullptr;  // includes /Identity and null
  }
  const auto type = file.get(resolved, "FunctionType").integer(-1);
  auto domain = read_numbers(file, resolved.get("Domain"));
  auto range = read_numbers(file, resolved.get("Range"));
  if (domain.size() < 2) {
    domain = {0.0, 1.0};
  }

  if (type == 2) {
    auto function = std::make_unique<ExponentialFunction>();
    function->domain_ = std::move(domain);
    function->range_ = range;
    function->c0 = read_numbers(file, resolved.get("C0"));
    function->c1 = read_numbers(file, resolved.get("C1"));
    if (function->c0.empty()) {
      function->c0 = {0.0};
    }
    if (function->c1.empty()) {
      function->c1 = {1.0};
    }
    function->exponent = file.get(resolved, "N").number(1.0);
    function->output_count_ = static_cast<int>(std::max(function->c0.size(), function->c1.size()));
    if (!range.empty()) {
      function->output_count_ = static_cast<int>(range.size() / 2);
    }
    return function;
  }

  if (type == 3) {
    auto function = std::make_unique<StitchingFunction>();
    function->domain_ = std::move(domain);
    function->range_ = range;
    function->bounds = read_numbers(file, resolved.get("Bounds"));
    function->encode = read_numbers(file, resolved.get("Encode"));
    const auto& parts = file.get(resolved, "Functions");
    if (const auto* array = parts.array(); array != nullptr) {
      for (const auto& entry : *array) {
        function->parts.push_back(load_function(file, entry));
        if (function->parts.back() == nullptr) {
          return nullptr;  // a broken subfunction breaks the whole stitch
        }
      }
    }
    if (function->parts.empty()) {
      return nullptr;
    }
    function->output_count_ = !range.empty() ? static_cast<int>(range.size() / 2)
                                             : function->parts.front()->output_count();
    return function;
  }

  if (type == 0) {
    if (resolved.stream() == nullptr || range.empty()) {
      return nullptr;  // /Range is required for sampled functions
    }
    auto function = std::make_unique<SampledFunction>();
    function->domain_ = std::move(domain);
    function->range_ = range;
    function->output_count_ = static_cast<int>(range.size() / 2);
    for (const double dimension : read_numbers(file, resolved.get("Size"))) {
      function->size.push_back(std::max(1, static_cast<int>(dimension)));
    }
    if (function->size.empty() || function->size.size() != function->domain_.size() / 2 ||
        function->size.size() > 8) {
      return nullptr;
    }
    function->bits_per_sample = static_cast<int>(file.get(resolved, "BitsPerSample").integer(8));
    if (function->bits_per_sample <= 0 || function->bits_per_sample > 32) {
      return nullptr;
    }
    function->encode = read_numbers(file, resolved.get("Encode"));
    function->decode = read_numbers(file, resolved.get("Decode"));
    function->samples = file.stream_data(resolved).data;
    if (function->samples.empty()) {
      return nullptr;
    }
    return function;
  }

  if (type == 4) {
    if (resolved.stream() == nullptr) {
      return nullptr;
    }
    const auto data = file.stream_data(resolved);
    auto function = std::make_unique<CalculatorFunction>();
    function->domain_ = std::move(domain);
    function->range_ = range;
    function->output_count_ = !range.empty() ? static_cast<int>(range.size() / 2) : 1;
    Lexer lexer(data.data);
    while (true) {
      const auto before = lexer.position();
      auto token = lexer.next();
      if (!token.has_value() || lexer.position() == before) {
        break;
      }
      CalculatorToken parsed;
      if (token->is_keyword()) {
        if (token->keyword == "{") {
          parsed.kind = CalculatorToken::Kind::Begin;
        } else if (token->keyword == "}") {
          parsed.kind = CalculatorToken::Kind::End;
        } else {
          parsed.kind = CalculatorToken::Kind::Operator;
          parsed.op = token->keyword;
        }
      } else if (token->object.is_number()) {
        parsed.kind = CalculatorToken::Kind::Number;
        parsed.number = token->object.number();
      } else if (token->object.is_boolean()) {
        parsed.kind = CalculatorToken::Kind::Number;
        parsed.number = token->object.boolean() ? 1.0 : 0.0;
      } else {
        continue;
      }
      function->program.push_back(std::move(parsed));
      if (function->program.size() > 100000) {
        return nullptr;
      }
    }
    if (function->program.empty()) {
      return nullptr;
    }
    return function;
  }

  return nullptr;
}

FunctionSet FunctionSet::load(const File& file, const Object& spec) {
  FunctionSet set;
  const auto& resolved = file.resolve(spec);
  if (const auto* array = resolved.array(); array != nullptr) {
    for (const auto& entry : *array) {
      auto function = load_function(file, entry);
      if (function == nullptr) {
        set.functions_.clear();
        return set;
      }
      set.functions_.push_back(std::move(function));
    }
    return set;
  }
  if (auto function = load_function(file, resolved); function != nullptr) {
    set.functions_.push_back(std::move(function));
  }
  return set;
}

std::vector<double> FunctionSet::evaluate(double t) const {
  std::vector<double> results;
  const double inputs[1] = {t};
  std::vector<double> outputs;
  for (const auto& function : functions_) {
    function->evaluate(inputs, outputs);
    results.insert(results.end(), outputs.begin(), outputs.end());
  }
  return results;
}

}  // namespace patchy::pdf
