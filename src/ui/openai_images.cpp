#include "ui/openai_images.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QThread>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>
#elif defined(Q_OS_MACOS)
#include <Security/Security.h>
#endif

namespace patchy::ui::openai {
namespace {

// Messages here are shown in the dialog, so they use its translation context, spelled as a
// literal at each call so lupdate extracts them (docs/localization.md).

constexpr int kReferenceMaxEdge = 2048;
constexpr int kTransferTimeoutMs = 6 * 60 * 1000;  // high quality at large sizes can take minutes

std::function<std::unique_ptr<KeyStore>()>& key_store_factory() {
  static std::function<std::unique_ptr<KeyStore>()> factory;
  return factory;
}

#if defined(Q_OS_WIN)
// Persisted identifier: the Credential Manager entry a remembered key lives in.
constexpr wchar_t kCredentialTarget[] = L"com.rtsoft.patchy.openai";
constexpr wchar_t kCredentialUser[] = L"api-key";

class WindowsKeyStore final : public KeyStore {
 public:
  [[nodiscard]] bool available() const override { return true; }
  [[nodiscard]] QString load() const override {
    PCREDENTIALW credential = nullptr;
    if (CredReadW(kCredentialTarget, CRED_TYPE_GENERIC, 0, &credential) == FALSE || credential == nullptr) {
      return {};
    }
    const auto key = QString::fromUtf8(reinterpret_cast<const char*>(credential->CredentialBlob),
                                       static_cast<qsizetype>(credential->CredentialBlobSize));
    CredFree(credential);
    return key.trimmed();
  }
  bool save(const QString& key) override {
    QByteArray blob = key.trimmed().toUtf8();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(kCredentialTarget);
    credential.UserName = const_cast<LPWSTR>(kCredentialUser);
    credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    const bool saved = CredWriteW(&credential, 0) != FALSE;
    std::fill(blob.begin(), blob.end(), '\0');
    return saved;
  }
  bool clear() override {
    return CredDeleteW(kCredentialTarget, CRED_TYPE_GENERIC, 0) != FALSE || GetLastError() == ERROR_NOT_FOUND;
  }
};
#elif defined(Q_OS_MACOS)
// Persisted identifiers: the keychain generic item a remembered key lives in.
CFMutableDictionaryRef keychain_query() {
  auto* query = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query, kSecAttrService, CFSTR("com.rtsoft.patchy.openai"));
  CFDictionarySetValue(query, kSecAttrAccount, CFSTR("api-key"));
  return query;
}

class MacKeyStore final : public KeyStore {
 public:
  [[nodiscard]] bool available() const override { return true; }
  [[nodiscard]] QString load() const override {
    auto* query = keychain_query();
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    const auto status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    if (status != errSecSuccess || result == nullptr) {
      return {};
    }
    const auto data = static_cast<CFDataRef>(result);
    const auto key = QString::fromUtf8(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                                       static_cast<qsizetype>(CFDataGetLength(data)));
    CFRelease(result);
    return key.trimmed();
  }
  bool save(const QString& key) override {
    (void)clear();
    const auto bytes = key.trimmed().toUtf8();
    auto* query = keychain_query();
    auto* data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(bytes.constData()), bytes.size());
    CFDictionarySetValue(query, kSecValueData, data);
    const auto status = SecItemAdd(query, nullptr);
    CFRelease(data);
    CFRelease(query);
    return status == errSecSuccess;
  }
  bool clear() override {
    auto* query = keychain_query();
    const auto status = SecItemDelete(query);
    CFRelease(query);
    return status == errSecSuccess || status == errSecItemNotFound;
  }
};
#endif

// A plain file only the user's account can open (Linux and the Flatpak, and tests).
class FileKeyStore final : public KeyStore {
 public:
  explicit FileKeyStore(QString path) : path_(std::move(path)) {}
  [[nodiscard]] bool available() const override { return !path_.isEmpty(); }
  [[nodiscard]] bool unencrypted_file() const override { return true; }
  [[nodiscard]] QString load() const override {
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) {
      return {};
    }
    return QString::fromUtf8(file.read(4096)).trimmed();
  }
  bool save(const QString& key) override {
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) {
      return false;
    }
    constexpr auto kOwnerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) {
      return false;
    }
    // Owner-only before any byte of the key is written, so it is never readable by others.
    (void)file.setPermissions(kOwnerOnly);
    if (file.write(key.trimmed().toUtf8()) < 0 || !file.commit()) {
      return false;
    }
    return QFile::setPermissions(path_, kOwnerOnly);
  }
  bool clear() override { return !QFile::exists(path_) || QFile::remove(path_); }

 private:
  QString path_;
};

QByteArray form_field(const QByteArray& boundary, const char* name, const QByteArray& value) {
  QByteArray part;
  part += "--" + boundary + "\r\n";
  part += "Content-Disposition: form-data; name=\"" + QByteArray(name) + "\"\r\n\r\n";
  part += value + "\r\n";
  return part;
}

}  // namespace

QString default_model() { return QStringLiteral("gpt-image-2.6-sunburst"); }

QUrl default_base_url() { return QUrl(QStringLiteral("https://api.openai.com/v1")); }

QSize fit_request_size(QSize canvas) { return fit_request_size(canvas, kMatchCanvasMinPixels, kMatchCanvasMaxPixels); }

QSize fit_request_size(QSize canvas, std::int64_t min_pixels, std::int64_t max_pixels) {
  if (canvas.width() <= 0 || canvas.height() <= 0) {
    return {1024, 1024};
  }
  const double aspect = std::clamp(static_cast<double>(canvas.width()) / canvas.height(), 1.0 / 3.0, 3.0);
  const auto canvas_pixels = static_cast<std::int64_t>(canvas.width()) * canvas.height();
  const auto area = static_cast<double>(std::clamp(canvas_pixels, std::max(min_pixels, kMinRequestPixels),
                                                   std::min(max_pixels, kMaxRequestPixels)));
  const auto snap = [](double value) {
    return std::max(kRequestSizeStep, static_cast<int>(std::lround(value / kRequestSizeStep)) * kRequestSizeStep);
  };
  const double height = std::sqrt(area / aspect);
  int width_px = std::min(snap(height * aspect), kMaxRequestEdge);
  int height_px = std::min(snap(height), kMaxRequestEdge);
  // Rounding must not leave the 1:3..3:1 range.
  while (width_px > 3 * height_px) {
    width_px -= kRequestSizeStep;
  }
  while (height_px > 3 * width_px) {
    height_px -= kRequestSizeStep;
  }
  // Grow the shorter edge until the request meets the pixel floor (which also moves the
  // aspect toward square, so the range check above still holds).
  while (static_cast<std::int64_t>(width_px) * height_px < kMinRequestPixels) {
    if (width_px <= height_px) {
      width_px += kRequestSizeStep;
    } else {
      height_px += kRequestSizeStep;
    }
  }
  return {width_px, height_px};
}

