// OpenAI Image Tools (docs/openai-images.md): request sizing and bodies, response parsing, the
// reference file reader, and the dialog end to end against a local stand-in for the API.
// Nothing here reaches the network or the system key store.
#include "formats/gif_document_io.hpp"
#include "test_harness.hpp"
#include "test_scratch_remove.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/companion_apps.hpp"
#include "ui/main_window.hpp"
#include "ui/openai_images.hpp"
#include "ui/openai_images_dialog.hpp"
#include "ui/qt_paths.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"
#include "ui/ui_text_size.hpp"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QElapsedTimer>
#include <QUrl>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTest>
#include <QToolButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QFile>

#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

namespace {
using namespace patchy::test::ui;
namespace openai = patchy::ui::openai;

template <typename Predicate> bool wait_until(Predicate ready, int timeout_ms = 10000) {
  QElapsedTimer deadline;
  deadline.start();
  while (!ready() && deadline.elapsed() < timeout_ms) {
    QApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return ready();
}

// 32x16: the left half opaque red, the right half green at alpha 128.
QImage half_transparent_image() {
  QImage image(32, 16, QImage::Format_RGBA8888);
  for (int y = 0; y < image.height(); ++y) {
    auto* row = image.scanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      const bool left = x < 16;
      row[x * 4 + 0] = left ? 255 : 0;
      row[x * 4 + 1] = left ? 0 : 255;
      row[x * 4 + 2] = 0;
      row[x * 4 + 3] = left ? 255 : 128;
    }
  }
  return image;
}

QByteArray png_of(const QImage& image) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  QImageWriter writer(&buffer, "png");
  (void)writer.write(image);
  return bytes;
}

// Variation 1 is half_transparent_image; later ones have a blue left half instead of red.
QImage variation_image(int index) {
  auto image = half_transparent_image();
  if (index > 0) {
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < 16; ++x) {
        image.setPixelColor(x, y, QColor(0, 0, 255, 255));
      }
    }
  }
  return image;
}

QByteArray images_response(int count) {
  QJsonArray data;
  for (int index = 0; index < count; ++index) {
    data.append(QJsonObject{{"b64_json", QString::fromLatin1(png_of(variation_image(index)).toBase64())}});
  }
  return QJsonDocument(QJsonObject{{"created", 1},
                                   {"data", data},
                                   {"usage", QJsonObject{{"input_tokens", 12}, {"output_tokens", 800}, {"total_tokens", 812}}}})
      .toJson(QJsonDocument::Compact);
}

// Opaque 8 px checkerboards, the second variation inverted: a pattern the tone match's mean
// shift cannot flatten away.
QByteArray checker_response(int count) {
  QJsonArray data;
  for (int index = 0; index < count; ++index) {
    QImage image(32, 16, QImage::Format_RGBA8888);
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 32; ++x) {
        const bool dark = ((x / 8 + y / 8) % 2 == 1) != (index > 0);
        image.setPixelColor(x, y, dark ? QColor(0, 0, 0, 255) : QColor(255, 255, 255, 255));
      }
    }
    data.append(QJsonObject{{"b64_json", QString::fromLatin1(png_of(image).toBase64())}});
  }
  return QJsonDocument(QJsonObject{{"created", 1}, {"data", data}}).toJson(QJsonDocument::Compact);
}

// A 16x8 three-frame GIF (frame delays 0.1, 0.2 and 0.3 s), as Patchy's own encoder writes it.
QByteArray three_frame_gif() {
  std::vector<patchy::gif::GifFrame> frames;
  for (int index = 0; index < 3; ++index) {
    patchy::gif::GifFrame frame;
    frame.palette = {patchy::RgbColor{0, 0, 0}, patchy::RgbColor{255, 0, 0}};
    frame.transparent_index = 0;
    frame.delay_cs = static_cast<std::uint16_t>(10 * (index + 1));
    frame.indexes.assign(16 * 8, 0);
    for (int y = 0; y < 8; ++y) {
      frame.indexes[static_cast<std::size_t>(y * 16 + index * 4)] = 1;  // a red column that moves
    }
    frames.push_back(std::move(frame));
  }
  const auto bytes = patchy::gif::encode_animation(16, 8, frames);
  return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

QByteArray gif_response() {
  QJsonArray data;
  data.append(QJsonObject{{"b64_json", QString::fromLatin1(three_frame_gif().toBase64())}});
  return QJsonDocument(QJsonObject{{"created", 1}, {"output_format", "gif"}, {"data", data}}).toJson(QJsonDocument::Compact);
}

// A one-request-per-connection HTTP/1.1 stand-in for api.openai.com.
struct FakeApi {
  struct Request {
    QByteArray method;
    QByteArray path;
    QByteArray headers;
    QByteArray body;
  };
  QTcpServer server;
  std::vector<Request> requests;
  int status{200};
  QByteArray reply_body;

  bool start() {
    if (!server.listen(QHostAddress::LocalHost, 0)) {
      return false;
    }
    QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
      while (auto* socket = server.nextPendingConnection()) {
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
          buffer->append(socket->readAll());
          const auto header_end = buffer->indexOf("\r\n\r\n");
          if (header_end < 0) {
            return;
          }
          const auto headers = buffer->left(header_end);
          qsizetype length = 0;
          for (const auto& line : headers.split('\n')) {
            if (line.toLower().startsWith("content-length:")) {
              length = line.mid(15).trimmed().toLongLong();
            }
          }
          if (buffer->size() < header_end + 4 + length) {
            return;
          }
          const auto request_line = headers.left(headers.indexOf("\r\n")).split(' ');
          requests.push_back({request_line.value(0), request_line.value(1), headers, buffer->mid(header_end + 4, length)});
          const QByteArray reason = status == 200 ? "OK" : "Error";
          socket->write("HTTP/1.1 " + QByteArray::number(status) + " " + reason +
                        "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(reply_body.size()) +
                        "\r\nConnection: close\r\n\r\n" + reply_body);
          socket->disconnectFromHost();
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      }
    });
    return true;
  }
  [[nodiscard]] QUrl base_url() const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()));
  }
};

struct MemoryKeyStore final : openai::KeyStore {
  std::shared_ptr<QString> value;
  explicit MemoryKeyStore(std::shared_ptr<QString> shared) : value(std::move(shared)) {}
  [[nodiscard]] bool available() const override { return true; }
  [[nodiscard]] QString load() const override { return *value; }
  bool save(const QString& key) override {
    *value = key;
    return true;
  }
  bool clear() override {
    value->clear();
    return true;
  }
};

const patchy::Layer* find_layer_named(const patchy::Document& document, const std::string& name) {
  for (const auto& layer : document.layers()) {
    if (layer.name() == name) {
      return &layer;
    }
  }
  return nullptr;
}

// Request sizes stay inside the API's rules for any canvas: edges on the 16 px grid, aspect
// 1:3..3:1, the pixel floor and the long-edge ceiling.
void ui_openai_request_size_follows_the_canvas_within_api_limits() {
  const QSize canvases[] = {{64, 48},    {1920, 1080}, {1080, 1920}, {100, 1000}, {4000, 200},
                            {10, 10},    {8000, 8000}, {0, 0},       {1, 4000},   {2560, 1440}};
  for (const auto canvas : canvases) {
    const auto size = openai::fit_request_size(canvas);
    CHECK(size.width() % openai::kRequestSizeStep == 0 && size.height() % openai::kRequestSizeStep == 0);
    CHECK(size.width() <= 3 * size.height() && size.height() <= 3 * size.width());
    const auto pixels = static_cast<std::int64_t>(size.width()) * size.height();
    CHECK(pixels >= openai::kMinRequestPixels);
    CHECK(pixels <= openai::kMaxRequestPixels);
    CHECK(size.width() <= openai::kMaxRequestEdge && size.height() <= openai::kMaxRequestEdge);
  }
  CHECK(openai::fit_request_size({1024, 1024}) == QSize(1024, 1024));
  const auto wide = openai::fit_request_size({1920, 1080});
  CHECK(wide.width() > wide.height());
  CHECK(std::abs(static_cast<double>(wide.width()) / wide.height() - 16.0 / 9.0) < 0.05);
  const auto clamped = openai::fit_request_size({4000, 200});
  CHECK(clamped.width() == 3 * clamped.height());
  CHECK(openai::size_parameter(QSize()) == QStringLiteral("auto"));
  CHECK(openai::size_parameter({1536, 1024}) == QStringLiteral("1536x1024"));
}

// The generations body carries the dialog's choices; the edits body carries each reference
// as an "image[]" PNG part and never a mask (docs/legal-constraints.md, "Prompt-driven image
// generation"); uploads carry no metadata from their source.
void ui_openai_request_bodies_send_references_without_a_mask() {
  openai::GenerateRequest request;
  request.model = QStringLiteral("gpt-image-2.6-flare");
  request.prompt = QStringLiteral("a cactus");
  request.size = QSize(1024, 1536);
  request.quality = QStringLiteral("low");
  request.background = QStringLiteral("transparent");
  request.count = 9;  // clamped to the dialog's ceiling
  const auto json = openai::generation_request_json(request);
  CHECK(json.value(QStringLiteral("model")).toString() == QStringLiteral("gpt-image-2.6-flare"));
  CHECK(json.value(QStringLiteral("prompt")).toString() == QStringLiteral("a cactus"));
  CHECK(json.value(QStringLiteral("size")).toString() == QStringLiteral("1024x1536"));
  CHECK(json.value(QStringLiteral("quality")).toString() == QStringLiteral("low"));
  CHECK(json.value(QStringLiteral("background")).toString() == QStringLiteral("transparent"));
  CHECK(json.value(QStringLiteral("output_format")).toString() == QStringLiteral("png"));
  CHECK(json.value(QStringLiteral("n")).toInt() == openai::kMaxImagesPerRequest);
  CHECK(!json.contains(QStringLiteral("mask")));

  auto tagged = half_transparent_image();
  tagged.setText(QStringLiteral("Comment"), QStringLiteral("camera-setting-canary"));
  CHECK(png_of(tagged).contains("camera-setting-canary"));  // the canary does reach a plain PNG
  const auto upload = openai::png_bytes(tagged);
  CHECK(upload.startsWith("\x89PNG"));
  CHECK(!upload.contains("camera-setting-canary"));
  CHECK(!upload.contains("tEXt") && !upload.contains("iTXt") && !upload.contains("eXIf"));

  request.references = {tagged, half_transparent_image()};
  const QByteArray boundary = "patchy-test-boundary";
  const auto body = openai::edits_multipart_body(request, boundary);
  CHECK(body.count("name=\"image[]\"") == 2);
  CHECK(!body.contains("name=\"mask\""));
  CHECK(!body.contains("name=\"markup\""));
  CHECK(body.contains("name=\"moderation\"\r\n\r\nlow\r\n"));  // always the lowest filtering level
  CHECK(!body.contains("name=\"complexity\""));                    // Create keeps the model default
  CHECK(openai::generation_request_json(request).value(QStringLiteral("moderation")).toString() == QStringLiteral("low"));
  CHECK(!body.contains("camera-setting-canary"));
  CHECK(body.contains("name=\"prompt\"\r\n\r\na cactus\r\n"));
  CHECK(body.contains("name=\"size\"\r\n\r\n1024x1536\r\n"));
  CHECK(body.endsWith("--patchy-test-boundary--\r\n"));
}

