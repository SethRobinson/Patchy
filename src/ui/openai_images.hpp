#pragma once
// OpenAI Image Tools (docs/openai-images.md): request building, response parsing, request
// sizing, the API key store and the network client behind Plugins > OpenAI Image Tools.
// Generation runs on OpenAI's servers; Patchy sends the user's own prompt and pixels and
// inserts what comes back as plain pixel layers. The legal rules this code keeps are in
// docs/legal-constraints.md ("Prompt-driven image generation"): no prompt is ever made up
// from image content or metadata, and no mask is derived from a result. Requests carry no
// mask; combining one with references or transparency is an open decision recorded there.
#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QMargins>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace patchy::ui::openai {

// The edits endpoint takes up to 16 input images.
inline constexpr int kMaxReferenceImages = 16;
// The dialog's ceiling on images per request (the API allows 10; each one is billed).
inline constexpr int kMaxImagesPerRequest = 4;
// Request sizes for the GPT image models: both edges divisible by 16, aspect between 1:3
// and 3:1, at most 3840 px on the long edge and 3840x2160 in area. The minimum pixel
// budget is not published: 1024x512 was refused and 1024x640 accepted (probed
// 2026-11-06), so the floor here is 1024x640.
inline constexpr int kRequestSizeStep = 16;
inline constexpr int kMaxRequestEdge = 3840;
inline constexpr std::int64_t kMaxRequestPixels = 3840LL * 2160LL;
inline constexpr std::int64_t kMinRequestPixels = 1024LL * 640LL;
// What a "Match canvas" request aims for: about one megapixel, up to 1536x1024's area, so a
// request costs what a standard size costs. The result is scaled to the canvas.
inline constexpr std::int64_t kMatchCanvasMinPixels = 1024LL * 1024LL;
inline constexpr std::int64_t kMatchCanvasMaxPixels = 1536LL * 1024LL;
// Masked edits (Edit, Remove, Expand) go out at the region's own resolution, up to the
// largest non-experimental size (2560x1440's area), so the edited area is not softened by a
// down-and-up scale.
inline constexpr std::int64_t kMaskedMaxPixels = 2560LL * 1440LL;

// Every request asks for OpenAI's less restrictive content filtering, the lowest level the
// API offers ("low"; the API default is "auto", and there is no "off") (Seth, 2026-11-06).
inline constexpr char kModeration[] = "low";

[[nodiscard]] QString default_model();
[[nodiscard]] QUrl default_base_url();

struct GenerateRequest {
  QString model;
  QString prompt;
  QSize size;                 // empty: "auto"
  QString quality{QStringLiteral("auto")};      // auto, low, medium, high
  QString background{QStringLiteral("auto")};   // auto, transparent, opaque
  int count{1};
  // Optional reference images: sent through the edits endpoint with the prompt, without a
  // mask (docs/legal-constraints.md, "Prompt-driven image generation", lists the open decision).
  std::vector<QImage> references;
  // Edit and Remove: the image to edit at the request size, with either a mask (Remove:
  // transparent where the selection is) or a markup overlay (Edit: a red outline around the
  // selection on a transparent image, OpenAI's markup-guided edit), never both. Either way the
  // request carries no references.
  QImage edit_image;
  QImage edit_mask;
  QImage edit_markup;
  // "low", "medium" or "high"; empty leaves it out (the model's default, high).
  QString complexity;
  // Animate: GIF output. frame_count 2 to 16, frame_duration_ms 20 to 2000 (rounded to 10 ms
  // by the API). A GIF request carries no mask.
  bool animated_gif{false};
  int frame_count{9};
  int frame_duration_ms{200};
};

struct GenerateResult {
  std::vector<QImage> images;  // Format_RGBA8888, alpha kept (a GIF's first frame)
  std::vector<QByteArray> encoded;  // each image as the API sent it (PNG or GIF bytes)
  std::int64_t input_tokens{0};
  std::int64_t output_tokens{0};
  std::int64_t total_tokens{0};
};