int seam_band(QSize selection_size) {
  const int longer = std::max(selection_size.width(), selection_size.height());
  return std::clamp(static_cast<int>(std::lround(longer * 0.04)), 4, 32);
}

namespace {

// One pass of a sliding max (van Herk / Gil-Werman) over `count` values `stride` apart: the
// line is padded with `radius` zeros at both ends, so a window clipped at an edge never reads
// a block that reaches past it.
void sliding_max(const std::uint8_t* in, std::uint8_t* out, int count, int stride, int radius,
                 std::vector<std::uint8_t>& forward, std::vector<std::uint8_t>& backward) {
  const int window = 2 * radius + 1;
  const int padded = count + 2 * radius;
  const auto value_at = [&](int p) -> std::uint8_t {
    const int i = p - radius;
    return (i < 0 || i >= count) ? std::uint8_t{0} : in[static_cast<std::ptrdiff_t>(i) * stride];
  };
  forward.assign(static_cast<std::size_t>(padded), 0);
  backward.assign(static_cast<std::size_t>(padded), 0);
  for (int p = 0; p < padded; ++p) {
    forward[p] = (p % window == 0) ? value_at(p) : std::max(forward[p - 1], value_at(p));
  }
  for (int p = padded - 1; p >= 0; --p) {
    backward[p] = (p == padded - 1 || (p + 1) % window == 0) ? value_at(p) : std::max(backward[p + 1], value_at(p));
  }
  // The window of value i is padded [i, i + 2r].
  for (int i = 0; i < count; ++i) {
    out[static_cast<std::ptrdiff_t>(i) * stride] = std::max(backward[i], forward[i + 2 * radius]);
  }
}

void box_blur_pass(const std::uint8_t* in, std::uint8_t* out, int count, int stride, int radius) {
  long long sum = 0;
  const auto at = [&](int i) { return in[static_cast<std::ptrdiff_t>(std::clamp(i, 0, count - 1)) * stride]; };
  for (int i = -radius; i <= radius; ++i) {
    sum += at(i);
  }
  const int window = 2 * radius + 1;
  for (int i = 0; i < count; ++i) {
    out[static_cast<std::ptrdiff_t>(i) * stride] = static_cast<std::uint8_t>((sum + window / 2) / window);
    sum += at(i + radius + 1) - at(i - radius);
  }
}

}  // namespace

QImage dilate_coverage(const QImage& coverage, int radius) {
  auto source = coverage.convertToFormat(QImage::Format_Grayscale8);
  if (radius <= 0 || source.isNull()) {
    return source;
  }
  QImage rows(source.size(), QImage::Format_Grayscale8);
  std::vector<std::uint8_t> forward;
  std::vector<std::uint8_t> backward;
  for (int y = 0; y < source.height(); ++y) {
    sliding_max(source.constScanLine(y), rows.scanLine(y), source.width(), 1, radius, forward, backward);
  }
  QImage result(source.size(), QImage::Format_Grayscale8);
  const auto stride = static_cast<int>(rows.bytesPerLine());
  for (int x = 0; x < source.width(); ++x) {
    sliding_max(rows.constBits() + x, result.bits() + x, source.height(), stride, radius, forward, backward);
  }
  return result;
}

QImage feather_coverage(const QImage& coverage, double sigma) {
  auto plane = coverage.convertToFormat(QImage::Format_Grayscale8);
  if (sigma <= 0.0 || plane.isNull()) {
    return plane;
  }
  // Three box passes approximate a gaussian; each box of radius r has variance r(r+1)/3.
  const int radius = std::max(1, static_cast<int>(std::lround(std::sqrt(sigma * sigma + 0.25) - 0.5)));
  QImage scratch(plane.size(), QImage::Format_Grayscale8);
  for (int pass = 0; pass < 3; ++pass) {
    for (int y = 0; y < plane.height(); ++y) {
      box_blur_pass(plane.constScanLine(y), scratch.scanLine(y), plane.width(), 1, radius);
    }
    const auto stride = static_cast<int>(plane.bytesPerLine());
    for (int x = 0; x < plane.width(); ++x) {
      box_blur_pass(scratch.constBits() + x, plane.bits() + x, plane.height(), stride, radius);
    }
  }
  return plane;
}

QImage edge_coverage(const QImage& selection, int band, double amount) {
  return dilate_coverage(selection, static_cast<int>(std::lround(2.0 * band * std::clamp(amount, 0.0, 1.0))));
}

double edge_feather(int band, double amount) { return band * std::clamp(amount, 0.0, 1.0); }

QImage tone_ring(const QImage& coverage, int band) {
  const auto inner = dilate_coverage(coverage, 3 * band);
  auto ring = dilate_coverage(coverage, 3 * band + std::max(8, band));
  for (int y = 0; y < ring.height(); ++y) {
    const auto* inner_row = inner.constScanLine(y);
    auto* ring_row = ring.scanLine(y);
    for (int x = 0; x < ring.width(); ++x) {
      ring_row[x] = (ring_row[x] != 0 && inner_row[x] == 0) ? 255 : 0;
    }
  }
  return ring;
}

QImage match_tone(const QImage& result, const QImage& original, const QImage& ring) {
  auto matched = result.convertToFormat(QImage::Format_RGBA8888);
  if (original.size() != result.size() || ring.size() != result.size()) {
    return matched;
  }
  const auto source = original.convertToFormat(QImage::Format_RGBA8888);
  const auto marks = ring.convertToFormat(QImage::Format_Grayscale8);
  long long sums[3] = {0, 0, 0};
  long long count = 0;
  for (int y = 0; y < matched.height(); ++y) {
    const auto* a = source.constScanLine(y);
    const auto* b = matched.constScanLine(y);
    const auto* m = marks.constScanLine(y);
    for (int x = 0; x < matched.width(); ++x) {
      if (m[x] == 0 || a[x * 4 + 3] < 255 || b[x * 4 + 3] < 255) {
        continue;
      }
      for (int c = 0; c < 3; ++c) {
        sums[c] += static_cast<int>(a[x * 4 + c]) - static_cast<int>(b[x * 4 + c]);
      }
      ++count;
    }
  }
  if (count == 0) {
    return matched;
  }
  int offsets[3];
  for (int c = 0; c < 3; ++c) {
    offsets[c] = static_cast<int>(std::llround(static_cast<double>(sums[c]) / static_cast<double>(count)));
  }
  for (int y = 0; y < matched.height(); ++y) {
    auto* row = matched.scanLine(y);
    for (int x = 0; x < matched.width(); ++x) {
      for (int c = 0; c < 3; ++c) {
        row[x * 4 + c] = static_cast<std::uint8_t>(std::clamp(row[x * 4 + c] + offsets[c], 0, 255));
      }
    }
  }
  return matched;
}