// Returned PNGs keep their alpha; usage, API error bodies and the model list are read.
void ui_openai_responses_keep_alpha_usage_and_errors() {
  QString error;
  const auto result = openai::parse_images_response(images_response(2), &error);
  CHECK(error.isEmpty());
  CHECK(result.images.size() == 2);
  CHECK(result.images.front().format() == QImage::Format_RGBA8888);
  CHECK(result.images.front().pixelColor(2, 2) == QColor(255, 0, 0, 255));
  CHECK(result.images.front().pixelColor(30, 2).alpha() == 128);
  CHECK(result.total_tokens == 812 && result.output_tokens == 800 && result.input_tokens == 12);

  QString bad_error;
  CHECK(openai::parse_images_response("not json", &bad_error).images.empty());
  CHECK(!bad_error.isEmpty());
  QString empty_error;
  CHECK(openai::parse_images_response(R"({"data": []})", &empty_error).images.empty());
  CHECK(!empty_error.isEmpty());

  const QByteArray api_error = R"({"error": {"message": "Invalid size '1024x512'.", "type": "image_generation_user_error"}})";
  CHECK(openai::api_error_message(api_error, 400) == QStringLiteral("Invalid size '1024x512'."));
  // Plain-language errors, with a link where an OpenAI page fixes it.
  const QByteArray bad_key = R"({"error": {"message": "Incorrect API key provided: sk-ab***yz.", "code": "invalid_api_key"}})";
  CHECK(openai::api_error_message(bad_key, 401).contains(QStringLiteral("did not accept this API key")));
  CHECK(openai::api_error_help(bad_key, 401).url == QStringLiteral("https://platform.openai.com/api-keys"));
  const QByteArray quota =
      R"({"error": {"message": "You exceeded your current quota.", "type": "insufficient_quota", "code": "insufficient_quota"}})";
  CHECK(openai::api_error_message(quota, 429).contains(QStringLiteral("out of credit")));
  CHECK(openai::api_error_help(quota, 429).url.contains(QStringLiteral("billing")));
  const QByteArray limit = R"({"error": {"message": "Billing hard limit has been reached", "code": "billing_hard_limit_reached"}})";
  CHECK(openai::api_error_message(limit, 400).contains(QStringLiteral("spending limit")));
  CHECK(openai::api_error_help(limit, 400).url.contains(QStringLiteral("limits")));
  const QByteArray busy = R"({"error": {"message": "Rate limit reached", "code": "rate_limit_exceeded"}})";
  CHECK(openai::api_error_message(busy, 429).contains(QStringLiteral("Wait a moment")));
  CHECK(openai::api_error_help(busy, 429).url.isEmpty());
  const QByteArray safety = R"({"error": {"message": "Your request was rejected by the safety system.", "code": "moderation_blocked"}})";
  CHECK(openai::api_error_message(safety, 400).contains(QStringLiteral("safety system declined")));
  const QByteArray verify = R"({"error": {"message": "Your organization must be verified to use the model."}})";
  CHECK(openai::api_error_message(verify, 403).contains(QStringLiteral("verified OpenAI organization")));
  CHECK(openai::api_error_help(verify, 403).url.contains(QStringLiteral("organization/general")));
  CHECK(openai::api_error_message("", 503).contains(QStringLiteral("503")));
  CHECK(openai::api_error_message(R"({"error": {"message": "The model does not exist", "code": "model_not_found"}})", 404)
            .contains(QStringLiteral("cannot use the chosen model")));
  CHECK(openai::api_error_message("", 500).contains(QStringLiteral("500")));

  QString models_error;
  const auto models = openai::image_model_ids(
      R"({"data": [{"id": "gpt-5"}, {"id": "gpt-image-2"}, {"id": "gpt-image-2.6-flare"}, {"id": "chatgpt-image-latest"}]})",
      &models_error);
  CHECK(models_error.isEmpty());
  CHECK(models == QStringList({QStringLiteral("gpt-image-2.6-flare"), QStringLiteral("gpt-image-2"),
                               QStringLiteral("chatgpt-image-latest")}));
}

// A reference image file with a Unicode name opens; a large one is scaled to 2048 px.
void ui_openai_reference_files_open_from_unicode_paths() {
  const auto directory = patchy::test::unicode_artifact_dir(u8"openai-references");
  const auto cleanup = qScopeGuard([&] { patchy::test::remove_test_scratch_tree(directory.parent_path()); });
  for (const auto stem : patchy::test::kUnicodePathStems) {
    auto path = directory / patchy::test::unicode_path_piece(stem);
    path += ".png";
    CHECK(half_transparent_image().save(patchy::ui::to_qstring(path), "PNG"));
    QString error;
    const auto image = openai::load_reference_image_file(patchy::ui::to_qstring(path), &error);
    CHECK(error.isEmpty());
    CHECK(image.size() == QSize(32, 16));
    CHECK(image.pixelColor(30, 2).alpha() == 128);
  }
  QImage big(4096, 1024, QImage::Format_RGBA8888);
  big.fill(Qt::blue);
  CHECK(openai::prepare_reference_image(big).size() == QSize(2048, 512));
  QString missing_error;
  CHECK(openai::load_reference_image_file(patchy::ui::to_qstring(directory / "missing.png"), &missing_error).isNull());
  CHECK(!missing_error.isEmpty());
}

// Edit and Remove: the context is the whole canvas when it fits the request, else a wide
// area around the selection grown toward the API's aspect range; the request images keep the
// 16 px grid, the mask opens only where the selection is, Edit's markup rings it, padding is
// kept, and the result comes back at the region's size.
void ui_openai_masked_edit_images_follow_the_selection() {
  const QSize canvas(400, 300);
  CHECK(openai::context_region(QRect(100, 100, 40, 20), canvas) == QRect(QPoint(0, 0), canvas));
  CHECK(openai::context_region(QRect(500, 500, 10, 10), canvas).isEmpty());
  CHECK(openai::context_region(QRect(900, 600, 181, 144), QSize(2000, 1500)) == QRect(0, 0, 2000, 1500));
  // Edit (tight): a crop of the selection grown by its longer side (at least 160 px) each way,
  // unless that would cover half the canvas.
  CHECK(openai::context_region(QRect(859, 593, 181, 144), QSize(2000, 1500), true) == QRect(678, 412, 543, 506));
  CHECK(openai::context_region(QRect(900, 700, 40, 20), QSize(2000, 1500), true) == QRect(740, 540, 360, 340));
  CHECK(openai::context_region(QRect(100, 100, 40, 20), canvas, true) == QRect(QPoint(0, 0), canvas));
  // A large canvas: the selection's surroundings, a quarter of the canvas's longer side each way.
  const QSize large(8000, 6000);
  const auto region = openai::context_region(QRect(4000, 3000, 100, 50), large);
  CHECK(region.contains(QRect(4000, 3000, 100, 50)));
  CHECK(QRect(QPoint(0, 0), large).contains(region));
  CHECK(region.left() <= 4000 - 2000 && region.right() >= 4100 + 2000 - 1 && region.top() <= 3000 - 2000);
  CHECK(region.width() < 8000);
  // Grown past half the canvas, it is the whole canvas.
  CHECK(openai::context_region(QRect(1000, 1000, 5000, 3000), large) == QRect(QPoint(0, 0), large));
  // A wide strip on a tall-enough large canvas grows to 3:1.
  const auto strip = openai::context_region(QRect(0, 3000, 8000, 4), QSize(8000, 30000));
  CHECK(strip.width() <= 3 * strip.height());
  // On a strip-shaped canvas the region stays inside it and the request is padded instead.
  const auto thin = openai::context_region(QRect(0, 0, 1200, 20), QSize(1200, 100));
  CHECK(thin.height() <= 100);

  QImage context(300, 60, QImage::Format_RGBA8888);
  context.fill(QColor(10, 200, 30, 255));
  QImage coverage(300, 60, QImage::Format_Grayscale8);
  coverage.fill(0);
  for (int y = 20; y < 40; ++y) {
    for (int x = 100; x < 200; ++x) {
      coverage.scanLine(y)[x] = x == 100 ? 1 : 255;  // a soft edge still opens the mask
    }
  }
  const auto sent = openai::masked_edit_images(context, coverage);
  CHECK(sent.padded_size == QSize(300, 100));
  CHECK(sent.content_rect == QRect(0, 20, 300, 60));
  CHECK(sent.image.size() == sent.mask.size());
  CHECK(sent.image.width() % openai::kRequestSizeStep == 0 && sent.image.height() % openai::kRequestSizeStep == 0);
  CHECK(static_cast<std::int64_t>(sent.image.width()) * sent.image.height() >= openai::kMinRequestPixels);
  const auto at = [&](double x, double y) {
    return QPoint(static_cast<int>(x * sent.mask.width() / 300.0), static_cast<int>(y * sent.mask.height() / 100.0));
  };
  CHECK(sent.mask.pixelColor(at(150, 50)).alpha() == 0);    // selected: edit
  CHECK(sent.mask.pixelColor(at(20, 50)).alpha() == 255);   // outside: keep
  CHECK(sent.mask.pixelColor(at(150, 5)).alpha() == 255);   // padding: keep
  CHECK(sent.image.pixelColor(at(150, 5)).alpha() == 0);    // padding is transparent
  CHECK(sent.image.pixelColor(at(20, 50)).green() > 150);
  const auto back = openai::result_for_region(sent.image, sent);
  CHECK(back.size() == QSize(300, 60));
  CHECK(back.pixelColor(20, 30).green() > 150);
  // A selection reaching the context's left edge: Edit pads that side, so the ring lies off
  // the picture, and the result still comes back at the region's size.
  {
    QImage edge_cover(300, 60, QImage::Format_Grayscale8);
    edge_cover.fill(0);
    for (int y = 10; y < 50; ++y) {
      std::memset(edge_cover.scanLine(y), 255, 120);  // x 0..119, touching the left edge
    }
    const auto padded = openai::masked_edit_images(context, edge_cover, 12);
    CHECK(padded.content_rect.size() == QSize(300, 60));
    CHECK(padded.content_rect.x() >= 12);                       // padding on the left only
    CHECK(padded.padded_size.width() - padded.content_rect.right() - 1 >= 0);
    const auto edge_markup = openai::markup_overlay(edge_cover, padded);
    const double scale = static_cast<double>(edge_markup.width()) / padded.padded_size.width();
    bool red_left_of_picture = false;
    for (int x = 0; x < static_cast<int>(padded.content_rect.x() * scale); ++x) {
      const auto color = edge_markup.pixelColor(x, static_cast<int>((padded.content_rect.y() + 30) * scale));
      red_left_of_picture = red_left_of_picture || (color.alpha() == 255 && color.red() > 200);
    }
    CHECK(red_left_of_picture);
    CHECK(openai::result_for_region(padded.image, padded).size() == QSize(300, 60));
    CHECK(!openai::coverage_is_full(edge_cover));
    QImage all(4, 4, QImage::Format_Grayscale8);
    all.fill(255);
    CHECK(openai::coverage_is_full(all));
    CHECK(openai::markup_edge_pad(QSize(1000, 400)) == 20 && openai::markup_edge_pad(QSize(100, 40)) == 8);
  }
  // Edit's markup: the request's size, red just outside the selection, transparent on it and
  // away from it (so the API sees transparent pixels and the selected content stays visible).
  const auto markup = openai::markup_overlay(coverage, sent);
  CHECK(markup.size() == sent.image.size());
  CHECK(markup.pixelColor(at(150, 50)).alpha() == 0);
  CHECK(markup.pixelColor(at(20, 50)).alpha() == 0);
  bool red_above = false;
  for (int y = 0; y < 20 + 20; ++y) {  // the column above the selection's middle, in padded rows
    const auto color = markup.pixelColor(at(150, y));
    red_above = red_above || (color.alpha() == 255 && color.red() > 200 && color.green() < 60);
  }
  CHECK(red_above);

  // The edits body carries the image and its mask, and never reference images with them.
  openai::GenerateRequest request;
  request.model = openai::default_model();
  request.prompt = QString::fromLatin1(openai::kRemovePrompt);
  request.background = QStringLiteral("opaque");
  request.size = sent.image.size();
  request.edit_image = sent.image;
  request.edit_mask = sent.mask;
  request.complexity = QString::fromLatin1(openai::kMaskedEditComplexity);
  const auto body = openai::edits_multipart_body(request, "b");
  CHECK(body.count("name=\"image[]\"") == 1);
  CHECK(body.count("name=\"mask\"") == 1);
  CHECK(!body.contains("name=\"markup\""));
  CHECK(body.contains("name=\"background\"\r\n\r\nopaque\r\n"));
  CHECK(body.contains("name=\"complexity\"\r\n\r\nlow\r\n"));
  // Edit: the markup instead of the mask (the API refuses both together).
  request.edit_mask = QImage();
  request.edit_markup = markup;
  const auto markup_body = openai::edits_multipart_body(request, "b");
  CHECK(markup_body.count("name=\"markup\"") == 1);
  CHECK(!markup_body.contains("name=\"mask\""));
}