// Edit and Remove (docs/openai-images.md#edit-and-remove). The fixed instructions: constant
// English text, never made from the image. An Edit sends the user's prompt followed by
// kEditOutlineInstructions, or kEditPromptWhenEmpty alone; a Remove sends kRemovePrompt.
inline constexpr char kEditOutlineInstructions[] =
    "Make this change only inside the area outlined in red; keep everything outside it exactly as it is. "
    "Remove the red outline from the finished image.";
// Appended to an Edit's prompt when reference images go with it (Seth allowed them,
// 2026-11-06; docs/legal-constraints.md, "Prompt-driven image generation").
inline constexpr char kEditReferencesNote[] =
    "Image 1 is the picture to edit; the other images are references, numbered from image 2.";
// Appended to an Edit of the whole picture (no selection, or all of it selected): no outline
// is sent then.
inline constexpr char kEditWholeInstructions[] =
    "Change only what this asks for; keep everything else in the picture exactly as it is.";
inline constexpr char kEditPromptWhenEmpty[] =
    "Redraw the area outlined in red so it continues the surrounding image naturally, with matching lighting, "
    "texture and perspective. Keep everything outside it exactly as it is. Remove the red outline from the "
    "finished image.";
inline constexpr char kRemovePrompt[] =
    "Remove what is in the masked area and fill it with the background that would naturally be behind it, "
    "matching the surrounding image. Keep everything else unchanged.";
// Edit and Remove ask for low complexity: OpenAI's guidance for focused edits, where the
// default (high) plans more and can change more than was asked.
inline constexpr char kMaskedEditComplexity[] = "low";

// Describe (docs/openai-images-tabs.md#describe): the image goes to a text model through the
// Responses API with this fixed instruction, and the reply is shown as text. Patchy never puts
// the description into a prompt or a generation request (docs/legal-constraints.md,
// "Prompt-driven image generation"), and describes only the image as it is now.
inline constexpr char kDescribeModel[] = "gpt-6.1-sol";
inline constexpr char kDescribeInstructions[] =
    "Describe this image in detail and accurately. Cover the overall scene and setting; each person (apparent age "
    "range, build, hair, clothing, pose, expression and what they are doing, without guessing who they are); the "
    "objects and where they are; any readable text, quoted exactly; the lighting and colors; and the photographic or "
    "artistic style. Describe only what is visible. Write plain prose in English.";
inline constexpr int kDescribeMaxEdge = 1536;  // the image is scaled down to this, never up

// Expand sends the grown frame with no mask at low complexity, and this fixed text after the
// user's prompt (or alone): with a mask and the default complexity, 2.6 drew a new strip that
// did not continue the picture (Seth's expand test and the live probes, 2026-11-06).
inline constexpr char kExpandPromptWhenEmpty[] =
    "Extend the picture into the empty transparent area around it, continuing the scene naturally with matching "
    "lighting, texture and perspective. Keep the existing picture exactly as it is, at the same position and scale; "
    "do not move, zoom, crop or redraw it.";
inline constexpr char kExpandKeepInstructions[] =
    "Keep the existing picture exactly as it is, at the same position and scale; do not move, zoom, crop or redraw "
    "it. Continue the scene into the empty transparent area around it with matching lighting, texture and "
    "perspective.";

// Expand: the frame sent for the grown canvas, the visible image at (left, top) on a
// transparent canvas grown by `margins`, scaled down so it is at most kExpandFramePixels,
// and its coverage (255 in the new area, 0 over the original), for masked_edit_images. The
// canvas size comes only from the user's margins; Expand never crops or rotates
// (docs/legal-constraints.md, "Prompt-driven image generation").
inline constexpr std::int64_t kExpandFramePixels = 2048LL * 2048LL;
struct ExpandFrame {
  QImage context;
  QImage coverage;
};
[[nodiscard]] ExpandFrame expand_frame(const QImage& visible, QMargins margins);