namespace {
// The deepest a pixel of the original can lie from an edge that meets new area.
int expand_max_depth(QSize original, QMargins margins) {
  const int across = margins.left() > 0 && margins.right() > 0 ? original.width() / 2
                     : margins.left() > 0 || margins.right() > 0 ? original.width()
                                                                  : std::numeric_limits<int>::max();
  const int down = margins.top() > 0 && margins.bottom() > 0 ? original.height() / 2
                   : margins.top() > 0 || margins.bottom() > 0 ? original.height()
                                                                : std::numeric_limits<int>::max();
  const int depth = std::min(across, down);
  return depth == std::numeric_limits<int>::max() ? 0 : std::max(1, depth);
}
}  // namespace

int expand_fade_width(QSize original, QMargins margins) {
  return std::max(8, static_cast<int>(std::lround(0.06 * expand_max_depth(original, margins))));
}

QImage expand_layer_image(const QImage& image, const QImage& visible, QMargins margins, double amount) {
  const QSize canvas = visible.size();
  const QSize grown(canvas.width() + margins.left() + margins.right(), canvas.height() + margins.top() + margins.bottom());
  auto out = image.convertToFormat(QImage::Format_RGBA8888);
  if (out.size() != grown) {
    out = out.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
              .scaled(grown, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
              .convertToFormat(QImage::Format_RGBA8888);
  }
  const int solid = static_cast<int>(std::lround(std::clamp(amount, 0.0, 1.0) * expand_max_depth(canvas, margins)));
  const int fade = expand_fade_width(canvas, margins);
  const QRect original(QPoint(margins.left(), margins.top()), canvas);
  // How deep a pixel of the original lies from the nearest edge that meets new area.
  const auto depth = [&](int x, int y) {
    int d = std::numeric_limits<int>::max();
    if (margins.left() > 0) {
      d = std::min(d, x - original.left());
    }
    if (margins.right() > 0) {
      d = std::min(d, original.right() - x);
    }
    if (margins.top() > 0) {
      d = std::min(d, y - original.top());
    }
    if (margins.bottom() > 0) {
      d = std::min(d, original.bottom() - y);
    }
    return d;
  };
  // Tone: the result's mean shifted to the original's across the fade (match_tone).
  QImage context(grown, QImage::Format_RGBA8888);
  context.fill(Qt::transparent);
  QImage ring(grown, QImage::Format_Grayscale8);
  ring.fill(0);
  const auto source = visible.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < canvas.height(); ++y) {
    std::memcpy(context.scanLine(y + original.top()) + static_cast<std::ptrdiff_t>(original.left()) * 4,
                source.constScanLine(y), static_cast<std::size_t>(canvas.width()) * 4U);
    auto* marks = ring.scanLine(y + original.top());
    for (int x = 0; x < canvas.width(); ++x) {
      const int d = depth(x + original.left(), y + original.top());
      if (d >= solid && d < solid + fade) {
        marks[x + original.left()] = 255;
      }
    }
  }
  out = match_tone(out, context, ring);
  // Alpha: opaque in the new area and to `solid` px into the original, fading over `fade`,
  // clear deeper in.
  for (int y = original.top(); y <= original.bottom() && y < out.height(); ++y) {
    auto* row = out.scanLine(y);
    for (int x = original.left(); x <= original.right() && x < out.width(); ++x) {
      const int d = depth(x, y);
      const int weight = d < solid          ? 255
                         : d >= solid + fade ? 0
                                             : (255 * (solid + fade - d) + fade / 2) / fade;
      row[x * 4 + 3] = static_cast<std::uint8_t>((row[x * 4 + 3] * weight + 127) / 255);
    }
  }
  return out;
}