// Describe: the request carries the fixed instruction and the image scaled down to the cap
// as a data URL, asks OpenAI not to store it, and the reply's text parts are joined; a
// refusal or an empty reply is an error.
void ui_openai_describe_request_and_reply() {
  QImage big(3000, 1500, QImage::Format_RGBA8888);
  big.fill(QColor(20, 120, 220, 255));
  const auto json = openai::describe_request_json(big, QStringLiteral("gpt-test"));
  CHECK(json.value(QStringLiteral("model")).toString() == QStringLiteral("gpt-test"));
  CHECK(json.value(QStringLiteral("store")).toBool(true) == false);
  const auto content = json.value(QStringLiteral("input")).toArray().at(0).toObject().value(QStringLiteral("content")).toArray();
  CHECK(content.size() == 2);
  CHECK(content.at(0).toObject().value(QStringLiteral("text")).toString() == QString::fromLatin1(openai::kDescribeInstructions));
  const auto url = content.at(1).toObject().value(QStringLiteral("image_url")).toString();
  CHECK(url.startsWith(QStringLiteral("data:image/png;base64,")));
  const auto sent = QImage::fromData(QByteArray::fromBase64(url.mid(22).toLatin1()));
  CHECK(sent.size() == QSize(openai::kDescribeMaxEdge, openai::kDescribeMaxEdge / 2));
  QImage small(40, 20, QImage::Format_RGBA8888);
  small.fill(Qt::red);
  const auto small_url = openai::describe_request_json(small, QStringLiteral("m"))
                             .value(QStringLiteral("input")).toArray().at(0).toObject()
                             .value(QStringLiteral("content")).toArray().at(1).toObject()
                             .value(QStringLiteral("image_url")).toString();
  CHECK(QImage::fromData(QByteArray::fromBase64(small_url.mid(22).toLatin1())).size() == QSize(40, 20));  // never scaled up

  QString error;
  const auto reply = openai::parse_describe_response(
      R"({"output": [{"type": "reasoning"}, {"type": "message", "content": [{"type": "output_text", "text": "First."},
          {"type": "output_text", "text": "Second."}]}], "usage": {"total_tokens": 42}})", &error);
  CHECK(error.isEmpty());
  CHECK(reply.text == QStringLiteral("First.\n\nSecond."));
  CHECK(reply.total_tokens == 42);
  QString refusal_error;
  CHECK(openai::parse_describe_response(
            R"({"output": [{"type": "message", "content": [{"type": "refusal", "refusal": "I can't help with that."}]}]})",
            &refusal_error)
            .text.isEmpty());
  CHECK(refusal_error.contains(QStringLiteral("I can't help with that.")));
  QString empty_error;
  (void)openai::parse_describe_response(R"({"output": []})", &empty_error);
  CHECK(!empty_error.isEmpty());
  QString junk_error;
  (void)openai::parse_describe_response("not json", &junk_error);
  CHECK(!junk_error.isEmpty());
}

// Seams: dilation grows the mask, the feather softens it, the tone match moves the result's
// mean onto the original over the ring, and the weighted blend composes like a masked layer.
void ui_openai_seam_helpers_dilate_feather_and_match_tone() {
  CHECK(openai::seam_band(QSize(10, 8)) == 4);
  CHECK(openai::seam_band(QSize(500, 200)) == 20);
  CHECK(openai::seam_band(QSize(4000, 100)) == 32);
  QImage dot(21, 21, QImage::Format_Grayscale8);
  dot.fill(0);
  dot.setPixel(10, 10, qRgb(255, 255, 255));
  const auto grown = openai::dilate_coverage(dot, 3);
  CHECK(openai::coverage_bounds(grown) == QRect(7, 7, 7, 7));
  CHECK(openai::coverage_bounds(dot) == QRect(10, 10, 1, 1));
  QImage empty(8, 8, QImage::Format_Grayscale8);
  empty.fill(0);
  CHECK(openai::coverage_bounds(empty).isEmpty());
  const auto soft = openai::feather_coverage(grown, 2.0);
  CHECK(soft.pixelColor(10, 10).red() > 128);
  CHECK(soft.pixelColor(5, 10).red() > 0 && soft.pixelColor(5, 10).red() < 128);
  CHECK(soft.pixelColor(0, 0).red() == 0);
  // The ring sits outside three bands and the tone match shifts by the ring's mean difference.
  QImage cover(60, 60, QImage::Format_Grayscale8);
  cover.fill(0);
  for (int y = 28; y < 32; ++y) {
    for (int x = 28; x < 32; ++x) {
      cover.setPixel(x, y, qRgb(255, 255, 255));
    }
  }
  const auto ring = openai::tone_ring(cover, 4);
  CHECK(ring.pixelColor(30, 30).red() == 0);       // inside the changed area
  CHECK(ring.pixelColor(30, 28 - 14).red() == 255);  // just past 3 bands (12 px) above the top edge
  CHECK(ring.pixelColor(2, 2).red() == 0);
  QImage original(60, 60, QImage::Format_RGBA8888);
  original.fill(QColor(120, 100, 80, 255));
  QImage result(60, 60, QImage::Format_RGBA8888);
  result.fill(QColor(100, 110, 60, 255));
  const auto matched = openai::match_tone(result, original, ring);
  CHECK(matched.pixelColor(30, 30) == QColor(120, 100, 80, 255));
  CHECK(openai::match_tone(result, original, QImage(60, 60, QImage::Format_Grayscale8)).size() == QSize(60, 60));
  QImage weight(60, 60, QImage::Format_Grayscale8);
  weight.fill(0);
  weight.setPixel(5, 5, qRgb(255, 255, 255));
  const auto blended = openai::blend_with_weight(original, result, weight);
  CHECK(blended.pixelColor(5, 5) == QColor(100, 110, 60, 255));
  CHECK(blended.pixelColor(6, 5) == QColor(120, 100, 80, 255));
  const auto prompt = openai::animation_prompt(QStringLiteral("the flag waves"), 6, true);
  CHECK(prompt.contains(QStringLiteral("exactly 6 sequential frames")));
  CHECK(prompt.contains(QStringLiteral("Animate the provided image")));
  CHECK(prompt.endsWith(QStringLiteral("Motion: the flag waves")));
  CHECK(!openai::animation_prompt(QStringLiteral("x"), 6, false).contains(QStringLiteral("provided image")));
  CHECK(openai::animation_prompt(QStringLiteral("x"), 6, true, 2).contains(QStringLiteral("Animate the first provided image")));
  CHECK(openai::animation_prompt(QStringLiteral("x"), 6, false, 2).startsWith(QStringLiteral("Use the provided images as references")));
}

// Animate: the GIF request fields, and the returned GIF's frames with their delays.
void ui_openai_gif_requests_and_frames() {
  // Full color: the grid for each frame count (fewest spare cells, columns >= rows, at most
  // 3:1), slicing in reading order, and the fixed sheet wording around the motion.
  const auto layout_of = [](int frames) {
    const auto layout = openai::sheet_layout(frames);
    return QSize(layout.columns, layout.rows);
  };
  CHECK(layout_of(2) == QSize(2, 1));
  CHECK(layout_of(4) == QSize(2, 2));
  CHECK(layout_of(5) == QSize(3, 2));  // rounds up to 6
  CHECK(layout_of(7) == QSize(4, 2));
  CHECK(layout_of(9) == QSize(3, 3));
  CHECK(layout_of(10) == QSize(5, 2));
  CHECK(layout_of(11) == QSize(4, 3));
  CHECK(layout_of(13) == QSize(5, 3));
  CHECK(layout_of(16) == QSize(4, 4));
  for (int frames = openai::kMinGifFrames; frames <= openai::kMaxGifFrames; ++frames) {
    const auto layout = openai::sheet_layout(frames);
    CHECK(layout.frames() >= frames && layout.columns >= layout.rows && layout.columns <= 3 * layout.rows);
    // The sheet at the largest frame size the count allows stays within the API's limits.
    const int edge = openai::max_gif_frame_edge(frames);
    CHECK(layout.columns * edge <= 3840);
    CHECK(static_cast<std::int64_t>(layout.columns) * edge * layout.rows * edge <= 3840LL * 2160LL);
  }
  QImage sheet(30, 20, QImage::Format_RGBA8888);
  const QColor panel_colors[] = {Qt::red, Qt::green, Qt::blue, Qt::yellow, Qt::cyan, Qt::magenta};
  for (int index = 0; index < 6; ++index) {
    QPainter painter(&sheet);
    painter.fillRect(QRect((index % 3) * 10, (index / 3) * 10, 10, 10), panel_colors[index]);
  }
  const auto sliced = openai::sheet_frames(sheet, openai::sheet_layout(5), 150);
  CHECK(sliced.size() == 6);
  for (std::size_t index = 0; index < sliced.size() && index < 6; ++index) {
    CHECK(sliced[index].image.size() == QSize(10, 10) && sliced[index].delay_ms == 150);
    CHECK(sliced[index].image.pixelColor(5, 5) == panel_colors[index]);
  }
  // Two 512 px frames would make a 1024 x 512 sheet, under the API's smallest image: the request
  // is enlarged (same shape) and the frames scale back to 512 px.
  const auto small_request = openai::sheet_request_size(openai::sheet_layout(2), 512);
  CHECK(static_cast<std::int64_t>(small_request.width()) * small_request.height() >= openai::kMinRequestPixels);
  CHECK(small_request.width() == 2 * small_request.height());
  CHECK(openai::sheet_request_size(openai::sheet_layout(9), 512) == QSize(1536, 1536));
  QImage big_sheet(small_request, QImage::Format_RGBA8888);
  big_sheet.fill(Qt::red);
  const auto scaled = openai::sheet_frames(big_sheet, openai::sheet_layout(2), 100, QSize(512, 512));
  CHECK(scaled.size() == 2 && scaled.front().image.size() == QSize(512, 512));
  const auto sheet_prompt = openai::sheet_animation_prompt(QStringLiteral("the flag waves"), openai::sheet_layout(9), false);
  CHECK(sheet_prompt.contains(QStringLiteral("exactly 9 sequential frames")));
  CHECK(sheet_prompt.contains(QStringLiteral("3 columns and 3 rows")));
  CHECK(sheet_prompt.endsWith(QStringLiteral("Motion: the flag waves")));
  openai::GenerateRequest request;
  request.model = openai::default_model();
  request.prompt = QStringLiteral("a waving flag");
  request.size = QSize(512, 512);
  request.animated_gif = true;
  request.frame_count = 40;          // clamped to 16
  request.frame_duration_ms = 5;     // clamped to 20
  const auto json = openai::generation_request_json(request);
  CHECK(json.value(QStringLiteral("output_format")).toString() == QStringLiteral("gif"));
  CHECK(json.value(QStringLiteral("frame_count")).toInt() == openai::kMaxGifFrames);
  CHECK(json.value(QStringLiteral("frame_duration_ms")).toInt() == openai::kMinGifFrameMs);
  request.references = {half_transparent_image()};
  request.frame_count = 6;
  const auto body = openai::edits_multipart_body(request, "b");
  CHECK(body.contains("name=\"output_format\"\r\n\r\ngif\r\n"));
  CHECK(body.contains("name=\"frame_count\"\r\n\r\n6\r\n"));
  CHECK(body.count("name=\"image[]\"") == 1);
  CHECK(!body.contains("name=\"mask\""));

  CHECK(openai::max_gif_frame_edge(2) == 960 && openai::max_gif_frame_edge(9) == 960);
  CHECK(openai::max_gif_frame_edge(10) == 720 && openai::max_gif_frame_edge(16) == 720);
  QString error;
  const auto frames = openai::gif_frames(three_frame_gif(), &error);
  CHECK(error.isEmpty());
  CHECK(frames.size() == 3);
  if (frames.size() == 3) {
    CHECK(frames[0].delay_ms == 100 && frames[1].delay_ms == 200 && frames[2].delay_ms == 300);
    CHECK(frames[0].image.size() == QSize(16, 8));
    CHECK(frames[1].image.pixelColor(4, 2) == QColor(255, 0, 0, 255));
    CHECK(frames[1].image.pixelColor(0, 2).alpha() == 0);
  }
  QString still_error;
  CHECK(openai::gif_frames(png_of(half_transparent_image()), &still_error).empty());
  CHECK(!still_error.isEmpty());
}

