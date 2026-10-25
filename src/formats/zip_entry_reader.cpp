#include "formats/zip_entry_reader.hpp"

#include "formats/miniz/miniz.h"

#include <stdexcept>

namespace patchy::formats {

struct ZipEntryReader::Impl {
  mz_zip_archive zip{};
  ZipReaderMessages messages{};
  ZipReaderLimits limits{};
  std::uint64_t extracted{0};
};

ZipEntryReader::ZipEntryReader(std::span<const std::uint8_t> bytes, const ZipReaderMessages& messages,
                               ZipReaderLimits limits)
    : impl_(std::make_unique<Impl>()) {
  impl_->messages = messages;
  impl_->limits = limits;
  if (bytes.size() < 22 || mz_zip_reader_init_mem(&impl_->zip, bytes.data(), bytes.size(), 0) == MZ_FALSE) {
    throw std::runtime_error(messages.damaged_directory);
  }
  if (mz_zip_reader_get_num_files(&impl_->zip) > limits.max_entries) {
    mz_zip_reader_end(&impl_->zip);
    throw std::runtime_error(messages.too_large);
  }
}

ZipEntryReader::~ZipEntryReader() {
  mz_zip_reader_end(&impl_->zip);
}

bool ZipEntryReader::contains(const std::string& name) {
  return mz_zip_reader_locate_file(&impl_->zip, name.c_str(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE) >= 0;
}

std::string ZipEntryReader::first_entry_name() {
  if (mz_zip_reader_get_num_files(&impl_->zip) == 0) {
    return {};
  }
  mz_zip_archive_file_stat stat{};
  if (mz_zip_reader_file_stat(&impl_->zip, 0, &stat) == MZ_FALSE) {
    return {};
  }
  return stat.m_filename;
}

std::vector<std::string> ZipEntryReader::entry_names() {
  std::vector<std::string> names;
  const auto count = mz_zip_reader_get_num_files(&impl_->zip);
  names.reserve(count);
  for (mz_uint index = 0; index < count; ++index) {
    mz_zip_archive_file_stat stat{};
    if (mz_zip_reader_file_stat(&impl_->zip, index, &stat) == MZ_TRUE && !stat.m_is_directory) {
      names.emplace_back(stat.m_filename);
    }
  }
  return names;
}

std::optional<std::vector<std::uint8_t>> ZipEntryReader::extract(const std::string& name, std::uint64_t limit) {
  auto& zip = impl_->zip;
  const auto& messages = impl_->messages;
  const int index = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
  if (index < 0) {
    return std::nullopt;
  }
  mz_zip_archive_file_stat stat{};
  if (mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(index), &stat) == MZ_FALSE || stat.m_is_directory) {
    return std::nullopt;
  }
  if (stat.m_is_encrypted || !stat.m_is_supported) {
    throw std::runtime_error(messages.unsupported);
  }
  // Deflate cannot expand past about 1032:1 and a stored entry not at all: a larger claim
  // is a damaged or hostile directory, refused before allocating for it.
  if ((stat.m_method == 0 && stat.m_uncomp_size != stat.m_comp_size) ||
      stat.m_uncomp_size > stat.m_comp_size * 1032U + 4096U) {
    throw std::runtime_error(messages.damaged_entry);
  }
  if (stat.m_uncomp_size > limit || stat.m_uncomp_size > impl_->limits.max_entry_bytes ||
      impl_->extracted + stat.m_uncomp_size > impl_->limits.max_total_extracted) {
    throw std::runtime_error(messages.too_large);
  }
  std::vector<std::uint8_t> data(static_cast<std::size_t>(stat.m_uncomp_size));
  if (!data.empty() &&
      mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(index), data.data(), data.size(), 0) == MZ_FALSE) {
    throw std::runtime_error(messages.damaged_entry);
  }
  impl_->extracted += stat.m_uncomp_size;
  return data;
}

bool safe_zip_entry_path(std::string_view path) {
  if (path.empty() || path.front() == '/' || path.find('\\') != std::string_view::npos ||
      path.find(':') != std::string_view::npos) {
    return false;
  }
  std::size_t start = 0;
  while (start <= path.size()) {
    const auto end = path.find('/', start);
    const auto piece = path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (piece == "..") {
      return false;
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return true;
}

}  // namespace patchy::formats