QRect coverage_bounds(const QImage& coverage) {
  const auto plane = coverage.convertToFormat(QImage::Format_Grayscale8);
  int left = plane.width();
  int top = plane.height();
  int right = -1;
  int bottom = -1;
  for (int y = 0; y < plane.height(); ++y) {
    const auto* row = plane.constScanLine(y);
    for (int x = 0; x < plane.width(); ++x) {
      if (row[x] != 0) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  return right < left ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

QImage blend_with_weight(const QImage& base, const QImage& top, const QImage& weight) {
  auto out = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  if (top.size() != base.size() || weight.size() != base.size()) {
    return out.convertToFormat(QImage::Format_RGBA8888);
  }
  auto layer = top.convertToFormat(QImage::Format_RGBA8888);
  const auto w = weight.convertToFormat(QImage::Format_Grayscale8);
  for (int y = 0; y < layer.height(); ++y) {
    auto* row = layer.scanLine(y);
    const auto* weights = w.constScanLine(y);
    for (int x = 0; x < layer.width(); ++x) {
      row[x * 4 + 3] = static_cast<std::uint8_t>((row[x * 4 + 3] * weights[x] + 127) / 255);
    }
  }
  {
    QPainter painter(&out);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(QPoint(0, 0), layer);
  }
  return out.convertToFormat(QImage::Format_RGBA8888);
}

QString animation_prompt(const QString& motion, int frames, bool from_image, int references) {
  QString text;
  if (from_image && references > 0) {
    text += QStringLiteral(
        "Animate the first provided image. Keep its subject, composition, colors, lighting and style exactly as they "
        "are in every frame. The other provided images are references for how things should look, not frames. ");
  } else if (from_image) {
    text += QStringLiteral(
        "Animate the provided image. Keep its subject, composition, colors, lighting and style exactly as they are "
        "in every frame. ");
  } else if (references > 0) {
    text += QStringLiteral("Use the provided images as references for how things should look. ");
  }
  text += QStringLiteral(
              "Create a single animation sprite sheet for a seamless looping GIF with exactly %1 sequential frames. "
              "Every panel has identical dimensions, a fixed camera and the same subject, background and lighting; "
              "only the motion described below changes, in small even steps, and the last frame leads smoothly back "
              "into the first. No borders, no gutters, no panel labels, no added text. Motion: ")
              .arg(std::clamp(frames, kMinGifFrames, kMaxGifFrames));
  text += motion;
  return text;
}

SheetLayout sheet_layout(int frames) {
  frames = std::clamp(frames, kMinGifFrames, kMaxGifFrames);
  SheetLayout best{frames, 1};
  int best_spare = std::numeric_limits<int>::max();
  for (int rows = 1; rows <= frames; ++rows) {
    const int columns = (frames + rows - 1) / rows;
    if (columns < rows || columns > 3 * rows) {
      continue;
    }
    const int spare = columns * rows - frames;
    // Fewer spare cells first, then the squarer grid.
    if (spare < best_spare || (spare == best_spare && columns - rows < best.columns - best.rows)) {
      best = {columns, rows};
      best_spare = spare;
    }
  }
  return best;
}

QString sheet_animation_prompt(const QString& motion, SheetLayout layout, bool from_image, int references) {
  QString text;
  if (from_image && references > 0) {
    text += QStringLiteral(
        "Animate the first provided image. Keep its subject, composition, colors, lighting and style exactly as they "
        "are in every frame. The other provided images are references for how things should look, not frames. ");
  } else if (from_image) {
    text += QStringLiteral(
        "Animate the provided image. Keep its subject, composition, colors, lighting and style exactly as they are "
        "in every frame. ");
  } else if (references > 0) {
    text += QStringLiteral("Use the provided images as references for how things should look. ");
  }
  text += QStringLiteral(
              "Create a single animation sprite sheet for a seamless looping animation with exactly %1 sequential "
              "frames, arranged in a grid of %2 columns and %3 rows, read left to right then top to bottom. The panels "
              "fill the whole image edge to edge with no gaps, all the same size. Every panel has a fixed camera and "
              "the same subject, background and lighting; only the motion described below changes, in small even "
              "steps, and the last frame leads smoothly back into the first. No borders, no gutters, no panel labels, "
              "no added text. Motion: ")
              .arg(layout.frames())
              .arg(layout.columns)
              .arg(layout.rows);
  text += motion;
  return text;
}

QSize sheet_request_size(SheetLayout layout, int edge) {
  const QSize grid(layout.columns * edge, layout.rows * edge);
  if (static_cast<std::int64_t>(grid.width()) * grid.height() >= kMinRequestPixels) {
    return grid;
  }
  return fit_request_size(grid, kMinRequestPixels, kMaxRequestPixels);
}

std::vector<GifFrame> sheet_frames(const QImage& sheet, SheetLayout layout, int delay_ms, QSize frame) {
  std::vector<GifFrame> frames;
  if (sheet.isNull() || layout.columns <= 0 || layout.rows <= 0) {
    return frames;
  }
  const auto source = sheet.convertToFormat(QImage::Format_RGBA8888);
  const int width = source.width() / layout.columns;
  const int height = source.height() / layout.rows;
  if (width <= 0 || height <= 0) {
    return frames;
  }
  for (int row = 0; row < layout.rows; ++row) {
    for (int column = 0; column < layout.columns; ++column) {
      auto panel = source.copy(column * width, row * height, width, height);
      if (frame.isValid() && panel.size() != frame) {
        panel = panel.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                    .scaled(frame, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_RGBA8888);
      }
      frames.push_back({std::move(panel), delay_ms});
    }
  }
  return frames;
}

QRect context_region(QRect selection_bounds, QSize canvas, bool tight) {
  const QRect canvas_rect(QPoint(0, 0), canvas);
  const auto bounds = selection_bounds.intersected(canvas_rect);
  if (bounds.isEmpty()) {
    return {};
  }
  const auto canvas_pixels = static_cast<std::int64_t>(canvas.width()) * canvas.height();
  if (!tight && canvas_pixels <= kMaskedMaxPixels) {
    return canvas_rect;
  }
  const int longer = std::max(bounds.width(), bounds.height());
  const int margin = tight ? std::max(160, longer)
                           : std::max({32, longer, std::max(canvas.width(), canvas.height()) / 4});
  auto region = bounds.adjusted(-margin, -margin, margin, margin).intersected(canvas_rect);
  if (2 * static_cast<std::int64_t>(region.width()) * region.height() >= canvas_pixels) {
    return canvas_rect;
  }
  // Grow the short side toward 1:3, centered, sliding back inside the canvas at its edges.
  const auto grow = [&](bool vertical) {
    const int length = vertical ? region.height() : region.width();
    const int wanted = ((vertical ? region.width() : region.height()) + 2) / 3;
    const int limit = vertical ? canvas.height() : canvas.width();
    const int target = std::min(wanted, limit);
    if (target <= length) {
      return;
    }
    const int start = vertical ? region.top() : region.left();
    int new_start = start - (target - length) / 2;
    new_start = std::clamp(new_start, 0, limit - target);
    if (vertical) {
      region.setTop(new_start);
      region.setHeight(target);
    } else {
      region.setLeft(new_start);
      region.setWidth(target);
    }
  };
  if (region.width() > 3 * region.height()) {
    grow(true);
  } else if (region.height() > 3 * region.width()) {
    grow(false);
  }
  return region;
}

ExpandFrame expand_frame(const QImage& visible, QMargins margins) {
  ExpandFrame frame;
  if (visible.isNull() || margins.left() < 0 || margins.top() < 0 || margins.right() < 0 || margins.bottom() < 0) {
    return frame;
  }
  const double grown_width = visible.width() + margins.left() + margins.right();
  const double grown_height = visible.height() + margins.top() + margins.bottom();
  const double scale = std::min(1.0, std::sqrt(static_cast<double>(kExpandFramePixels) / (grown_width * grown_height)));
  const QSize size(std::max(1, static_cast<int>(std::lround(grown_width * scale))),
                   std::max(1, static_cast<int>(std::lround(grown_height * scale))));
  // The original's place in the scaled frame, rounded inward so the new area is never
  // reported as original.
  const QRect original(static_cast<int>(std::ceil(margins.left() * scale)),
                       static_cast<int>(std::ceil(margins.top() * scale)),
                       std::max(0, static_cast<int>(std::floor(visible.width() * scale)) - 1),
                       std::max(0, static_cast<int>(std::floor(visible.height() * scale)) - 1));
  frame.context = QImage(size, QImage::Format_RGBA8888);
  frame.context.fill(Qt::transparent);
  {
    QPainter painter(&frame.context);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRectF(margins.left() * scale, margins.top() * scale, visible.width() * scale,
                             visible.height() * scale),
                      visible.convertToFormat(QImage::Format_RGBA8888_Premultiplied));
  }
  frame.coverage = QImage(size, QImage::Format_Grayscale8);
  frame.coverage.fill(255);
  for (int y = original.top(); y <= original.bottom() && y < size.height(); ++y) {
    std::memset(frame.coverage.scanLine(y) + original.left(), 0,
                static_cast<std::size_t>(std::min(original.width(), size.width() - original.left())));
  }
  return frame;
}

int markup_edge_pad(QSize context) {
  return std::max(8, static_cast<int>(std::lround(0.02 * std::max(context.width(), context.height()))));
}

bool coverage_is_full(const QImage& coverage) {
  const auto plane = coverage.convertToFormat(QImage::Format_Grayscale8);
  if (plane.isNull()) {
    return false;
  }
  for (int y = 0; y < plane.height(); ++y) {
    const auto* row = plane.constScanLine(y);
    for (int x = 0; x < plane.width(); ++x) {
      if (row[x] != 255) {
        return false;
      }
    }
  }
  return true;
}

MaskedEditImages masked_edit_images(const QImage& context, const QImage& coverage, int edge_pad) {
  MaskedEditImages sent;
  const int width = context.width();
  const int height = context.height();
  if (width <= 0 || height <= 0 || coverage.size() != context.size()) {
    return sent;
  }
  // Padding on the sides the selection reaches, for Edit's ring (see the header).
  QMargins pad;
  if (edge_pad > 0) {
    const auto plane = coverage.convertToFormat(QImage::Format_Grayscale8);
    bool left = false;
    bool right = false;
    bool top = false;
    bool bottom = false;
    for (int y = 0; y < height; ++y) {
      left = left || plane.constScanLine(y)[0] != 0;
      right = right || plane.constScanLine(y)[width - 1] != 0;
    }
    for (int x = 0; x < width; ++x) {
      top = top || plane.constScanLine(0)[x] != 0;
      bottom = bottom || plane.constScanLine(height - 1)[x] != 0;
    }
    pad = QMargins(left ? edge_pad : 0, top ? edge_pad : 0, right ? edge_pad : 0, bottom ? edge_pad : 0);
  }
  const int inner_width = width + pad.left() + pad.right();
  const int inner_height = height + pad.top() + pad.bottom();
  const int padded_width = std::max(inner_width, (inner_height + 2) / 3);
  const int padded_height = std::max(inner_height, (inner_width + 2) / 3);
  sent.padded_size = QSize(padded_width, padded_height);
  sent.content_rect = QRect((padded_width - inner_width) / 2 + pad.left(), (padded_height - inner_height) / 2 + pad.top(),
                            width, height);
  QImage padded(sent.padded_size, QImage::Format_RGBA8888);
  padded.fill(Qt::transparent);
  QImage mask(sent.padded_size, QImage::Format_RGBA8888);
  mask.fill(Qt::black);  // color-literal-ok: API mask data, opaque means keep
  const auto source = context.convertToFormat(QImage::Format_RGBA8888);
  const auto cover = coverage.convertToFormat(QImage::Format_Grayscale8);
  for (int y = 0; y < height; ++y) {
    std::memcpy(padded.scanLine(y + sent.content_rect.y()) + sent.content_rect.x() * 4, source.constScanLine(y),
                static_cast<std::size_t>(width) * 4U);
    const auto* cover_row = cover.constScanLine(y);
    auto* mask_row = mask.scanLine(y + sent.content_rect.y()) + sent.content_rect.x() * 4;
    for (int x = 0; x < width; ++x) {
      if (cover_row[x] != 0) {
        mask_row[x * 4 + 3] = 0;  // transparent: edit
      }
    }
  }
  const auto request = fit_request_size(sent.padded_size, kMatchCanvasMinPixels, kMaskedMaxPixels);
  sent.image = padded.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                   .scaled(request, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                   .convertToFormat(QImage::Format_RGBA8888);
  // Smooth scaling softens the mask's edge; any pixel the selection reaches stays editable.
  auto scaled_mask = mask.scaled(request, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < scaled_mask.height(); ++y) {
    auto* row = scaled_mask.scanLine(y);
    for (int x = 0; x < scaled_mask.width(); ++x) {
      row[x * 4 + 0] = 0;
      row[x * 4 + 1] = 0;
      row[x * 4 + 2] = 0;
      row[x * 4 + 3] = row[x * 4 + 3] == 255 ? 255 : 0;
    }
  }
  sent.mask = scaled_mask;
  return sent;
}

QImage markup_overlay(const QImage& coverage, const MaskedEditImages& sent) {
  if (sent.image.isNull() || sent.padded_size.isEmpty() || coverage.size() != sent.content_rect.size()) {
    return {};
  }
  // The selection at the request size (any part of a pixel counts), so the ring is drawn
  // solid at the resolution the model sees: about 1/250 of the longer edge wide, just outside.
  QImage padded(sent.padded_size, QImage::Format_Grayscale8);
  padded.fill(0);
  const auto cover = coverage.convertToFormat(QImage::Format_Grayscale8);
  for (int y = 0; y < cover.height(); ++y) {
    std::memcpy(padded.scanLine(y + sent.content_rect.y()) + sent.content_rect.x(), cover.constScanLine(y),
                static_cast<std::size_t>(cover.width()));
  }
  auto scaled = padded.scaled(sent.image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  for (int y = 0; y < scaled.height(); ++y) {
    auto* row = scaled.scanLine(y);
    for (int x = 0; x < scaled.width(); ++x) {
      row[x] = row[x] != 0 ? 255 : 0;
    }
  }
  const int width = std::max(4, std::max(sent.image.width(), sent.image.height()) / 250);
  const int gap = std::max(2, width / 2);
  const auto inner = dilate_coverage(scaled, gap);
  const auto outer = dilate_coverage(scaled, gap + width);
  QImage overlay(sent.image.size(), QImage::Format_RGBA8888);
  overlay.fill(Qt::transparent);
  for (int y = 0; y < overlay.height(); ++y) {
    const auto* inner_row = inner.constScanLine(y);
    const auto* outer_row = outer.constScanLine(y);
    auto* row = overlay.scanLine(y);
    for (int x = 0; x < overlay.width(); ++x) {
      if (outer_row[x] != 0 && inner_row[x] == 0) {
        row[x * 4 + 0] = 255;  // color-literal-ok: the markup's red outline (API data)
        row[x * 4 + 3] = 255;
      }
    }
  }
  return overlay;
}

QImage result_for_region(const QImage& result, const MaskedEditImages& sent) {
  if (result.isNull() || sent.padded_size.isEmpty()) {
    return {};
  }
  const auto full = result.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                        .scaled(sent.padded_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_RGBA8888);
  return full.copy(sent.content_rect);
}

QString size_parameter(QSize size) {
  if (size.isEmpty()) {
    return QStringLiteral("auto");
  }
  return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
}

QJsonObject describe_request_json(const QImage& image, const QString& model) {
  auto sent = image.convertToFormat(QImage::Format_RGBA8888);
  if (std::max(sent.width(), sent.height()) > kDescribeMaxEdge) {
    sent = sent.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
               .scaled(kDescribeMaxEdge, kDescribeMaxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation)
               .convertToFormat(QImage::Format_RGBA8888);
  }
  const auto url = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png_bytes(sent).toBase64());
  const QJsonArray content{
      QJsonObject{{QStringLiteral("type"), QStringLiteral("input_text")},
                  {QStringLiteral("text"), QString::fromLatin1(kDescribeInstructions)}},
      QJsonObject{{QStringLiteral("type"), QStringLiteral("input_image")},
                  {QStringLiteral("image_url"), url},
                  {QStringLiteral("detail"), QStringLiteral("high")}}};
  return QJsonObject{
      {QStringLiteral("model"), model},
      {QStringLiteral("store"), false},
      {QStringLiteral("input"),
       QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}}}}};
}