// The area sent for Edit and Remove, grown along the short side toward the API's 1:3..3:1
// range and the whole canvas whenever it would cover half of it.
// Remove (a mask): the whole canvas when it fits kMaskedMaxPixels, so the model sees the
// whole scene (with a mask, a small crop made it paint a new picture into the area, Seth's TV
// test, 2026-11-06); on a larger canvas the selection's bounds grown by its longer side or a
// quarter of the canvas's, whichever is more.
// Edit (`tight`, the markup outline): the selection's bounds grown by its longer side, at
// least 160 px, each way. Measured live (docs/openai-images.md#edit-lab), that crop came back
// sharper inside the selection and changed less around it than the whole picture did.
[[nodiscard]] QRect context_region(QRect selection_bounds, QSize canvas, bool tight = false);

// The request images for a masked edit. `context` is the visible image over the region and
// `coverage` the selection over it (Format_Grayscale8, same size). A region still outside
// 1:3..3:1 is padded with transparent pixels the mask keeps. Both images are scaled to the
// request size; the mask is transparent wherever the selection covers any part of a pixel.
struct MaskedEditImages {
  QImage image;
  QImage mask;
  QSize padded_size;
  QRect content_rect;  // the region inside the padded frame, before scaling
};
// `edge_pad` > 0 (Edit's markup) adds that many transparent pixels on each side where the
// selection reaches the context's edge, so the red ring around the selection lies off the
// picture there and whatever red the model leaves in that padding is cut away with it.
[[nodiscard]] MaskedEditImages masked_edit_images(const QImage& context, const QImage& coverage, int edge_pad = 0);
// The padding Edit uses: 2% of the context's longer side, at least 8 px (the ring is about
// 0.6% of the request's longer side, gap included).
[[nodiscard]] int markup_edge_pad(QSize context);
// True when every pixel of `coverage` is fully selected (an Edit of the whole picture).
[[nodiscard]] bool coverage_is_full(const QImage& coverage);
// Edit's markup overlay for `sent` (made from the same context): transparent, with a red ring
// just outside the selection in `coverage` (Format_Grayscale8, the context's size), about
// 1/250 of the request's longer edge wide, at the request size.
[[nodiscard]] QImage markup_overlay(const QImage& coverage, const MaskedEditImages& sent);
// A returned image back at the region's size: scaled to the padded frame, padding cut away.
[[nodiscard]] QImage result_for_region(const QImage& result, const MaskedEditImages& sent);

// The request size nearest the canvas's aspect (clamped to 1:3..3:1) with both edges a
// multiple of 16 and the area inside the "Match canvas" range and the API's limits.
[[nodiscard]] QSize fit_request_size(QSize canvas);
// The same with the area clamped to [min_pixels, max_pixels] instead.
[[nodiscard]] QSize fit_request_size(QSize canvas, std::int64_t min_pixels, std::int64_t max_pixels);

