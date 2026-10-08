#include "core/document_depth.hpp"

#include "core/pixel_depth.hpp"
#include "core/smart_filter.hpp"

#include <utility>

namespace patchy {
namespace {

const char* depth_label(BitDepth depth) noexcept {
  switch (depth) {
    case BitDepth::UInt8:
      return "8-bit";
    case BitDepth::UInt16:
      return "16-bit";
    case BitDepth::Float32:
      return "32-bit";
  }
  return "?";
}

void convert_layer(Layer& layer, BitDepth depth) {
  const auto& pixels = std::as_const(layer).pixels();
  if (!pixels.empty() && pixels.format().bit_depth != depth) {
    const auto kind = pixels.format().channels >= 3 ? SampleKind::Color : SampleKind::Coverage;
    auto converted = convert_pixel_buffer_depth(pixels, depth, kind);
    // Assigned through the mutable accessor (which bumps every revision): set_pixels
    // would also turn a text or smart object layer into a pixel layer at the origin.
    layer.pixels() = std::move(converted);
  }
  if (const auto& mask = std::as_const(layer).mask();
      mask.has_value() && !mask->pixels.empty() && mask->pixels.format().bit_depth != depth) {
    auto converted = *mask;
    converted.pixels = convert_pixel_buffer_depth(mask->pixels, depth, SampleKind::Coverage);
    layer.set_mask(std::move(converted));
  }
  if (const auto* stack = layer.smart_filter_stack();
      stack != nullptr && !stack->mask.pixels.empty() && stack->mask.pixels.format().bit_depth != depth) {
    auto converted = *stack;
    converted.mask.pixels = convert_pixel_buffer_depth(stack->mask.pixels, depth, SampleKind::Coverage);
    layer.set_smart_filter_stack(std::move(converted));
  }
  for (auto& child : layer.children()) {
    convert_layer(child, depth);
  }
}

void collect_layer_problems(const Layer& layer, BitDepth depth, std::vector<std::string>& problems) {
  const auto& pixels = layer.pixels();
  // Pixel layers own their pixels; other kinds hold derived rasters.
  if (layer.kind() == LayerKind::Pixel && !pixels.empty() && pixels.format().bit_depth != depth) {
    problems.push_back("layer '" + layer.name() + "' is " + depth_label(pixels.format().bit_depth));
  }
  if (layer.mask().has_value() && !layer.mask()->pixels.empty() &&
      layer.mask()->pixels.format().bit_depth != depth) {
    problems.push_back("mask of layer '" + layer.name() + "' is " +
                       depth_label(layer.mask()->pixels.format().bit_depth));
  }
  if (const auto* stack = layer.smart_filter_stack();
      stack != nullptr && !stack->mask.pixels.empty() && stack->mask.pixels.format().bit_depth != depth) {
    problems.push_back("smart filter mask of layer '" + layer.name() + "' is " +
                       depth_label(stack->mask.pixels.format().bit_depth));
  }
  for (const auto& child : layer.children()) {
    collect_layer_problems(child, depth, problems);
  }
}

}  // namespace

BitDepth document_bit_depth(const Document& document) noexcept {
  return document.color_state().bit_depth;
}

void convert_document_depth(Document& document, BitDepth depth) {
  for (auto& layer : document.layers()) {
    convert_layer(layer, depth);
  }
  for (auto& channel : document.channels()) {
    const auto& pixels = std::as_const(channel).pixels();
    if (!pixels.empty() && pixels.format().bit_depth != depth) {
      channel.set_pixels(convert_pixel_buffer_depth(pixels, depth, SampleKind::Coverage));
    }
  }
  if (auto& composite = document.metadata().psd_flat_composite;
      composite.has_value() && composite->format().bit_depth != depth) {
    composite = convert_pixel_buffer_depth(*composite, depth, SampleKind::Color);
  }
  document.color_state().bit_depth = depth;
  document.set_format(with_bit_depth(document.format(), depth));
}

std::vector<std::string> document_depth_problems(const Document& document) {
  std::vector<std::string> problems;
  const auto depth = document_bit_depth(document);
  if (document.format().bit_depth != depth) {
    problems.push_back(std::string("document format is ") + depth_label(document.format().bit_depth) +
                       " but its color state is " + depth_label(depth));
  }
  for (const auto& layer : document.layers()) {
    collect_layer_problems(layer, depth, problems);
  }
  for (const auto& channel : document.channels()) {
    if (!channel.pixels().empty() && channel.pixels().format().bit_depth != depth) {
      problems.push_back("channel '" + channel.name() + "' is " + depth_label(channel.pixels().format().bit_depth));
    }
  }
  return problems;
}

}  // namespace patchy