DescribeResult parse_describe_response(const QByteArray& body, QString* error) {
  DescribeResult result;
  const auto document = QJsonDocument::fromJson(body);
  if (!document.isObject()) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "OpenAI sent a reply Patchy could not read.");
    }
    return result;
  }
  const auto root = document.object();
  QStringList parts;
  QString refusal;
  for (const auto& entry : root.value(QStringLiteral("output")).toArray()) {
    const auto item = entry.toObject();
    if (item.value(QStringLiteral("type")).toString() != QLatin1String("message")) {
      continue;
    }
    for (const auto& part_value : item.value(QStringLiteral("content")).toArray()) {
      const auto part = part_value.toObject();
      const auto type = part.value(QStringLiteral("type")).toString();
      if (type == QLatin1String("output_text")) {
        parts.push_back(part.value(QStringLiteral("text")).toString().trimmed());
      } else if (type == QLatin1String("refusal")) {
        refusal = part.value(QStringLiteral("refusal")).toString().trimmed();
      }
    }
  }
  result.text = parts.join(QStringLiteral("\n\n")).trimmed();
  result.total_tokens = root.value(QStringLiteral("usage")).toObject().value(QStringLiteral("total_tokens")).toInteger();
  if (result.text.isEmpty() && error != nullptr) {
    *error = refusal.isEmpty()
                 ? QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "OpenAI sent no description.")
                 : QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "OpenAI declined to describe the image: %1")
                       .arg(refusal);
  }
  return result;
}