// Expand: the frame holds the visible image at (left, top) of the grown canvas, the coverage
// opens only the new area, and a large canvas is scaled down to the frame budget.
void ui_openai_expand_frame_marks_only_the_new_area() {
  QImage visible(100, 60, QImage::Format_RGBA8888);
  visible.fill(QColor(200, 30, 30, 255));
  const auto frame = openai::expand_frame(visible, QMargins(50, 0, 50, 10));
  CHECK(frame.context.size() == QSize(200, 70));
  CHECK(frame.coverage.size() == QSize(200, 70));
  CHECK(frame.context.pixelColor(100, 30) == QColor(200, 30, 30, 255));
  CHECK(frame.context.pixelColor(10, 30).alpha() == 0);
  CHECK(frame.coverage.pixelColor(10, 30).red() == 255);   // new: edit
  CHECK(frame.coverage.pixelColor(190, 30).red() == 255);  // new: edit
  CHECK(frame.coverage.pixelColor(100, 65).red() == 255);  // new bottom: edit
  CHECK(frame.coverage.pixelColor(100, 30).red() == 0);    // original: keep
  QImage big(4000, 3000, QImage::Format_RGBA8888);
  big.fill(Qt::gray);
  const auto scaled = openai::expand_frame(big, QMargins(1000, 0, 1000, 0));
  CHECK(static_cast<std::int64_t>(scaled.context.width()) * scaled.context.height() <= openai::kExpandFramePixels + 4096);
  CHECK(std::abs(static_cast<double>(scaled.context.width()) / scaled.context.height() - 2.0) < 0.01);
  CHECK(openai::expand_frame(visible, QMargins(-1, 0, 0, 0)).context.isNull());
}

// The plain-file key store (Linux, the Flatpak): a Unicode path works, the file is owner-only
// where the file system has permissions, and clear() deletes it.
void ui_openai_file_key_store_keeps_the_key_owner_only() {
  const auto directory = patchy::test::unicode_artifact_dir(u8"openai-key-store");
  const auto cleanup = qScopeGuard([&] { patchy::test::remove_test_scratch_tree(directory.parent_path()); });
  const auto path = patchy::ui::to_qstring(directory / patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem));
  auto store = openai::make_file_key_store(path);
  CHECK(store->available() && store->unencrypted_file());
  CHECK(store->load().isEmpty());
  CHECK(store->save(QStringLiteral("sk-file-key\n")));
  CHECK(QFile::exists(path));
  CHECK(store->load() == QStringLiteral("sk-file-key"));
#if !defined(Q_OS_WIN)
  const auto permissions = QFile::permissions(path);
  CHECK((permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther |
                        QFileDevice::WriteOther)) == 0);
  CHECK((permissions & QFileDevice::ReadOwner) != 0 && (permissions & QFileDevice::WriteOwner) != 0);
#endif
  CHECK(store->save(QStringLiteral("sk-second")));
  CHECK(store->load() == QStringLiteral("sk-second"));
  CHECK(store->clear());
  CHECK(!QFile::exists(path));
  CHECK(store->clear());
  CHECK(openai::key_file_path().endsWith(QStringLiteral("/openai-api-key")));
}