// Seam control for Edit and Remove (docs/openai-images.md#seams). The model redraws the
// whole frame it is sent and drifts in tone, so a hard mask at the selection edge shows a
// seam. The band is the width, in document pixels, of the transition past the selection:
// the request lets the model change 3 bands past it, the layer mask opens 2 bands past it
// and is feathered by one band, and the result's tone is matched to the original on a ring
// just outside the changed area.
[[nodiscard]] int seam_band(QSize selection_size);
// A square max filter of `radius` px over a Format_Grayscale8 coverage plane.
[[nodiscard]] QImage dilate_coverage(const QImage& coverage, int radius);
// A gaussian-like blur (three box passes) of a Format_Grayscale8 plane, sigma about `sigma`.
[[nodiscard]] QImage feather_coverage(const QImage& coverage, double sigma);
// `result` (RGBA8) with each color channel shifted by the mean difference from `original`
// over the pixels `ring` marks (Format_Grayscale8, nonzero) where both are opaque. Unchanged
// when the ring has no such pixel.
[[nodiscard]] QImage match_tone(const QImage& result, const QImage& original, const QImage& ring);
// The bounds of the nonzero pixels of a Format_Grayscale8 plane (empty when none).
[[nodiscard]] QRect coverage_bounds(const QImage& coverage);
// `top` over `base` (both RGBA8, same size) with `weight` (Format_Grayscale8) as top's opacity:
// what a layer masked by `weight` would composite to, written into one layer.
[[nodiscard]] QImage blend_with_weight(const QImage& base, const QImage& top, const QImage& weight);
// Animate's request text: the user's prompt wrapped in fixed English instructions for a
// consistent sprite sheet (the form OpenAI's Images 2.6 examples use), never built from the
// image (docs/legal-constraints.md, "Prompt-driven image generation").
[[nodiscard]] QString animation_prompt(const QString& motion, int frames, bool from_image, int references = 0);
// Expand's layer: `image` scaled to the grown canvas (`visible`, the original, at (left,
// top)). Over the original it is opaque to a depth of `amount` (0 to 1, the dialog's Blend)
// times the deepest a pixel of the original can lie from an edge that meets new area, then
// fades to clear over expand_fade_width, tone matched to the original across that fade; deeper
// in, the original shows through untouched. The model redraws the original near the new area
// to make the extension flow (a quarter of a 512 px image, Seth's test, 2026-11-06), so the
// fade belongs where its version and the original agree, not at the old edge.
[[nodiscard]] QImage expand_layer_image(const QImage& image, const QImage& visible, QMargins margins, double amount);
// The fade after the replaced depth: 6% of the deepest a pixel can lie, at least 8 px.
[[nodiscard]] int expand_fade_width(QSize original, QMargins margins);
// Edge blend (0 to 1, the dialog's 0 to 100%): the result's mask is the selection dilated by
// up to two bands with a feather of up to one band. 0 is the selection as it is, with no feather.
[[nodiscard]] QImage edge_coverage(const QImage& selection, int band, double amount);
[[nodiscard]] double edge_feather(int band, double amount);
// The ring match_tone reads for a selection `coverage` and band: just outside the area the
// request let the model change.
[[nodiscard]] QImage tone_ring(const QImage& coverage, int band);
// "1024x1024", or "auto" for an empty size.
[[nodiscard]] QString size_parameter(QSize size);
// The JSON body for POST /images/generations (a request without references).
[[nodiscard]] QJsonObject generation_request_json(const GenerateRequest& request);
// The multipart/form-data body for POST /images/edits: text fields plus one "image[]" PNG
// part per reference, or, for a masked edit, the edit image as "image[]" and its "mask"
// (references are never sent with a mask).
[[nodiscard]] QByteArray edits_multipart_body(const GenerateRequest& request, const QByteArray& boundary);
// Decodes `data[].b64_json` and `usage`. Empty images with *error set on a bad body.
[[nodiscard]] GenerateResult parse_images_response(const QByteArray& body, QString* error);
// A plain-language line for an API error (docs/openai-images.md#errors): a rejected key, an
// account out of credit or at its spending limit, a rate limit, a model the key cannot use,
// organization verification, a safety refusal, an OpenAI outage; otherwise OpenAI's own
// `error.message`, or a line for the HTTP status.
[[nodiscard]] QString api_error_message(const QByteArray& body, int http_status);
// Where the user can fix that error (API keys, billing, limits, organization settings), with
// the link's text; empty when no page helps.
struct ErrorHelp {
  QString url;
  QString text;
};
[[nodiscard]] ErrorHelp api_error_help(const QByteArray& body, int http_status);
// The image models in a GET /models body (ids starting with "gpt-image" or "chatgpt-image"),
// newest name first.
[[nodiscard]] QStringList image_model_ids(const QByteArray& models_body, QString* error);
// Animate: the frames of a GIF the API returned, fully composited (qgif), each with its delay
// in milliseconds. Empty with *error when the bytes are not an animated GIF.
struct GifFrame {
  QImage image;
  int delay_ms{0};
};
[[nodiscard]] std::vector<GifFrame> gif_frames(const QByteArray& gif, QString* error);
inline constexpr int kMinGifFrames = 2;
inline constexpr int kMaxGifFrames = 16;
inline constexpr int kMinGifFrameMs = 20;
inline constexpr int kMaxGifFrameMs = 2000;
// The largest square frame OpenAI accepts for `frames`: it lays frames out on a fixed sheet
// (4x2 for few, 3x3 for 9, 4x4 for 16) that must stay within 3840 px a side and its pixel
// budget (refusals probed 2026-11-06: 1024 px never fits; 768 px x 16 frames does not).
[[nodiscard]] constexpr int max_gif_frame_edge(int frames) { return frames <= 9 ? 960 : 720; }