QJsonObject generation_request_json(const GenerateRequest& request) {
  QJsonObject body;
  body.insert(QStringLiteral("model"), request.model);
  body.insert(QStringLiteral("prompt"), request.prompt);
  body.insert(QStringLiteral("n"), std::clamp(request.count, 1, kMaxImagesPerRequest));
  body.insert(QStringLiteral("size"), size_parameter(request.size));
  body.insert(QStringLiteral("quality"), request.quality);
  body.insert(QStringLiteral("background"), request.background);
  body.insert(QStringLiteral("moderation"), QString::fromLatin1(kModeration));
  if (request.animated_gif) {
    body.insert(QStringLiteral("output_format"), QStringLiteral("gif"));
    body.insert(QStringLiteral("frame_count"), std::clamp(request.frame_count, kMinGifFrames, kMaxGifFrames));
    body.insert(QStringLiteral("frame_duration_ms"),
                std::clamp(request.frame_duration_ms, kMinGifFrameMs, kMaxGifFrameMs));
  } else {
    body.insert(QStringLiteral("output_format"), QStringLiteral("png"));
  }
  return body;
}

QByteArray edits_multipart_body(const GenerateRequest& request, const QByteArray& boundary) {
  QByteArray body;
  body += form_field(boundary, "model", request.model.toUtf8());
  body += form_field(boundary, "prompt", request.prompt.toUtf8());
  body += form_field(boundary, "n", QByteArray::number(std::clamp(request.count, 1, kMaxImagesPerRequest)));
  body += form_field(boundary, "size", size_parameter(request.size).toUtf8());
  body += form_field(boundary, "quality", request.quality.toUtf8());
  body += form_field(boundary, "background", request.background.toUtf8());
  body += form_field(boundary, "moderation", QByteArray(kModeration));
  if (!request.complexity.isEmpty()) {
    body += form_field(boundary, "complexity", request.complexity.toUtf8());
  }
  if (request.animated_gif) {
    // GIF edits take no mask: an animation starts from the reference images only.
    Q_ASSERT(request.edit_image.isNull());
    body += form_field(boundary, "output_format", QByteArrayLiteral("gif"));
    body += form_field(boundary, "frame_count",
                       QByteArray::number(std::clamp(request.frame_count, kMinGifFrames, kMaxGifFrames)));
    body += form_field(boundary, "frame_duration_ms",
                       QByteArray::number(std::clamp(request.frame_duration_ms, kMinGifFrameMs, kMaxGifFrameMs)));
  } else {
    body += form_field(boundary, "output_format", QByteArrayLiteral("png"));
  }
  if (!request.edit_image.isNull() && !request.animated_gif) {
    // A selection edit (Edit, Remove): the edit image and its markup or its mask (the API
    // refuses both together). Reference images follow the edit image only with markup (Edit,
    // Seth's decision 2026-11-06), never with a mask (docs/legal-constraints.md,
    // "Prompt-driven image generation").
    Q_ASSERT(request.edit_markup.isNull() || request.edit_mask.isNull());
    Q_ASSERT(request.references.empty() || request.edit_mask.isNull());
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"image[]\"; filename=\"image.png\"\r\n";
    body += "Content-Type: image/png\r\n\r\n";
    body += png_bytes(request.edit_image);
    body += "\r\n";
    if (request.edit_mask.isNull()) {
      const auto extra = std::min<std::size_t>(request.references.size(), kMaxReferenceImages - 1);
      for (std::size_t index = 0; index < extra; ++index) {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"image[]\"; filename=\"reference-" +
                QByteArray::number(static_cast<qulonglong>(index + 1)) + ".png\"\r\n";
        body += "Content-Type: image/png\r\n\r\n";
        body += png_bytes(request.references[index]);
        body += "\r\n";
      }
    }
    // Expand sends neither: the frame alone, its new area transparent.
    if (!request.edit_markup.isNull() || !request.edit_mask.isNull()) {
      const bool markup = !request.edit_markup.isNull();
      body += "--" + boundary + "\r\n";
      body += markup ? "Content-Disposition: form-data; name=\"markup\"; filename=\"markup.png\"\r\n"
                     : "Content-Disposition: form-data; name=\"mask\"; filename=\"mask.png\"\r\n";
      body += "Content-Type: image/png\r\n\r\n";
      body += png_bytes(markup ? request.edit_markup : request.edit_mask);
      body += "\r\n";
    }
    body += "--" + boundary + "--\r\n";
    return body;
  }
  // References: no "mask" part goes with them (an open decision in docs/legal-constraints.md).
  const auto count = std::min<std::size_t>(request.references.size(), kMaxReferenceImages);
  for (std::size_t index = 0; index < count; ++index) {
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"image[]\"; filename=\"reference-" +
            QByteArray::number(static_cast<qulonglong>(index + 1)) + ".png\"\r\n";
    body += "Content-Type: image/png\r\n\r\n";
    body += png_bytes(request.references[index]);
    body += "\r\n";
  }
  body += "--" + boundary + "--\r\n";
  return body;
}