// The dialog's own steps, run inside run_non_modal_dialog's loop; they end by closing it.
void run_openai_dialog_steps(patchy::ui::MainWindow& window, FakeApi& api, const std::shared_ptr<QString>& stored_key,
                             std::size_t layers_before) {
  auto* dialog = qobject_cast<patchy::ui::OpenAiImagesDialog*>(find_top_level_dialog(QStringLiteral("openAiImagesDialog")));
  CHECK(dialog != nullptr);
  if (dialog == nullptr) {
    return;
  }
  dialog->client().set_base_url(api.base_url());
  auto* key_edit = dialog->findChild<QLineEdit*>(QStringLiteral("openAiImagesKeyEdit"));
  auto* remember = dialog->findChild<QCheckBox*>(QStringLiteral("openAiImagesRememberCheck"));
  auto* prompt = dialog->findChild<QPlainTextEdit*>(QStringLiteral("openAiImagesPromptEdit"));
  auto* generate = dialog->findChild<QPushButton*>(QStringLiteral("openAiImagesGenerateButton"));
  auto* status = dialog->findChild<QLabel*>(QStringLiteral("openAiImagesStatusLabel"));
  auto* count_two = dialog->findChild<QToolButton*>(QStringLiteral("openAiImagesCount2"));
  auto* tabs = dialog->findChild<QTabWidget*>(QStringLiteral("openAiImagesModeTabs"));
  auto* models = dialog->findChild<QComboBox*>(QStringLiteral("openAiImagesModelCombo"));
  auto* references = dialog->findChild<QListWidget*>(QStringLiteral("openAiImagesReferenceList"));
  auto* paste = dialog->findChild<QPushButton*>(QStringLiteral("openAiImagesPasteReferenceButton"));
  auto* remove = dialog->findChild<QPushButton*>(QStringLiteral("openAiImagesRemoveReferenceButton"));
  const bool found = key_edit != nullptr && remember != nullptr && prompt != nullptr && generate != nullptr &&
                     status != nullptr && count_two != nullptr && tabs != nullptr && models != nullptr &&
                     references != nullptr && paste != nullptr && remove != nullptr;
  CHECK(found);
  if (!found) {
    dialog->reject();
    return;
  }
  CHECK(key_edit->echoMode() == QLineEdit::Password);
  CHECK(dialog->findChild<QToolButton*>(QStringLiteral("openAiImagesKeyStorageInfoButton")) != nullptr);
  (void)capture_widget_artifact("ui_openai_images_dialog", *dialog);
  // The dialog is shorter than the main window's smallest height on every tab, so it never
  // covers more than the window it belongs to (Seth, 2026-11-06).
  {
    const int main_minimum = window.minimumSizeHint().height();
    CHECK(dialog->height() < main_minimum);
    for (int index = 0; index < tabs->count(); ++index) {
      tabs->setCurrentIndex(index);
      for (int pass = 0; pass < 4; ++pass) {  // the deferred fit, layout passes, the text-size growth
        QApplication::processEvents();
      }
      CHECK(dialog->minimumSizeHint().height() < main_minimum);
      CHECK(dialog->height() < main_minimum);
      // Settled: further layout passes leave it alone (the fit and the growth agree).
      const int settled = dialog->height();
      for (int pass = 0; pass < 4; ++pass) {
        QApplication::processEvents();
      }
      CHECK(dialog->height() == settled);
      (void)capture_widget_artifact("ui_openai_images_dialog_tab" + std::to_string(index), *dialog);
    }
    tabs->setCurrentIndex(patchy::ui::OpenAiImagesDialog::CreateTab);
  }

  // One tab per operation, all enabled; the action button is named after the current one.
  CHECK(tabs->count() == 7);
  // The key, model and quality live on the last tab, Config, which has no action button.
  CHECK(tabs->tabText(patchy::ui::OpenAiImagesDialog::ConfigTab) == QStringLiteral("Config"));
  tabs->setCurrentIndex(patchy::ui::OpenAiImagesDialog::ConfigTab);
  CHECK(key_edit->isVisible() && !generate->isVisible());
  CHECK(dialog->findChild<QComboBox*>(QStringLiteral("openAiImagesModelCombo"))->isVisible());
  tabs->setCurrentIndex(patchy::ui::OpenAiImagesDialog::CreateTab);
  CHECK(!key_edit->isVisible() && generate->isVisible());
  for (int index = 0; index < tabs->count(); ++index) {
    CHECK(tabs->isTabEnabled(index));
  }
  CHECK(generate->text() == QStringLiteral("Create"));
  // Only the GPT Image 2.6 models, and a saved older model falls back to the default.
  CHECK(models->count() == 2);
  for (int row = 0; row < models->count(); ++row) {
    CHECK(models->itemData(row).toString().startsWith(QStringLiteral("gpt-image-2.6-")));
  }
  CHECK(models->currentData().toString() == openai::default_model());

  // Paste: greyed out without an image; the button and Ctrl+V add one; the prompt keeps text
  // paste and hands an image-only clipboard to the references.
  auto* clipboard = QApplication::clipboard();
  clipboard->clear();
  QApplication::processEvents();
  CHECK(!paste->isEnabled());
  CHECK(!dialog->paste_references_from_clipboard());
  clipboard->setImage(half_transparent_image());
  QApplication::processEvents();
  CHECK(paste->isEnabled());
  paste->click();
  CHECK(dialog->reference_count() == 1);
  // Window shortcuts fire only in the active window.
  dialog->activateWindow();
  CHECK(QTest::qWaitForWindowActive(dialog));
  references->setFocus();
  QTest::keyClick(references, Qt::Key_V, Qt::ControlModifier);
  CHECK(dialog->reference_count() == 2);
  // Patchy's hotkeys are application-wide; the dialog claims Paste, so nothing lands in the
  // document.
  CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size() == layers_before);
  prompt->setFocus();
  prompt->paste();
  CHECK(dialog->reference_count() == 3);
  CHECK(prompt->toPlainText().isEmpty());
  clipboard->setText(QStringLiteral("a red ball"));
  prompt->paste();
  CHECK(prompt->toPlainText() == QStringLiteral("a red ball"));
  CHECK(dialog->reference_count() == 3);
  references->selectAll();
  remove->click();
  CHECK(dialog->reference_count() == 0);
  clipboard->clear();
  prompt->clear();

  // References are numbered as the request sends them (the prompt says "image 2"), and the
  // numbers close up when one is removed.
  dialog->add_reference_for_testing(half_transparent_image(), QStringLiteral("Cat"));
  dialog->add_reference_for_testing(half_transparent_image(), QStringLiteral("Hat"));
  CHECK(references->item(0)->text() == QStringLiteral("1. Cat") && references->item(1)->text() == QStringLiteral("2. Hat"));
  CHECK(references->item(1)->toolTip().startsWith(QStringLiteral("Image 2: Hat")));
  references->clearSelection();
  references->item(0)->setSelected(true);
  remove->click();
  CHECK(dialog->reference_count() == 1 && references->item(0)->text() == QStringLiteral("1. Hat"));
  // The X in a thumbnail's top-right corner removes that reference.
  QApplication::processEvents();
  const auto hat_rect = references->visualItemRect(references->item(0));
  QTest::mouseClick(references->viewport(), Qt::LeftButton, Qt::NoModifier,
                    QPoint(hat_rect.right() - 9, hat_rect.top() + 10));
  CHECK(wait_until([&] { return dialog->reference_count() == 0; }, 2000));
  references->selectAll();
  remove->click();

  // Dropping image files from outside adds them in order (Unicode paths included); image data
  // dragged from a browser adds one; a text drag is refused.
  {
    const auto drop_directory = patchy::test::unicode_artifact_dir(u8"openai-reference-drops");
    const auto drop_cleanup = qScopeGuard([&] { patchy::test::remove_test_scratch_tree(drop_directory.parent_path()); });
    QList<QUrl> urls;
    for (const auto stem : patchy::test::kUnicodePathStems) {
      auto path = drop_directory / patchy::test::unicode_path_piece(stem);
      path += ".png";
      CHECK(half_transparent_image().save(patchy::ui::to_qstring(path), "PNG"));
      urls.push_back(QUrl::fromLocalFile(patchy::ui::to_qstring(path)));
    }
    const auto drop = [&](QMimeData& mime) {
      QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(references->viewport(), &enter);
      if (!enter.isAccepted()) {
        return false;
      }
      QDropEvent dropped(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(references->viewport(), &dropped);
      return dropped.isAccepted();
    };
    QMimeData files;
    files.setUrls(urls);
    CHECK(drop(files));
    CHECK(dialog->reference_count() == static_cast<int>(urls.size()));
    CHECK(references->item(0)->text().startsWith(QStringLiteral("1. ")));
    QMimeData image_data;
    image_data.setImageData(half_transparent_image());
    CHECK(drop(image_data));
    CHECK(dialog->reference_count() == static_cast<int>(urls.size()) + 1);
    QMimeData text_only;
    text_only.setText(QStringLiteral("not an image"));
    CHECK(!drop(text_only));
    CHECK(dialog->reference_count() == static_cast<int>(urls.size()) + 1);
    CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size() == layers_before);
  }
  references->selectAll();
  remove->click();
  CHECK(dialog->reference_count() == 0);

  // Nothing is sent without a prompt.
  key_edit->setText(QStringLiteral("sk-test-key"));
  generate->click();
  CHECK(api.requests.empty() && !dialog->busy());

  remember->setChecked(true);
  prompt->setPlainText(QStringLiteral("a red ball"));
  generate->click();
  // While the request runs, the action button says so and spins; it is back afterwards.
  CHECK(dialog->busy());
  CHECK(generate->text() == QStringLiteral("Creating...") && !generate->icon().isNull());
  (void)capture_widget_artifact("ui_openai_images_dialog_busy", *dialog);
  CHECK(wait_until([&] { return !dialog->busy() && !api.requests.empty(); }));
  CHECK(generate->text() == QStringLiteral("Create") && generate->icon().isNull());
  CHECK(*stored_key == QStringLiteral("sk-test-key"));
  CHECK(api.requests.size() == 1);
  const auto& first = api.requests.front();
  CHECK(first.method == "POST" && first.path == "/v1/images/generations");
  CHECK(first.headers.toLower().contains("authorization: bearer sk-test-key"));
  const auto body = QJsonDocument::fromJson(first.body).object();
  CHECK(body.value(QStringLiteral("prompt")).toString() == QStringLiteral("a red ball"));
  CHECK(body.value(QStringLiteral("background")).toString() == QStringLiteral("transparent"));
  CHECK(body.value(QStringLiteral("size")).toString() == openai::size_parameter(openai::fit_request_size({64, 48})));

  const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(doc.layers().size() == layers_before + 1);
  const auto* added = find_layer_named(doc, "OpenAI: a red ball");
  CHECK(added != nullptr);
  if (added != nullptr) {
    // 32x16 scaled to fit 64x48 is 64x32, centered vertically.
    CHECK(added->bounds().x == 0 && added->bounds().y == 8 && added->bounds().width == 64 && added->bounds().height == 32);
    const auto image = patchy::ui::display_qimage_from_pixel_buffer(added->pixels());
    CHECK(image.pixelColor(4, 16) == QColor(255, 0, 0, 255));
    CHECK(std::abs(image.pixelColor(60, 16).alpha() - 128) <= 1);
    CHECK(doc.active_layer_id() == added->id());
  }
  CHECK(status->text().contains(QStringLiteral("812")));
  patchy::ui::MainWindowTestAccess::undo(window);
  CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size() == layers_before);

  // References go to the edits endpoint as image[] parts, and no mask goes with them.
  dialog->add_reference_for_testing(half_transparent_image(), QStringLiteral("One"));
  dialog->add_reference_for_testing(half_transparent_image(), QStringLiteral("Two"));
  CHECK(dialog->reference_count() == 2);
  count_two->click();
  CHECK(dialog->image_count() == 2);
  api.reply_body = images_response(2);
  generate->click();
  CHECK(wait_until([&] { return !dialog->busy() && api.requests.size() == 2; }));
  const auto& edit = api.requests.back();
  CHECK(edit.path == "/v1/images/edits");
  CHECK(edit.headers.toLower().contains("content-type: multipart/form-data; boundary="));
  CHECK(edit.body.count("name=\"image[]\"") == 2);
  CHECK(!edit.body.contains("name=\"mask\""));
  CHECK(edit.body.contains("name=\"n\"\r\n\r\n2\r\n"));
  // Two variations make ONE layer; the Results row offers both and swaps the layer's pixels.
  CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size() == layers_before + 1);
  CHECK(dialog->result_count() == 2 && dialog->shown_variation() == 0);
  auto* second = dialog->findChild<QToolButton*>(QStringLiteral("openAiImagesResult2"));
  CHECK(second != nullptr && second->isVisible() && !second->icon().isNull());
  const auto shown_pixel = [&] {
    const auto* layer = find_layer_named(std::as_const(patchy::ui::MainWindowTestAccess::document(window)), "OpenAI: a red ball");
    return layer == nullptr ? QColor() : patchy::ui::display_qimage_from_pixel_buffer(layer->pixels()).pixelColor(4, 16);
  };
  CHECK(shown_pixel() == QColor(255, 0, 0, 255));
  const auto bounds_before_swap = [&] {
    const auto* layer = find_layer_named(std::as_const(patchy::ui::MainWindowTestAccess::document(window)), "OpenAI: a red ball");
    return layer == nullptr ? patchy::Rect{} : layer->bounds();
  }();
  if (second != nullptr) {
    second->click();
  }
  // The swap keeps the layer where it was (set_pixels alone would move it to 0, 0).
  if (const auto* swapped = find_layer_named(std::as_const(patchy::ui::MainWindowTestAccess::document(window)), "OpenAI: a red ball");
      swapped != nullptr) {
    CHECK(swapped->bounds().x == bounds_before_swap.x && swapped->bounds().y == bounds_before_swap.y);
    CHECK(swapped->bounds().y == 8);
  }
  CHECK(dialog->shown_variation() == 1);
  CHECK(shown_pixel() == QColor(0, 0, 255, 255));
  CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size() == layers_before + 1);
  CHECK(dialog->image_count() == 2);  // picking a variation leaves the count alone
  // Undo removes the result layer; its variations go with it.
  patchy::ui::MainWindowTestAccess::undo(window);
  CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size() == layers_before);
  CHECK(!dialog->show_variation(0));
  CHECK(dialog->result_count() == 0);

  // Edit: needs a selection; sends the region with a mask and no references (the Create tab
  // still holds two); the result is a new layer at the region, masked to the selection grown
  // and feathered as Edge blend says.
  CHECK(dialog->reference_count() == 2);
  if (auto* count_one = dialog->findChild<QToolButton*>(QStringLiteral("openAiImagesCount1")); count_one != nullptr) {
    count_one->click();
  }
  tabs->setCurrentIndex(patchy::ui::OpenAiImagesDialog::EditTab);
  CHECK(dialog->image_count() == 1);
  // The references show on Edit too, numbered from 2: the picture being edited is image 1.
  CHECK(references->isVisible());
  CHECK(references->item(0)->text() == QStringLiteral("2. One") && references->item(1)->text() == QStringLiteral("3. Two"));
  (void)capture_widget_artifact("ui_openai_images_dialog_edit_references", *dialog);
  // The thumbnail row keeps its full height: the theme's list rule would let it be squeezed.
  CHECK(references->height() >= 80);
  // The prompt field keeps a fixed height of a few lines; longer text scrolls.
  if (auto* edit_prompt = dialog->findChild<QPlainTextEdit*>(QStringLiteral("openAiImagesEditPromptEdit"));
      edit_prompt != nullptr) {
    CHECK(edit_prompt->minimumHeight() == edit_prompt->maximumHeight());
    CHECK(edit_prompt->height() < 12 * edit_prompt->fontMetrics().lineSpacing());
  } else {
    CHECK(!"no Edit prompt field");
  }
  CHECK(generate->text() == QStringLiteral("Edit"));
  auto* edge = dialog->findChild<QSlider*>(QStringLiteral("openAiImagesEdgeBlendSlider"));
  CHECK(edge != nullptr && edge->isVisible());
  if (edge != nullptr) {
    edge->setValue(50);
  }
  const auto requests_before_fill = api.requests.size();
  generate->click();
  // With nothing selected Edit takes the whole picture, but it needs a prompt then; the
  // button, which showed "Editing..." at the click, is back.
  CHECK(api.requests.size() == requests_before_fill && !dialog->busy());
  CHECK(status->text().contains(QStringLiteral("whole picture")));
  CHECK(generate->text() == QStringLiteral("Edit") && generate->icon().isNull());
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  patchy::PixelBuffer selection(10, 8, patchy::PixelFormat::gray8());
  std::fill(selection.data().begin(), selection.data().end(), std::uint8_t{255});
  canvas->set_selection_from_coverage(QRect(20, 16, 10, 8), selection);
  api.reply_body = images_response(1);
  const auto layers_before_fill = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size();
  generate->click();
  CHECK(wait_until([&] { return !dialog->busy() && api.requests.size() == requests_before_fill + 1; }));
  const auto& fill = api.requests.back();
  CHECK(fill.path == "/v1/images/edits");
  // The picture, then the two references (Seth allowed references on Edit, 2026-11-06).
  CHECK(fill.body.count("name=\"image[]\"") == 3);
  CHECK(fill.body.contains(openai::kEditReferencesNote));
  CHECK(fill.body.indexOf("filename=\"image.png\"") < fill.body.indexOf("filename=\"reference-1.png\""));
  // Edit marks the selection with markup (OpenAI's markup-guided edit), not a mask.
  CHECK(fill.body.count("name=\"markup\"") == 1);
  CHECK(!fill.body.contains("name=\"mask\""));
  CHECK(fill.body.contains("name=\"complexity\"\r\n\r\nlow\r\n"));
  CHECK(fill.body.contains("name=\"background\"\r\n\r\nopaque\r\n"));
  CHECK(fill.body.contains(QByteArray("name=\"prompt\"\r\n\r\n") + openai::kEditPromptWhenEmpty));
  const auto& filled_doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(filled_doc.layers().size() == layers_before_fill + 1);
  const auto edited_layer = [&] {
    return find_layer_named(std::as_const(patchy::ui::MainWindowTestAccess::document(window)), "OpenAI Edit");
  };
  // The mask's value just right of the selection (its right edge is x = 29) at document x.
  const auto mask_at = [&](int x) {
    const auto* layer = edited_layer();
    if (layer == nullptr || !layer->mask().has_value()) {
      return -1;
    }
    const auto& mask = *layer->mask();
    return static_cast<int>(mask.pixels.row(19 - mask.bounds.y)[static_cast<std::size_t>(x - mask.bounds.x)]);
  };
  const auto mask_feather = [&] {
    const auto* layer = edited_layer();
    return layer == nullptr || !layer->mask().has_value() ? -1.0 : layer->mask()->feather;
  };
  if (const auto* edited = edited_layer(); edited != nullptr) {
    // The layer covers what Edge blend can show: three 4 px bands past the selection.
    CHECK(edited->bounds().x == 20 - 12 && edited->bounds().y == 16 - 12 && edited->bounds().width == 10 + 24 &&
          edited->bounds().height == 8 + 24);
    CHECK(edited->mask().has_value() && edited->mask()->default_color == 0);
    // Edge blend 50%: the selection grown by one 4 px band, feathered by half a band.
    CHECK(mask_feather() == 2.0);
    CHECK(mask_at(29) == 255 && mask_at(33) == 255 && mask_at(35) == 0);
  } else {
    CHECK(!"no OpenAI Edit layer");
  }
  if (edge != nullptr) {
    // 0% is the selection as it is with no feather; 100% two bands with a one band feather.
    // The slider rewrites the mask in place, with no history step.
    const auto layer_count = patchy::ui::MainWindowTestAccess::document(window).layers().size();
    edge->setValue(0);
    CHECK(wait_until([&] { return mask_feather() == 0.0; }));
    CHECK(mask_at(29) == 255 && mask_at(30) == 0);
    edge->setValue(100);
    CHECK(wait_until([&] { return mask_feather() == 4.0; }));
    CHECK(mask_at(37) == 255 && mask_at(38) == 0);
    CHECK(patchy::ui::MainWindowTestAccess::document(window).layers().size() == layer_count);
    CHECK(patchy::ui::app_settings().value(QStringLiteral("openAiImages/edgeBlend")).toInt() == 100);
    edge->setValue(50);
    CHECK(wait_until([&] { return mask_feather() == 2.0; }));
  }

  // Remove: the fixed instruction, a layer named for it.
  tabs->setCurrentIndex(patchy::ui::OpenAiImagesDialog::RemoveTab);
  CHECK(generate->text() == QStringLiteral("Remove"));
  CHECK(!references->isVisible());  // Remove sends a mask, and never references with one
  generate->click();
  CHECK(wait_until([&] { return !dialog->busy() && api.requests.size() == requests_before_fill + 2; }));
  CHECK(api.requests.back().body.contains(QByteArray("name=\"prompt\"\r\n\r\n") + openai::kRemovePrompt));
  CHECK(api.requests.back().body.count("name=\"mask\"") == 1 && !api.requests.back().body.contains("name=\"markup\""));
  CHECK(api.requests.back().body.count("name=\"image[]\"") == 1);
  CHECK(find_layer_named(std::as_const(patchy::ui::MainWindowTestAccess::document(window)), "OpenAI Remove") != nullptr);
  CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size() == layers_before_fill + 2);

  // Target Active layer: the result goes into the Base layer inside the selection, as one undo
  // step, no new layer; its variations swap there too.
  auto* target = dialog->findChild<QComboBox*>(QStringLiteral("openAiImagesTargetCombo"));
  CHECK(target != nullptr && target->isVisible());
  if (target != nullptr) {
    target->setCurrentIndex(target->findData(QStringLiteral("active")));
    auto& live = patchy::ui::MainWindowTestAccess::document(window);
    const auto* base = find_layer_named(std::as_const(live), "Base");
    CHECK(base != nullptr);
    if (base != nullptr) {
      live.set_active_layer(base->id());
      const auto base_pixel = [&] {
        const auto* layer = find_layer_named(std::as_const(patchy::ui::MainWindowTestAccess::document(window)), "Base");
        return layer == nullptr ? QColor() : patchy::ui::display_qimage_from_pixel_buffer(layer->pixels()).pixelColor(24, 19);
      };
      CHECK(base_pixel() == QColor(255, 255, 255, 255));
      const auto layers_before_active = std::as_const(live).layers().size();
      if (auto* count_two_again = dialog->findChild<QToolButton*>(QStringLiteral("openAiImagesCount2"));
          count_two_again != nullptr) {
        count_two_again->click();
      }
      api.reply_body = checker_response(2);
      const auto requests_before_active = api.requests.size();
      generate->click();
      CHECK(wait_until([&] { return !dialog->busy() && api.requests.size() == requests_before_active + 1; }));
      CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size() == layers_before_active);
      const auto first_pixel = base_pixel();
      CHECK(first_pixel != QColor(255, 255, 255, 255));
      CHECK(dialog->result_count() == 2);
      CHECK(dialog->show_variation(1));
      CHECK(base_pixel() != first_pixel);
      // Edge blend recomposes the active layer: at 0% a pixel 2 px below the selection is the
      // original again, at 100% the result (a dark checker square there) reaches it.
      const auto beside_pixel = [&] {
        const auto* layer = find_layer_named(std::as_const(patchy::ui::MainWindowTestAccess::document(window)), "Base");
        return layer == nullptr ? QColor() : patchy::ui::display_qimage_from_pixel_buffer(layer->pixels()).pixelColor(24, 25);
      };
      if (edge != nullptr) {
        const auto inside_pixel = base_pixel();  // inside the selection the result shows at any amount
        edge->setValue(0);
        CHECK(wait_until([&] { return beside_pixel() == QColor(255, 255, 255, 255); }));
        CHECK(base_pixel() == inside_pixel);
        edge->setValue(100);
        CHECK(wait_until([&] { return beside_pixel() != QColor(255, 255, 255, 255); }));
        edge->setValue(50);
      }
      patchy::ui::MainWindowTestAccess::undo(window);
      CHECK(base_pixel() == QColor(255, 255, 255, 255));
      if (auto* count_one_again = dialog->findChild<QToolButton*>(QStringLiteral("openAiImagesCount1"));
          count_one_again != nullptr) {
        count_one_again->click();
      }
    }
    target->setCurrentIndex(target->findData(QStringLiteral("new")));
  }
  // Expand: needs pixels on a side; grows the canvas and adds a layer over it masked to the
  // new area only, both undone in one step.
  tabs->setCurrentIndex(patchy::ui::OpenAiImagesDialog::ExpandTab);
  CHECK(generate->text() == QStringLiteral("Expand"));
  const auto requests_before_expand = api.requests.size();
  generate->click();
  CHECK(api.requests.size() == requests_before_expand && status->text().contains(QStringLiteral("at least one side")));
  auto* left = dialog->findChild<QSpinBox*>(QStringLiteral("openAiImagesExpandLeft"));
  auto* right = dialog->findChild<QSpinBox*>(QStringLiteral("openAiImagesExpandRight"));
  auto* readout = dialog->findChild<QLabel*>(QStringLiteral("openAiImagesExpandReadout"));
  CHECK(left != nullptr && right != nullptr && readout != nullptr);
  auto* view = dynamic_cast<patchy::ui::OpenAiExpandFrameView*>(dialog->findChild<QWidget*>(QStringLiteral("openAiImagesExpandView")));
  CHECK(view != nullptr);
  if (left != nullptr && right != nullptr && readout != nullptr && view != nullptr) {
    // Dragging the right edge outward adds pixels on the right; dragging it inward stops at
    // the image (Expand never crops).
    QApplication::processEvents();  // lay out the newly shown tab
    const auto frame = view->frame_rect();
    const QPoint right_edge(static_cast<int>(frame.right()), static_cast<int>(frame.center().y()));
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, right_edge);
    QTest::mouseMove(view, right_edge + QPoint(30, 0));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, right_edge + QPoint(30, 0));
    CHECK(right->value() > 0 && left->value() == 0);
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier,
                      QPoint(static_cast<int>(view->frame_rect().right()), static_cast<int>(frame.center().y())));
    QTest::mouseMove(view, QPoint(5, static_cast<int>(frame.center().y())));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(5, static_cast<int>(frame.center().y())));
    CHECK(right->value() == 0);
    left->setValue(16);
    right->setValue(16);
    CHECK(view->margins() == QMargins(16, 0, 16, 0));
    CHECK(readout->text().contains(QStringLiteral("96 x 48")));
    (void)capture_widget_artifact("ui_openai_images_dialog_expand", *dialog);
    const auto layers_before_expand = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size();
    generate->click();
    CHECK(wait_until([&] { return !dialog->busy() && api.requests.size() == requests_before_expand + 1; }));
    const auto& expand = api.requests.back();
    CHECK(expand.path == "/v1/images/edits");
    // The frame alone at low complexity: no mask (2.6 drew an unrelated strip with one).
    CHECK(!expand.body.contains("name=\"mask\"") && !expand.body.contains("name=\"markup\""));
    CHECK(expand.body.count("name=\"image[]\"") == 1);
    CHECK(expand.body.contains("name=\"complexity\"\r\n\r\nlow\r\n"));
    CHECK(expand.body.contains(QByteArray("name=\"prompt\"\r\n\r\n") + openai::kExpandPromptWhenEmpty));
    const auto& grown = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    CHECK(grown.width() == 96 && grown.height() == 48);
    // Applied: the side fields clear, so the frame shows the grown canvas with no new area.
    CHECK(left->value() == 0 && right->value() == 0);
    CHECK(view->margins() == QMargins());
    CHECK(readout->text().contains(QStringLiteral("96 x 48")));
    CHECK(grown.layers().size() == layers_before_expand + 1);
    if (const auto* expanded = find_layer_named(grown, "OpenAI Expand"); expanded != nullptr) {
      CHECK(expanded->bounds().x == 0 && expanded->bounds().width == 96 && expanded->bounds().height == 48);
      // No mask: the original's area of the layer is transparent, so it shows through.
      CHECK(!expanded->mask().has_value());
      const auto pixels = patchy::ui::display_qimage_from_pixel_buffer(expanded->pixels());
      CHECK(pixels.pixelColor(5, 10).alpha() == 255);   // the new left strip
      CHECK(pixels.pixelColor(48, 10).alpha() == 0);    // the original stays as it was
      // Blend 25%: OpenAI's version covers a quarter of the deepest an original pixel can lie
      // from new area (32 px here, both sides grew), so 8 px, then fades over 8 px.
      CHECK(pixels.pixelColor(16, 10).alpha() == 255);
      CHECK(pixels.pixelColor(20, 10).alpha() == 255);
      CHECK(pixels.pixelColor(28, 10).alpha() > 60 && pixels.pixelColor(28, 10).alpha() < 200);
      CHECK(pixels.pixelColor(32, 10).alpha() == 0);
      CHECK(pixels.pixelColor(90, 10).alpha() == 255);  // the new right strip
    } else {
      CHECK(!"no OpenAI Expand layer");
    }
    // The Blend slider re-prepares the result in place: 0% fades right at the old edge, 100%
    // is OpenAI's whole picture.
    const auto expanded_alpha = [&](int x) {
      const auto* layer = find_layer_named(std::as_const(patchy::ui::MainWindowTestAccess::document(window)), "OpenAI Expand");
      return layer == nullptr ? -1 : patchy::ui::display_qimage_from_pixel_buffer(layer->pixels()).pixelColor(x, 10).alpha();
    };
    auto* expand_blend = dialog->findChild<QSlider*>(QStringLiteral("openAiImagesExpandBlendSlider"));
    CHECK(expand_blend != nullptr && expand_blend->value() == 25);
    if (expand_blend != nullptr) {
      expand_blend->setValue(0);
      CHECK(wait_until([&] { return expanded_alpha(20) > 60 && expanded_alpha(20) < 200; }));
      CHECK(expanded_alpha(28) == 0);
      expand_blend->setValue(100);
      CHECK(wait_until([&] { return expanded_alpha(48) == 255; }));
      expand_blend->setValue(25);
    }
    patchy::ui::MainWindowTestAccess::undo(window);
    const auto& undone = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    CHECK(undone.width() == 64 && undone.height() == 48);
    CHECK(undone.layers().size() == layers_before_expand);
    left->setValue(0);
    right->setValue(0);
  }
  // Animate: needs a prompt; from the prompt alone it posts a GIF generation, and the GIF
  // opens as a new document with a layer per frame, frame 1 on top, delays in the names.
  tabs->setCurrentIndex(patchy::ui::OpenAiImagesDialog::AnimateTab);
  CHECK(generate->text() == QStringLiteral("Animate"));
  auto* animate_prompt = dialog->findChild<QPlainTextEdit*>(QStringLiteral("openAiImagesAnimatePromptEdit"));
  auto* frames_spin = dialog->findChild<QSpinBox*>(QStringLiteral("openAiImagesAnimateFramesSpin"));
  auto* delay_spin = dialog->findChild<QSpinBox*>(QStringLiteral("openAiImagesAnimateDelaySpin"));
  auto* source = dialog->findChild<QComboBox*>(QStringLiteral("openAiImagesAnimateSourceCombo"));
  CHECK(animate_prompt != nullptr && frames_spin != nullptr && delay_spin != nullptr && source != nullptr);
  if (animate_prompt != nullptr && frames_spin != nullptr && delay_spin != nullptr && source != nullptr) {
    CHECK(references->isVisible());
    references->selectAll();
    remove->click();
    CHECK(dialog->reference_count() == 0);
    const auto requests_before_animate = api.requests.size();
    generate->click();
    CHECK(api.requests.size() == requests_before_animate);
    animate_prompt->setPlainText(QStringLiteral("a moving red line"));
    // More than 9 frames grey out the largest size and step down from it.
    auto* size_combo = dialog->findChild<QComboBox*>(QStringLiteral("openAiImagesAnimateSizeCombo"));
    CHECK(size_combo != nullptr);
    if (size_combo != nullptr) {
      size_combo->setCurrentIndex(size_combo->findData(960));
      frames_spin->setValue(12);
      CHECK(size_combo->currentData().toInt() == 720);
      frames_spin->setValue(9);
      size_combo->setCurrentIndex(size_combo->findData(960));
      CHECK(size_combo->currentData().toInt() == 960);
      size_combo->setCurrentIndex(0);
    }
    frames_spin->setValue(3);
    delay_spin->setValue(100);
    (void)capture_widget_artifact("ui_openai_images_dialog_animate", *dialog);
    api.reply_body = gif_response();
    generate->click();
    CHECK(wait_until([&] { return !dialog->busy() && api.requests.size() == requests_before_animate + 1; }));
    const auto gif_body = QJsonDocument::fromJson(api.requests.back().body).object();
    CHECK(api.requests.back().path == "/v1/images/generations");
    CHECK(gif_body.value(QStringLiteral("output_format")).toString() == QStringLiteral("gif"));
    CHECK(gif_body.value(QStringLiteral("frame_count")).toInt() == 3);
    CHECK(gif_body.value(QStringLiteral("frame_duration_ms")).toInt() == 100);
    CHECK(gif_body.value(QStringLiteral("size")).toString() == QStringLiteral("512x512"));
    // The prompt goes inside fixed sprite-sheet instructions, as OpenAI's own GIF examples write.
    const auto sent_prompt = gif_body.value(QStringLiteral("prompt")).toString();
    CHECK(sent_prompt.contains(QStringLiteral("exactly 3 sequential frames")));
    CHECK(sent_prompt.endsWith(QStringLiteral("a moving red line")));
    const auto& animation = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    CHECK(animation.width() == 16 && animation.height() == 8);
    CHECK(animation.layers().size() == 3);
    if (animation.layers().size() == 3) {
      // Frame 1 is the top layer (the last in the bottom-to-top list).
      CHECK(animation.layers().back().name() == "Frame 1 0.1s");
      CHECK(animation.layers().front().name() == "Frame 3 0.3s");
      for (const auto& frame_layer : animation.layers()) {
        CHECK(frame_layer.visible());
      }
    }
    // From the active layer with a reference: a GIF edit with that layer first, the reference
    // after it (numbered 2 on the tab), and no mask.
    dialog->add_reference_for_testing(half_transparent_image(), QStringLiteral("Style"));
    CHECK(references->item(0)->text() == QStringLiteral("1. Style"));
    source->setCurrentIndex(source->findData(QStringLiteral("layer")));
    CHECK(references->item(0)->text() == QStringLiteral("2. Style"));
    generate->click();
    CHECK(wait_until([&] { return !dialog->busy() && api.requests.size() == requests_before_animate + 2; }));
    const auto& from_layer = api.requests.back();
    CHECK(from_layer.path == "/v1/images/edits");
    CHECK(from_layer.body.count("name=\"image[]\"") == 2);
    CHECK(from_layer.body.contains("Animate the first provided image"));
    CHECK(!from_layer.body.contains("name=\"mask\""));
    CHECK(from_layer.body.contains("name=\"output_format\"\r\n\r\ngif\r\n"));
    source->setCurrentIndex(0);
    CHECK(references->item(0)->text() == QStringLiteral("1. Style"));
    references->selectAll();
    remove->click();

    // Full color: an ordinary PNG request for a 2 x 2 sheet of 512 px frames, cut into a new
    // document with one layer per frame, frame 1 on top. GIF stays the default.
    auto* colors = dialog->findChild<QComboBox*>(QStringLiteral("openAiImagesAnimateColorsCombo"));
    auto* frame_size = dialog->findChild<QComboBox*>(QStringLiteral("openAiImagesAnimateSizeCombo"));
    CHECK(colors != nullptr && frame_size != nullptr);
    if (colors != nullptr && frame_size != nullptr) {
      CHECK(colors->currentData().toString() == QStringLiteral("gif"));
      colors->setCurrentIndex(colors->findData(QStringLiteral("sheet")));
      frames_spin->setValue(4);
      frame_size->setCurrentIndex(frame_size->findData(512));
      QImage sheet(1024, 1024, QImage::Format_RGBA8888);
      const QColor quadrant[] = {Qt::red, Qt::green, Qt::blue, Qt::yellow};
      {
        QPainter painter(&sheet);
        for (int index = 0; index < 4; ++index) {
          painter.fillRect(QRect((index % 2) * 512, (index / 2) * 512, 512, 512), quadrant[index]);
        }
      }
      api.reply_body = QJsonDocument(QJsonObject{{"created", 1},
                                                 {"data", QJsonArray{QJsonObject{
                                                              {"b64_json", QString::fromLatin1(png_of(sheet).toBase64())}}}}})
                           .toJson(QJsonDocument::Compact);
      const auto requests_before_sheet = api.requests.size();
      generate->click();
      CHECK(wait_until([&] { return !dialog->busy() && api.requests.size() == requests_before_sheet + 1; }));
      const auto sheet_request = QJsonDocument::fromJson(api.requests.back().body).object();
      CHECK(api.requests.back().path == "/v1/images/generations");
      CHECK(sheet_request.value(QStringLiteral("output_format")).toString() == QStringLiteral("png"));
      CHECK(sheet_request.value(QStringLiteral("size")).toString() == QStringLiteral("1024x1024"));
      CHECK(!sheet_request.contains(QStringLiteral("frame_count")));
      CHECK(sheet_request.value(QStringLiteral("prompt")).toString().contains(QStringLiteral("2 columns and 2 rows")));
      const auto& sheet_doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
      CHECK(sheet_doc.width() == 512 && sheet_doc.height() == 512);
      CHECK(sheet_doc.layers().size() == 4);
      if (sheet_doc.layers().size() == 4) {
        CHECK(sheet_doc.layers().back().name().rfind("Frame 1", 0) == 0);  // frame 1 on top
        const auto top = patchy::ui::display_qimage_from_pixel_buffer(sheet_doc.layers().back().pixels());
        CHECK(top.pixelColor(10, 10) == QColor(Qt::red));
        const auto bottom = patchy::ui::display_qimage_from_pixel_buffer(sheet_doc.layers().front().pixels());
        CHECK(bottom.pixelColor(10, 10) == QColor(Qt::yellow));
      }
      colors->setCurrentIndex(colors->findData(QStringLiteral("gif")));
      frames_spin->setValue(3);
    }
  }

  // Describe: the visible image (previewed on the tab) goes to the text model through the
  // Responses API, and the reply fills the tab's read-only box; nothing reaches the document
  // or any prompt field. The image model, quality and variations rows hide on this tab.
  tabs->setCurrentIndex(patchy::ui::OpenAiImagesDialog::DescribeTab);
  CHECK(generate->text() == QStringLiteral("Describe Image"));
  CHECK(!references->isVisible());
  auto* model_combo = dialog->findChild<QComboBox*>(QStringLiteral("openAiImagesModelCombo"));
  CHECK(model_combo != nullptr && !model_combo->isVisible());  // on the Config tab
  auto* preview = dialog->findChild<QLabel*>(QStringLiteral("openAiImagesDescribePreview"));
  CHECK(preview != nullptr && !preview->pixmap().isNull());
  auto* described = dialog->findChild<QPlainTextEdit*>(QStringLiteral("openAiImagesDescribeText"));
  CHECK(described != nullptr && described->isReadOnly());
  const auto create_prompt_before = prompt->toPlainText();
  const auto layers_before_describe = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size();
  api.reply_body = R"({"status": "completed", "output": [{"type": "reasoning", "summary": []},
      {"type": "message", "role": "assistant", "content": [{"type": "output_text", "text": "A small room with a red ball."}]}],
      "usage": {"input_tokens": 900, "output_tokens": 100, "total_tokens": 1000}})";
  const auto requests_before_describe = api.requests.size();
  generate->click();
  CHECK(generate->text() == QStringLiteral("Describing..."));
  CHECK(wait_until([&] { return !dialog->busy() && api.requests.size() == requests_before_describe + 1; }));
  const auto& describe_request = api.requests.back();
  CHECK(describe_request.path == "/v1/responses");
  const auto describe_json = QJsonDocument::fromJson(describe_request.body).object();
  CHECK(describe_json.value(QStringLiteral("model")).toString() == QString::fromLatin1(openai::kDescribeModel));
  CHECK(describe_json.value(QStringLiteral("store")).toBool(true) == false);
  CHECK(describe_request.body.contains("data:image/png;base64,"));
  if (described != nullptr) {
    CHECK(described->toPlainText() == QStringLiteral("A small room with a red ball."));
  }
  CHECK(status->text().contains(QStringLiteral("1,000")) || status->text().contains(QStringLiteral("1000")) ||
        status->text().contains(QStringLiteral("tokens")));
  CHECK(prompt->toPlainText() == create_prompt_before);
  CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size() == layers_before_describe);
  CHECK(generate->text() == QStringLiteral("Describe Image"));
  (void)capture_widget_artifact("ui_openai_images_dialog_describe", *dialog);
  // Ctrl+A then Ctrl+C in the read-only box copy its text, not the canvas (Patchy's hotkeys
  // are application-wide), and Ctrl+V in the Create prompt pastes that text, not an image.
  dialog->activateWindow();
  CHECK(QTest::qWaitForWindowActive(dialog));
  const auto selection_before_keys = canvas->selected_document_rect();
  clipboard->clear();
  if (described != nullptr) {
    described->setFocus();
    QTest::keyClick(described, Qt::Key_A, Qt::ControlModifier);
    CHECK(described->textCursor().selectedText() == QStringLiteral("A small room with a red ball."));
    QTest::keyClick(described, Qt::Key_C, Qt::ControlModifier);
  }
  CHECK(clipboard->text() == QStringLiteral("A small room with a red ball."));
  CHECK(clipboard->mimeData() != nullptr && !clipboard->mimeData()->hasImage());
  CHECK(canvas->selected_document_rect() == selection_before_keys);
  tabs->setCurrentIndex(patchy::ui::OpenAiImagesDialog::CreateTab);
  CHECK(model_combo != nullptr && !model_combo->isVisible());
  const auto references_before_paste = dialog->reference_count();
  prompt->clear();
  prompt->setFocus();
  QTest::keyClick(prompt, Qt::Key_V, Qt::ControlModifier);
  CHECK(prompt->toPlainText() == QStringLiteral("A small room with a red ball."));
  CHECK(dialog->reference_count() == references_before_paste);
  CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size() == layers_before_describe);
  prompt->setPlainText(create_prompt_before);
  clipboard->clear();

  // Edit with nothing selected: the whole picture and the prompt, with no outline or mask;
  // the result is a plain layer over the whole canvas.
  canvas->clear_selection();
  tabs->setCurrentIndex(patchy::ui::OpenAiImagesDialog::EditTab);
  auto* edit_prompt_field = dialog->findChild<QPlainTextEdit*>(QStringLiteral("openAiImagesEditPromptEdit"));
  CHECK(edit_prompt_field != nullptr);
  if (edit_prompt_field != nullptr) {
    edit_prompt_field->setPlainText(QStringLiteral("make it night"));
    api.reply_body = images_response(1);
    const auto requests_before_whole = api.requests.size();
    const auto layers_before_whole = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size();
    generate->click();
    CHECK(wait_until([&] { return !dialog->busy() && api.requests.size() == requests_before_whole + 1; }));
    const auto& whole = api.requests.back();
    CHECK(whole.path == "/v1/images/edits");
    CHECK(!whole.body.contains("name=\"markup\"") && !whole.body.contains("name=\"mask\""));
    CHECK(whole.body.contains(QByteArray("make it night\n\n") + openai::kEditWholeInstructions));
    const auto& whole_doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    CHECK(whole_doc.layers().size() == layers_before_whole + 1);
    if (const auto* layer = find_layer_named(whole_doc, "OpenAI Edit: make it night"); layer != nullptr) {
      CHECK(layer->bounds().x == 0 && layer->bounds().y == 0 && layer->bounds().width == whole_doc.width() &&
            layer->bounds().height == whole_doc.height());
      CHECK(!layer->mask().has_value());
    } else {
      CHECK(!"no whole-picture OpenAI Edit layer");
    }
    patchy::ui::MainWindowTestAccess::undo(window);
    edit_prompt_field->clear();
  }
  tabs->setCurrentIndex(patchy::ui::OpenAiImagesDialog::CreateTab);

  // Stop while the request is still being prepared (its body is built on a worker thread):
  // nothing is sent and the dialog is free again.
  {
    auto* stop = dialog->findChild<QPushButton*>(QStringLiteral("openAiImagesStopButton"));
    CHECK(stop != nullptr);
    if (stop != nullptr) {
      const auto requests_before_stop = api.requests.size();
      generate->click();
      CHECK(dialog->busy() && generate->text() == QStringLiteral("Creating..."));
      stop->click();
      CHECK(!dialog->busy());
      QTest::qWait(300);
      CHECK(api.requests.size() == requests_before_stop);
      CHECK(generate->text() == QStringLiteral("Create"));
    }
  }

  // An API error is shown in the dialog and adds nothing.
  api.status = 401;
  api.reply_body = R"({"error": {"message": "Incorrect API key provided.", "code": "invalid_api_key"}})";
  const auto requests_before_error = api.requests.size();
  const auto layers_before_error = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size();
  generate->click();
  CHECK(wait_until([&] { return !dialog->busy() && api.requests.size() == requests_before_error + 1; }));
  CHECK(status->text().contains(QStringLiteral("did not accept this API key")));
  CHECK(status->text().contains(QStringLiteral("https://platform.openai.com/api-keys")));
  CHECK(generate->text() == QStringLiteral("Create"));
  CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size() == layers_before_error);

  // Unticking Remember forgets the stored key.
  remember->setChecked(false);
  CHECK(stored_key->isEmpty());
  dialog->reject();
}

