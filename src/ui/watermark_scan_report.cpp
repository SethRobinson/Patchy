#include "ui/watermark_scan_report.hpp"

#include "ai/watermark_engine.hpp"
#include "core/trustmark_payload.hpp"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>
#include <array>
#include <string_view>

namespace patchy::ui {
namespace {

QString latin1(std::string_view text) {
  return QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size()));
}

WatermarkCheck not_checked(QString id, QString name, QString reason, QString detail) {
  WatermarkCheck check;
  check.id = std::move(id);
  check.name = std::move(name);
  check.status = WatermarkCheckStatus::NotChecked;
  check.reason = std::move(reason);
  check.detail = std::move(detail);
  return check;
}

}  // namespace

bool WatermarkScanReport::any_found() const noexcept {
  return std::any_of(checks.begin(), checks.end(),
                     [](const WatermarkCheck& check) { return check.status == WatermarkCheckStatus::Found; });
}

std::vector<WatermarkCheck> watermark_checks(const InvisibleWatermarkScan& stable_diffusion,
                                             const std::optional<ai::TrustMarkScan>& trustmark,
                                             const std::string& trustmark_error,
                                             const std::vector<FileInfoObserved>& observed) {
  std::vector<WatermarkCheck> checks;
  for (const auto& known : known_invisible_watermarks()) {
    const auto id = latin1(known.id);
    const auto name = latin1(known.name);
    if (!stable_diffusion.scanned) {
      checks.push_back(not_checked(id, name, QStringLiteral("imageTooSmall"),
                                   QCoreApplication::translate("WatermarkScanReport",
                                                               "The image is under 256 x 256 pixels, too small to "
                                                               "carry this mark.")));
      continue;
    }
    WatermarkCheck check;
    check.id = id;
    check.name = name;
    const auto match = std::find_if(stable_diffusion.matches.begin(), stable_diffusion.matches.end(),
                                    [&](const InvisibleWatermarkMatch& item) { return item.id == known.id; });
    if (match != stable_diffusion.matches.end()) {
      check.status = WatermarkCheckStatus::Found;
      check.match = *match;
      check.detail = QCoreApplication::translate("WatermarkScanReport", "%1 of %2 bits match.")
                         .arg(match->bit_count - match->differing_bits)
                         .arg(match->bit_count);
    }
    checks.push_back(std::move(check));
  }

  constexpr std::array variants{ai::TrustMarkVariant::Q, ai::TrustMarkVariant::P, ai::TrustMarkVariant::C};
  for (const auto variant : variants) {
    const auto letter = latin1(ai::trustmark_variant_name(variant));
    const auto id = QStringLiteral("trustmark") + letter;
    const auto name = QStringLiteral("Adobe TrustMark ") + letter;
    if (!trustmark.has_value() || !trustmark->ran) {
      if (!trustmark_error.empty()) {
        checks.push_back(not_checked(id, name, QStringLiteral("failed"),
                                     QCoreApplication::translate("WatermarkScanReport", "TrustMark could not run: %1")
                                         .arg(QString::fromStdString(trustmark_error))));
      } else if (!ai::trustmark_runtime_available()) {
        checks.push_back(not_checked(id, name, QStringLiteral("notSupported"),
                                     QCoreApplication::translate("WatermarkScanReport",
                                                                 "This version of Patchy cannot read TrustMark "
                                                                 "watermarks.")));
      } else if (!ai::trustmark_models_available()) {
        checks.push_back(not_checked(id, name, QStringLiteral("modelsNotDownloaded"),
                                     QCoreApplication::translate("WatermarkScanReport",
                                                                 "The watermark models are not downloaded.")));
      } else {
        const auto reason = trustmark.has_value() ? trustmark->unavailable_reason : std::string();
        checks.push_back(not_checked(id, name, QStringLiteral("failed"),
                                     QCoreApplication::translate("WatermarkScanReport", "TrustMark could not run: %1")
                                         .arg(QString::fromStdString(reason))));
      }
      continue;
    }
    WatermarkCheck check;
    check.id = id;
    check.name = name;
    const auto mark = std::find_if(trustmark->marks.begin(), trustmark->marks.end(),
                                   [&](const ai::TrustMarkMark& item) { return item.variant == variant; });
    if (mark != trustmark->marks.end()) {
      check.status = WatermarkCheckStatus::Found;
      check.mark = *mark;
      const auto payload = mark->text.empty() ? QString::fromStdString(mark->hex)
                                              : QStringLiteral("“%1”").arg(QString::fromStdString(mark->text));
      check.detail = QCoreApplication::translate("WatermarkScanReport", "Carries %1 (%2), confidence %3.")
                         .arg(payload, latin1(trustmark::schema_name(mark->schema)),
                              QString::number(static_cast<double>(mark->confidence), 'f', 1));
    }
    checks.push_back(std::move(check));
  }

  WatermarkCheck declared;
  declared.id = QStringLiteral("c2paDeclared");
  declared.name = QCoreApplication::translate("WatermarkScanReport", "Declared in C2PA metadata");
  QStringList summaries;
  for (const auto& item : observed) {
    if (item.privacy == MetadataPrivacy::Watermark) {
      declared.declarations.push_back(item.summary);
      summaries.push_back(QString::fromStdString(item.summary));
    }
  }
  if (!declared.declarations.empty()) {
    declared.status = WatermarkCheckStatus::Found;
    declared.detail = summaries.join(QStringLiteral("; "));
  }
  checks.push_back(std::move(declared));
  return checks;
}

WatermarkScanReport scan_watermarks(const PixelBuffer& rgb8, const std::vector<FileInfoObserved>& observed,
                                    ai::InferenceCancel* cancel) {
  WatermarkScanReport report;
  report.stable_diffusion = scan_invisible_watermarks(rgb8);
  if (ai::trustmark_models_available()) {
    try {
      report.trustmark = ai::scan_trustmark(rgb8, cancel);
    } catch (const ai::InferenceCancelled&) {
      throw;
    } catch (const std::exception& error) {
      report.trustmark_error = error.what();
      if (report.trustmark_error.empty()) {
        report.trustmark_error = "unknown error";
      }
    }
  }
  report.checks = watermark_checks(report.stable_diffusion, report.trustmark, report.trustmark_error, observed);
  return report;
}

QString watermark_check_status_text(WatermarkCheckStatus status) {
  switch (status) {
    case WatermarkCheckStatus::Found:
      return QCoreApplication::translate("WatermarkScanReport", "Found");
    case WatermarkCheckStatus::NotFound:
      return QCoreApplication::translate("WatermarkScanReport", "Not found");
    case WatermarkCheckStatus::NotChecked:
      break;
  }
  return QCoreApplication::translate("WatermarkScanReport", "Not checked");
}

QString watermark_check_status_token(WatermarkCheckStatus status) {
  switch (status) {
    case WatermarkCheckStatus::Found:
      return QStringLiteral("found");
    case WatermarkCheckStatus::NotFound:
      return QStringLiteral("notFound");
    case WatermarkCheckStatus::NotChecked:
      break;
  }
  return QStringLiteral("notChecked");
}

QString watermark_scan_limits_note() {
  return QCoreApplication::translate("WatermarkScanReport",
                                     "Some watermarks, such as Google SynthID and Digimarc, can only be read with "
                                     "their owners' tools, so no scan here can find them. Not found means only "
                                     "that none of the kinds above was found.");
}

}  // namespace patchy::ui