GenerateResult parse_images_response(const QByteArray& body, QString* error) {
  GenerateResult result;
  QJsonParseError parse_error{};
  const auto document = QJsonDocument::fromJson(body, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "OpenAI sent a reply Patchy could not read.");
    }
    return result;
  }
  const auto root = document.object();
  for (const auto& entry : root.value(QStringLiteral("data")).toArray()) {
    const auto encoded = entry.toObject().value(QStringLiteral("b64_json")).toString().toLatin1();
    if (encoded.isEmpty()) {
      continue;
    }
    auto bytes = QByteArray::fromBase64(encoded);
    auto image = QImage::fromData(bytes);
    if (image.isNull()) {
      continue;
    }
    result.images.push_back(image.convertToFormat(QImage::Format_RGBA8888));
    result.encoded.push_back(std::move(bytes));
  }
  const auto usage = root.value(QStringLiteral("usage")).toObject();
  result.input_tokens = usage.value(QStringLiteral("input_tokens")).toInteger();
  result.output_tokens = usage.value(QStringLiteral("output_tokens")).toInteger();
  result.total_tokens = usage.value(QStringLiteral("total_tokens")).toInteger();
  if (result.images.empty() && error != nullptr) {
    *error = QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "OpenAI's reply held no image.");
  }
  return result;
}

namespace {

struct ApiError {
  QString message;
  QString code;
  QString type;
};

ApiError parse_api_error(const QByteArray& body) {
  const auto error = QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toObject();
  return {error.value(QStringLiteral("message")).toString().trimmed(), error.value(QStringLiteral("code")).toString(),
          error.value(QStringLiteral("type")).toString()};
}

bool out_of_credit(const ApiError& error) {
  return error.code == QLatin1String("insufficient_quota") || error.type == QLatin1String("insufficient_quota");
}

bool needs_verification(const ApiError& error, int http_status) {
  return http_status == 403 && error.message.contains(QStringLiteral("verif"), Qt::CaseInsensitive);
}

}  // namespace

QString api_error_message(const QByteArray& body, int http_status) {
  const auto error = parse_api_error(body);
  if (http_status == 401 || error.code == QLatin1String("invalid_api_key")) {
    return QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "OpenAI did not accept this API key. Check that it was copied whole, or create a new one.");
  }
  if (out_of_credit(error)) {
    return QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "Your OpenAI account is out of credit, so nothing was generated. Add credit to keep going.");
  }
  if (error.code == QLatin1String("billing_hard_limit_reached")) {
    return QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "Your OpenAI account has reached its spending limit, so nothing was generated. Raise the limit "
                "to keep going.");
  }
  if (http_status == 429) {
    return QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "OpenAI is limiting requests from this key right now. Wait a moment and try again.");
  }
  if (error.code == QLatin1String("moderation_blocked") ||
      error.message.contains(QStringLiteral("safety system"), Qt::CaseInsensitive)) {
    return QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "OpenAI's safety system declined this request. Try rewording the prompt or using other images.");
  }
  if (needs_verification(error, http_status)) {
    return QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "This model needs a verified OpenAI organization. Verify it, then try again.");
  }
  if (http_status == 404 || error.code == QLatin1String("model_not_found")) {
    return QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "This API key cannot use the chosen model. Pick the other model, or check the key's project "
                "permissions.");
  }
  if (http_status >= 500) {
    return QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "OpenAI had a problem on its side (HTTP %1). Try again in a moment.").arg(http_status);
  }
  if (!error.message.isEmpty()) {
    return error.message;
  }
  return QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "OpenAI returned HTTP status %1.").arg(http_status);
}

ErrorHelp api_error_help(const QByteArray& body, int http_status) {
  const auto error = parse_api_error(body);
  if (http_status == 401 || error.code == QLatin1String("invalid_api_key")) {
    return {QStringLiteral("https://platform.openai.com/api-keys"), QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "Manage API keys")};
  }
  if (out_of_credit(error)) {
    return {QStringLiteral("https://platform.openai.com/settings/organization/billing/overview"), QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "Add credit")};
  }
  if (error.code == QLatin1String("billing_hard_limit_reached")) {
    return {QStringLiteral("https://platform.openai.com/settings/organization/limits"), QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "Open spending limits")};
  }
  if (needs_verification(error, http_status)) {
    return {QStringLiteral("https://platform.openai.com/settings/organization/general"), QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "Verify organization")};
  }
  return {};
}

QStringList image_model_ids(const QByteArray& models_body, QString* error) {
  QStringList ids;
  const auto document = QJsonDocument::fromJson(models_body);
  if (!document.isObject()) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "OpenAI sent a reply Patchy could not read.");
    }
    return ids;
  }
  for (const auto& entry : document.object().value(QStringLiteral("data")).toArray()) {
    const auto id = entry.toObject().value(QStringLiteral("id")).toString();
    if (id.startsWith(QStringLiteral("gpt-image")) || id.startsWith(QStringLiteral("chatgpt-image"))) {
      ids.push_back(id);
    }
  }
  std::sort(ids.begin(), ids.end(), [](const QString& a, const QString& b) { return a > b; });
  return ids;
}

std::vector<GifFrame> gif_frames(const QByteArray& gif, QString* error) {
  std::vector<GifFrame> frames;
  QBuffer buffer;
  buffer.setData(gif);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer, "gif");
  // qgif returns fully composited full-canvas frames; nextImageDelay() is the frame just read
  // (the GIF import's own reading, main_window_files.cpp).
  while (true) {
    auto frame = reader.read();
    if (frame.isNull()) {
      break;
    }
    frames.push_back({frame.convertToFormat(QImage::Format_RGBA8888), reader.nextImageDelay()});
    if (!reader.canRead()) {
      break;
    }
  }
  if (frames.size() < 2 && error != nullptr) {
    *error = QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "OpenAI's reply held no animation.");
  }
  if (frames.size() < 2) {
    frames.clear();
  }
  return frames;
}

QByteArray png_bytes(const QImage& image) {
  // A fresh image from the pixel rows only, so no text chunk or other metadata from the
  // source travels with the request.
  const auto source = image.convertToFormat(QImage::Format_RGBA8888);
  QImage clean(source.width(), source.height(), QImage::Format_RGBA8888);
  for (int y = 0; y < source.height(); ++y) {
    std::memcpy(clean.scanLine(y), source.constScanLine(y), static_cast<std::size_t>(source.width()) * 4U);
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  QImageWriter writer(&buffer, "png");
  (void)writer.write(clean);
  return bytes;
}

QImage placed_image(const QImage& image, QSize canvas, bool fit_to_canvas, QPoint* origin) {
  auto source = image.convertToFormat(QImage::Format_RGBA8888);
  if (fit_to_canvas && canvas.isValid() && source.size() != canvas) {
    source = source.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                 .scaled(canvas, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                 .convertToFormat(QImage::Format_RGBA8888);
  }
  if (origin != nullptr) {
    *origin = QPoint(static_cast<int>(std::floor((canvas.width() - source.width()) / 2.0)),
                     static_cast<int>(std::floor((canvas.height() - source.height()) / 2.0)));
  }
  return source;
}

QImage prepare_reference_image(const QImage& image) {
  if (image.isNull()) {
    return {};
  }
  auto prepared = image.convertToFormat(QImage::Format_RGBA8888);
  if (std::max(prepared.width(), prepared.height()) > kReferenceMaxEdge) {
    prepared = prepared.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                   .scaled(kReferenceMaxEdge, kReferenceMaxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                   .convertToFormat(QImage::Format_RGBA8888);
  }
  return prepared;
}

QImage load_reference_image_file(const QString& path, QString* error) {
  QImageReader reader(path);
  reader.setAutoTransform(true);
  const auto image = reader.read();
  if (image.isNull()) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "Could not read %1: %2").arg(QFileInfo(path).fileName(), reader.errorString());
    }
    return {};
  }
  return prepare_reference_image(image);
}