// The whole path: the menu opens the dialog, Generate posts to the API with the key, and the
// returned images land as layers (scaled to the canvas, alpha kept) in one undo step. With
// references the request goes to the edits endpoint without a mask; an error shows in the
// dialog; the key is remembered through the store.
void ui_openai_images_dialog_adds_generated_layers() {
  SettingsGroupRestorer settings_restore(QStringLiteral("openAiImages"));
  // At a text size other than 100% dialogs grow to their layout's height after each layout
  // change; the per-tab height must hold there too (the stale duplicated controls Seth saw).
  patchy::ui::set_ui_text_size_percent(115);
  const auto restore_text_size = qScopeGuard([] { patchy::ui::set_ui_text_size_percent(100); });
  auto stored_key = std::make_shared<QString>();
  openai::set_key_store_factory_for_testing([stored_key] { return std::make_unique<MemoryKeyStore>(stored_key); });
  const auto restore_store = qScopeGuard([] { openai::set_key_store_factory_for_testing(nullptr); });
  FakeApi api;
  CHECK(api.start());
  api.reply_body = images_response(1);

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Base", solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(255, 255, 255, 255)));
  window.add_document_session(std::move(document), QStringLiteral("OpenAI"));
  const auto layers_before = patchy::ui::MainWindowTestAccess::document(window).layers().size();

  auto* action = window.findChild<QAction*>(QStringLiteral("pluginsOpenAiImagesAction"));
  CHECK(action != nullptr);
  if (action == nullptr) {
    return;
  }
  // The built-in command heads Plugins > Companions and survives the submenu's rebuild.
  auto* companions = window.findChild<patchy::ui::CompanionMenu*>();
  CHECK(companions != nullptr);
  if (companions != nullptr) {
    emit companions->menu()->aboutToShow();
    CHECK(!companions->menu()->actions().isEmpty() && companions->menu()->actions().front() == action);
    CHECK(action->text() == QStringLiteral("OpenAI Image Tools via API Key (built-in)..."));
  }
  // A model saved by an older build, which the dialog no longer offers.
  patchy::ui::app_settings().setValue(QStringLiteral("openAiImages/model"), QStringLiteral("gpt-image-2"));
  // run_non_modal_dialog keeps a nested loop until the dialog closes, so the steps run from it.
  bool steps_ran = false;
  QTimer::singleShot(0, guarded([&] {
    run_openai_dialog_steps(window, api, stored_key, layers_before);
    steps_ran = true;
  }));
  action->trigger();
  CHECK(steps_ran);
}