// Animate in full color (docs/openai-images-tabs.md#animate): OpenAI's GIFs share one
// 256-color palette across all frames, so Patchy can instead ask for the frames as one PNG
// sheet (an ordinary image request) and slice it. The grid for `frames`: the fewest spare
// cells with columns >= rows and at most 3:1 (the API's aspect range); spare cells are not
// left empty: the frame count rounds up to fill the grid (5 frames draw as 6 on 3 x 2).
struct SheetLayout {
  int columns{1};
  int rows{1};
  [[nodiscard]] int frames() const { return columns * rows; }
};
[[nodiscard]] SheetLayout sheet_layout(int frames);
// The sheet's request text: fixed English grid and loop instructions with the user's motion
// appended, never generated from the image (docs/legal-constraints.md).
[[nodiscard]] QString sheet_animation_prompt(const QString& motion, SheetLayout layout, bool from_image,
                                             int references = 0);
// The sheet's request size for square frames of `edge` px: the grid of them, enlarged to the
// API's smallest image when it is below it (2 frames of 512 px make 1024 x 512, refused).
[[nodiscard]] QSize sheet_request_size(SheetLayout layout, int edge);
// The sheet cut into its panels, left to right then top to bottom, each with `delay_ms`, and
// scaled to `frame` when it is given and the panels differ from it.
[[nodiscard]] std::vector<GifFrame> sheet_frames(const QImage& sheet, SheetLayout layout, int delay_ms,
                                                 QSize frame = QSize());

// PNG bytes of `image` (8-bit RGBA, no metadata).
[[nodiscard]] QByteArray png_bytes(const QImage& image);
// Create's placement of a returned image on a `canvas`-sized document: scaled to fit it when
// `fit_to_canvas`, centered (floor division, as Paste centers). *origin gets the top-left.
[[nodiscard]] QImage placed_image(const QImage& image, QSize canvas, bool fit_to_canvas, QPoint* origin);
// A reference image scaled down, if needed, so its long edge is at most 2048 px: the API
// accepts 50 MB per image, but there is no gain in uploading more than it renders from.
[[nodiscard]] QImage prepare_reference_image(const QImage& image);
// A reference image read from a file Qt can decode (PNG, JPEG, WebP, BMP, GIF, TIFF),
// prepared as above. Null with *error set when it cannot be read.
[[nodiscard]] QImage load_reference_image_file(const QString& path, QString* error);