std::unique_ptr<KeyStore> make_system_key_store() {
#if defined(Q_OS_WIN)
  return std::make_unique<WindowsKeyStore>();
#elif defined(Q_OS_MACOS)
  return std::make_unique<MacKeyStore>();
#else
  return std::make_unique<FileKeyStore>(key_file_path());
#endif
}

QString key_file_path() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/openai-api-key");
}

std::unique_ptr<KeyStore> make_file_key_store(const QString& path) { return std::make_unique<FileKeyStore>(path); }

void set_key_store_factory_for_testing(std::function<std::unique_ptr<KeyStore>()> factory) {
  key_store_factory() = std::move(factory);
}

std::unique_ptr<KeyStore> make_key_store() {
  if (key_store_factory()) {
    return key_store_factory()();
  }
  return make_system_key_store();
}

QString environment_api_key() {
  return QProcessEnvironment::systemEnvironment().value(QStringLiteral("OPENAI_API_KEY")).trimmed();
}

Client::Client(QObject* owner) : owner_(owner), manager_(new QNetworkAccessManager(owner)), base_url_(default_base_url()) {
  const auto override_url = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATCHY_OPENAI_BASE_URL"));
  if (!override_url.isEmpty()) {
    base_url_ = QUrl(override_url);
  }
}

Client::~Client() { cancel(); }

bool Client::busy() const { return preparing_ || !reply_.isNull(); }

QNetworkRequest Client::make_request(const QString& path) const {
  QUrl url = base_url_;
  auto base_path = url.path();
  while (base_path.endsWith(QLatin1Char('/'))) {
    base_path.chop(1);
  }
  url.setPath(base_path + path);
  QNetworkRequest request(url);
  request.setRawHeader("Authorization", "Bearer " + api_key_.toUtf8());
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  request.setTransferTimeout(kTransferTimeoutMs);
  return request;
}

void Client::watch(QNetworkReply* reply, std::function<void(const QByteArray&)> done,
                   std::function<void(QString)> failed) {
  reply_ = reply;
  last_help_ = {};
  QObject::connect(reply, &QNetworkReply::finished, owner_,
                   [this, reply, done = std::move(done), failed = std::move(failed)] {
                     reply->deleteLater();
                     if (reply->property("patchyCancelled").toBool()) {
                       return;
                     }
                     if (reply_ == reply) {
                       reply_ = nullptr;
                     }
                     const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                     const auto body = reply->readAll();
                     if (status >= 200 && status < 300 && reply->error() == QNetworkReply::NoError) {
                       done(body);
                     } else if (status > 0) {
                       last_help_ = api_error_help(body, status);
                       failed(api_error_message(body, status));
                     } else if (reply->error() == QNetworkReply::OperationCanceledError) {
                       failed(QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "OpenAI did not answer in time."));
                     } else {
                       failed(QCoreApplication::translate("patchy::ui::OpenAiImagesDialog", "Could not reach OpenAI: %1").arg(reply->errorString()));
                     }
                   });
}

void Client::post_prepared(const QString& path, const QByteArray& content_type, std::function<QByteArray()> build_body,
                           std::function<void(const QByteArray&)> done, std::function<void(QString)> failed) {
  cancel();
  last_path_ = path;
  preparing_ = true;
  const auto generation = ++generation_;
  auto body = std::make_shared<QByteArray>();
  // The worker owns copies of everything it reads; it deletes itself when done, whatever
  // happened to the request or the dialog meanwhile.
  auto* worker = QThread::create([body, build = std::move(build_body)] { *body = build(); });
  QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  QObject::connect(worker, &QThread::finished, owner_,
                   [this, generation, body, path, content_type, done = std::move(done), failed = std::move(failed)] {
                     if (generation != generation_) {
                       return;  // cancelled or replaced while the body was being built
                     }
                     preparing_ = false;
                     auto network_request = make_request(path);
                     network_request.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromLatin1(content_type));
                     watch(manager_->post(network_request, *body), done, failed);
                   });
  worker->start();
}

void Client::list_models(std::function<void(QStringList)> done, std::function<void(QString)> failed) {
  cancel();
  last_path_ = QStringLiteral("/models");
  auto* reply = manager_->get(make_request(last_path_));
  watch(reply,
        [done = std::move(done), failed](const QByteArray& body) {
          QString error;
          auto ids = image_model_ids(body, &error);
          if (!error.isEmpty()) {
            failed(error);
            return;
          }
          done(std::move(ids));
        },
        failed);
}

void Client::generate(const GenerateRequest& request, std::function<void(GenerateResult)> done,
                      std::function<void(QString)> failed) {
  const bool json = request.references.empty() && request.edit_image.isNull();
  const auto boundary = "patchy-" + QUuid::createUuid().toByteArray(QUuid::Id128);
  post_prepared(json ? QStringLiteral("/images/generations") : QStringLiteral("/images/edits"),
                json ? QByteArrayLiteral("application/json") : "multipart/form-data; boundary=" + boundary,
                [request, boundary, json] {
                  return json ? QJsonDocument(generation_request_json(request)).toJson(QJsonDocument::Compact)
                              : edits_multipart_body(request, boundary);
                },
        [done = std::move(done), failed](const QByteArray& body) {
          QString error;
          auto result = parse_images_response(body, &error);
          if (result.images.empty()) {
            failed(error);
            return;
          }
          done(std::move(result));
        },
        failed);
}

void Client::describe(const QImage& image, std::function<void(DescribeResult)> done,
                      std::function<void(QString)> failed) {
  post_prepared(QStringLiteral("/responses"), QByteArrayLiteral("application/json"),
                [image] {
                  return QJsonDocument(describe_request_json(image, QString::fromLatin1(kDescribeModel)))
                      .toJson(QJsonDocument::Compact);
                },
        [done = std::move(done), failed](const QByteArray& body) {
          QString error;
          auto result = parse_describe_response(body, &error);
          if (!error.isEmpty()) {
            failed(error);
            return;
          }
          done(std::move(result));
        },
        failed);
}

void Client::cancel() {
  ++generation_;  // a body still being built is dropped when it finishes
  preparing_ = false;
  if (reply_.isNull()) {
    return;
  }
  QNetworkReply* reply = reply_;
  reply_ = nullptr;
  reply->setProperty("patchyCancelled", true);
  reply->abort();
}

}  // namespace patchy::ui::openai