// The info mark's note names where this platform keeps a remembered key.
void ui_openai_key_storage_note_names_the_platform_store() {
  const auto note = patchy::ui::OpenAiImagesDialog::key_storage_note();
#if defined(Q_OS_WIN)
  CHECK(note.contains(QStringLiteral("Credential Manager")));
#elif defined(Q_OS_MACOS)
  CHECK(note.contains(QStringLiteral("keychain")));
#else
  CHECK(note.contains(QStringLiteral("OPENAI_API_KEY")));
#endif
  CHECK(note.contains(QStringLiteral("settings file")));
}

}  // namespace

std::vector<patchy::test::TestCase> openai_images_tests() {
  return {
      {"ui_openai_request_size_follows_the_canvas_within_api_limits",
       ui_openai_request_size_follows_the_canvas_within_api_limits},
      {"ui_openai_request_bodies_send_references_without_a_mask", ui_openai_request_bodies_send_references_without_a_mask},
      {"ui_openai_responses_keep_alpha_usage_and_errors", ui_openai_responses_keep_alpha_usage_and_errors},
      {"ui_openai_reference_files_open_from_unicode_paths", ui_openai_reference_files_open_from_unicode_paths},
      {"ui_openai_images_dialog_adds_generated_layers", ui_openai_images_dialog_adds_generated_layers},
      {"ui_openai_key_storage_note_names_the_platform_store", ui_openai_key_storage_note_names_the_platform_store},
      {"ui_openai_masked_edit_images_follow_the_selection", ui_openai_masked_edit_images_follow_the_selection},
      {"ui_openai_describe_request_and_reply", ui_openai_describe_request_and_reply},
      {"ui_openai_file_key_store_keeps_the_key_owner_only", ui_openai_file_key_store_keeps_the_key_owner_only},
      {"ui_openai_expand_frame_marks_only_the_new_area", ui_openai_expand_frame_marks_only_the_new_area},
      {"ui_openai_gif_requests_and_frames", ui_openai_gif_requests_and_frames},
      {"ui_openai_seam_helpers_dilate_feather_and_match_tone", ui_openai_seam_helpers_dilate_feather_and_match_tone},
  };
}