// Where the key is remembered: Windows Credential Manager or the macOS keychain (generic
// item "com.rtsoft.patchy.openai" / account "api-key"; a persisted identifier). Elsewhere
// (Linux, the Flatpak) a plain file only the user's account can open, key_file_path(); the
// dialog says it is not encrypted.
class KeyStore {
 public:
  virtual ~KeyStore() = default;
  [[nodiscard]] virtual bool available() const = 0;
  // True for the plain-file store, which the dialog warns about.
  [[nodiscard]] virtual bool unencrypted_file() const { return false; }
  [[nodiscard]] virtual QString load() const = 0;
  virtual bool save(const QString& key) = 0;
  virtual bool clear() = 0;
};
[[nodiscard]] std::unique_ptr<KeyStore> make_system_key_store();
// <per-user app data>/openai-api-key (a persisted name), the plain-file store's file.
[[nodiscard]] QString key_file_path();
// The plain-file store at `path`: written atomically (QSaveFile) with owner-only read and
// write permission, deleted by clear(). The system store on Linux; tests use it anywhere.
[[nodiscard]] std::unique_ptr<KeyStore> make_file_key_store(const QString& path);
// Tests replace the store the dialog uses; pass nullptr to restore the system one.
void set_key_store_factory_for_testing(std::function<std::unique_ptr<KeyStore>()> factory);
[[nodiscard]] std::unique_ptr<KeyStore> make_key_store();
// OPENAI_API_KEY from the environment, trimmed.
[[nodiscard]] QString environment_api_key();

// One request at a time against the OpenAI REST API. The base URL is
// https://api.openai.com/v1 unless PATCHY_OPENAI_BASE_URL names another (tests point it
// at a local server). Callbacks run on the owner's thread; a cancelled request calls
// neither.
// Describe's request: the instruction and the image (scaled to kDescribeMaxEdge, metadata-free
// PNG) as a data URL, `store: false` so OpenAI keeps no copy for later retrieval.
[[nodiscard]] QJsonObject describe_request_json(const QImage& image, const QString& model);
struct DescribeResult {
  QString text;
  std::int64_t total_tokens{0};
};
// The reply's output_text parts joined; a refusal or an empty reply sets `error`.
[[nodiscard]] DescribeResult parse_describe_response(const QByteArray& body, QString* error);

class Client final {
 public:
  explicit Client(QObject* owner);
  ~Client();
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  void set_base_url(const QUrl& url) { base_url_ = url; }
  [[nodiscard]] QUrl base_url() const { return base_url_; }
  void set_api_key(const QString& key) { api_key_ = key.trimmed(); }
  [[nodiscard]] bool busy() const;

  // GET /models: the image models this key can use.
  void list_models(std::function<void(QStringList)> done, std::function<void(QString)> failed);
  void generate(const GenerateRequest& request, std::function<void(GenerateResult)> done,
                std::function<void(QString)> failed);
  // POST /responses: a written description of `image` (Describe).
  void describe(const QImage& image, std::function<void(DescribeResult)> done, std::function<void(QString)> failed);
  void cancel();
  // The last request's path and body, for tests (the key is never kept here).
  [[nodiscard]] QString last_request_path() const { return last_path_; }
  // The help link for the last failure (api_error_help), empty when none applies.
  [[nodiscard]] ErrorHelp last_error_help() const { return last_help_; }

 private:
  [[nodiscard]] QNetworkRequest make_request(const QString& path) const;
  // Builds the body on a worker thread (PNG encoding takes most of a second for a large
  // picture, and the dialog must stay responsive meanwhile), then posts it to `path` unless
  // the request was cancelled or replaced since (docs/openai-images.md#dialog).
  void post_prepared(const QString& path, const QByteArray& content_type, std::function<QByteArray()> build_body,
                     std::function<void(const QByteArray&)> done, std::function<void(QString)> failed);
  void watch(QNetworkReply* reply, std::function<void(const QByteArray&)> done, std::function<void(QString)> failed);

  QObject* owner_;
  QNetworkAccessManager* manager_;
  QPointer<QNetworkReply> reply_;
  bool preparing_{false};      // a body is being built for the current request
  std::uint64_t generation_{0};  // bumped by every request and cancel: stale bodies are dropped
  QUrl base_url_;
  QString api_key_;
  QString last_path_;
  ErrorHelp last_help_;
};

}  // namespace patchy::ui::openai
