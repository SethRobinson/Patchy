// `patchy_perf_tests startup`: cold/warm startup timing and the startup regression
// check (docs/startup-performance.md). Launches the sibling patchy executable N times
// with --startup-benchmark, takes the median of every phase, and compares it with a
// per-host baseline file or, interleaved, with a second build.
#include "test_scratch_remove.hpp"
#include "ui/qt_paths.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSettings>
#include <QSysInfo>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace patchy::perf {

namespace {

// One launch: metric key -> milliseconds. Keys: "ready" (startup.idle), "pre_main",
// "mark:<name>", "phase:<name>", "background:<name>".
using Sample = std::map<std::string, double>;

struct Stat {
  double median = 0.0;
  double min = 0.0;
  double mad = 0.0;  // median absolute deviation, scaled to a standard deviation
  int count = 0;
};

struct LaunchConfig {
  QString exe;
  bool onscreen = false;
  bool update_check = false;
  QString scratch;  // settings, fonts and recovery folders shared by every launch
};

int env_int(const char* name, int fallback) {
  bool ok = false;
  const int value = qEnvironmentVariableIntValue(name, &ok);
  return ok ? value : fallback;
}

double env_double(const char* name, double fallback) {
  bool ok = false;
  const double value = qEnvironmentVariable(name).toDouble(&ok);
  return ok ? value : fallback;
}

QString default_patchy_exe() {
  const QDir dir(QCoreApplication::applicationDirPath());
  for (const auto& name : {QStringLiteral("patchy.exe"), QStringLiteral("patchy"),
                           QStringLiteral("Patchy.app/Contents/MacOS/Patchy"),
                           QStringLiteral("patchy.app/Contents/MacOS/patchy")}) {
    if (QFileInfo(dir.filePath(name)).isExecutable()) {
      return dir.filePath(name);
    }
  }
  return {};
}

// "Cold" on Windows: write the modified page list out, then drop the standby list (the
// file cache), as RAMMap's Empty Standby List does. Files a running process has mapped
// stay resident, and the font cache service keeps its own data, so this approximates a
// first launch after boot; it is the closest repeatable cold start without a reboot.
// Needs an elevated process (SeProfileSingleProcessPrivilege).
bool purge_file_cache(std::string& why) {
#ifdef Q_OS_WIN
  HANDLE token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token) == FALSE) {
    why = "OpenProcessToken failed";
    return false;
  }
  TOKEN_PRIVILEGES privileges{};
  privileges.PrivilegeCount = 1;
  privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  const bool looked_up =
      LookupPrivilegeValueW(nullptr, L"SeProfileSingleProcessPrivilege", &privileges.Privileges[0].Luid) != FALSE;
  const bool adjusted = looked_up && AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr) != FALSE &&
                        GetLastError() == ERROR_SUCCESS;
  CloseHandle(token);
  if (!adjusted) {
    why = "needs an elevated process (SeProfileSingleProcessPrivilege)";
    return false;
  }
  using SetSystemInformation = LONG(NTAPI*)(INT, PVOID, ULONG);
  auto* ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto set_information =
      ntdll == nullptr ? nullptr
                       : reinterpret_cast<SetSystemInformation>(
                             reinterpret_cast<void*>(GetProcAddress(ntdll, "NtSetSystemInformation")));
  if (set_information == nullptr) {
    why = "NtSetSystemInformation is unavailable";
    return false;
  }
  constexpr INT kSystemMemoryListInformation = 80;
  for (int command : {3 /* MemoryFlushModifiedList */, 4 /* MemoryPurgeStandbyList */}) {
    if (set_information(kSystemMemoryListInformation, &command, sizeof(command)) < 0) {
      why = "the memory list command was refused";
      return false;
    }
  }
  return true;
#else
  why = "only implemented on Windows";
  return false;
#endif
}

QString cpu_model() {
#ifdef Q_OS_WIN
  const QSettings processor(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
                            QSettings::NativeFormat);
  return processor.value(QStringLiteral("ProcessorNameString")).toString().simplified();
#else
  return QSysInfo::currentCpuArchitecture();
#endif
}

std::optional<Sample> launch_once(const LaunchConfig& config, const QString& report_path, std::string& error,
                                  std::vector<std::string>* order) {
  (void)QFile::remove(report_path);
  auto environment = QProcessEnvironment::systemEnvironment();
  const QDir scratch(config.scratch);
  environment.insert(QStringLiteral("PATCHY_SETTINGS_DIR"), scratch.filePath(QStringLiteral("settings")));
  environment.insert(QStringLiteral("PATCHY_USER_FONTS_DIR"), scratch.filePath(QStringLiteral("user-fonts")));
  environment.insert(QStringLiteral("PATCHY_RECOVERY_DIR"), scratch.filePath(QStringLiteral("recovery")));
  environment.insert(QStringLiteral("PATCHY_MCP_ENDPOINT"),
                     QStringLiteral("PatchyMcp-perf-startup-%1").arg(QCoreApplication::applicationPid()));
  environment.insert(QStringLiteral("PATCHY_NO_SINGLE_INSTANCE"), QStringLiteral("1"));
  environment.insert(QStringLiteral("PATCHY_NO_SOUND"), QStringLiteral("1"));
  environment.remove(QStringLiteral("PATCHY_STARTUP_TRACE"));
  if (!config.update_check) {
    environment.insert(QStringLiteral("PATCHY_NO_UPDATE_CHECK"), QStringLiteral("1"));
  }
  QStringList arguments;
  if (config.onscreen) {
    environment.remove(QStringLiteral("QT_QPA_PLATFORM"));  // this harness runs offscreen itself
  } else {
    arguments << QStringLiteral("--headless");
  }
  arguments << QStringLiteral("--startup-benchmark") << report_path;
  QProcess process;
  process.setProcessEnvironment(environment);
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(config.exe, arguments);
  if (!process.waitForStarted(30000)) {
    error = "could not start " + config.exe.toStdString();
    return std::nullopt;
  }
  if (!process.waitForFinished(180000)) {
    process.kill();
    process.waitForFinished(10000);
    error = "timed out";
    return std::nullopt;
  }
  const auto output = process.readAll();
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    error = "exit code " + std::to_string(process.exitCode());
    // Exit 3 with a report: startup never went idle; name the marks that never came.
    if (QFile partial(report_path); partial.open(QIODevice::ReadOnly)) {
      const auto events = QJsonDocument::fromJson(partial.readAll()).object().value(QStringLiteral("events")).toArray();
      std::string missing;
      for (const char* mark : {"event_loop.enter", "first_paint", "startup.idle"}) {
        const bool seen = std::any_of(events.begin(), events.end(), [mark](const QJsonValue& value) {
          return value.toObject().value(QStringLiteral("name")).toString() == QLatin1String(mark);
        });
        if (!seen) {
          missing += std::string(missing.empty() ? "" : ", ") + mark;
        }
      }
      error += missing.empty() ? " (report complete)" : " (never reached: " + missing + ")";
    }
    error += ": " + output.left(2000).toStdString();
    return std::nullopt;
  }
  QFile file(report_path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = "no report written";
    return std::nullopt;
  }
  const auto document = QJsonDocument::fromJson(file.readAll());
  const auto root = document.object();
  if (root.value(QStringLiteral("format")).toString() != QStringLiteral("patchy-startup-report")) {
    error = "not a startup report";
    return std::nullopt;
  }
  Sample sample;
  sample["ready"] = root.value(QStringLiteral("ready_ms")).toDouble(-1.0);
  if (root.value(QStringLiteral("process_start_known")).toBool()) {
    sample["pre_main"] = root.value(QStringLiteral("pre_main_ms")).toDouble();
  }
  for (const auto& value : root.value(QStringLiteral("events")).toArray()) {
    const auto event = value.toObject();
    const auto name = event.value(QStringLiteral("name")).toString().toStdString();
    const auto kind = event.value(QStringLiteral("kind")).toString().toStdString();
    std::string key;
    double ms = event.value(QStringLiteral("duration_ms")).toDouble();
    if (kind == "mark") {
      key = "mark:" + name;
      ms = event.value(QStringLiteral("start_ms")).toDouble();
    } else {
      key = kind + ":" + name;
    }
    if (sample.count(key) == 0) {  // a repeated phase keeps its first occurrence
      sample[key] = ms;
      if (order != nullptr && std::find(order->begin(), order->end(), key) == order->end()) {
        order->push_back(key);
      }
    }
  }
  if (sample["ready"] < 0.0) {
    error = "the report has no startup.idle mark";
    return std::nullopt;
  }
  return sample;
}

std::map<std::string, Stat> statistics(const std::vector<Sample>& samples) {
  std::map<std::string, std::vector<double>> values;
  for (const auto& sample : samples) {
    for (const auto& [key, ms] : sample) {
      values[key].push_back(ms);
    }
  }
  const auto median_of = [](std::vector<double> list) {
    std::sort(list.begin(), list.end());
    const auto size = list.size();
    return size % 2 == 1 ? list[size / 2] : (list[size / 2 - 1] + list[size / 2]) / 2.0;
  };
  std::map<std::string, Stat> result;
  for (const auto& [key, list] : values) {
    Stat stat;
    stat.count = static_cast<int>(list.size());
    stat.median = median_of(list);
    stat.min = *std::min_element(list.begin(), list.end());
    std::vector<double> deviations;
    for (const double value : list) {
      deviations.push_back(std::abs(value - stat.median));
    }
    stat.mad = 1.4826 * median_of(deviations);
    result[key] = stat;
  }
  return result;
}

std::string fixed(double value, int decimals = 1) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(decimals) << value;
  return out.str();
}

std::string label(const std::string& key) {
  if (key == "ready") {
    return "ready (startup.idle)";
  }
  if (key == "pre_main") {
    return "process.pre_main";
  }
  const auto colon = key.find(':');
  const auto kind = key.substr(0, colon);
  const auto name = key.substr(colon + 1);
  if (kind == "mark") {
    return "@" + name;
  }
  if (kind == "background") {
    return "bg " + name;
  }
  return name;
}

enum class Verdict { None, Ok, Warn, Fail };

struct Judgement {
  Verdict verdict = Verdict::None;
  double delta = 0.0;
  double ratio = 0.0;
};

// Noise-robust: a metric only counts as slower when it is slower by a ratio AND by an
// absolute margin AND by three times the larger spread of the two sets. Phases under
// the floor (default 10 ms) are reported but never judged.
Judgement judge(const std::string& key, const Stat& current, const Stat& reference) {
  Judgement judgement;
  judgement.delta = current.median - reference.median;
  judgement.ratio = reference.median > 0.0 ? current.median / reference.median : 0.0;
  const bool total = key == "ready";
  const bool timed_phase = key.rfind("phase:", 0) == 0 || key == "pre_main";
  if (!total && !timed_phase) {
    return judgement;
  }
  if (!total && reference.median < env_double("PATCHY_PERF_STARTUP_FLOOR_MS", 10.0)) {
    return judgement;
  }
  const double noise = 3.0 * std::max(current.mad, reference.mad);
  const double warn_ratio = total ? 1.10 : 1.25;
  const double fail_ratio = total ? 1.25 : 1.50;
  const double warn_ms = total ? 30.0 : 10.0;
  const double fail_ms = total ? 60.0 : 25.0;
  judgement.verdict = Verdict::Ok;
  if (judgement.ratio > fail_ratio && judgement.delta > std::max(fail_ms, noise)) {
    judgement.verdict = Verdict::Fail;
  } else if (judgement.ratio > warn_ratio && judgement.delta > std::max(warn_ms, noise)) {
    judgement.verdict = Verdict::Warn;
  }
  return judgement;
}

const char* verdict_text(Verdict verdict) {
  switch (verdict) {
    case Verdict::Ok:
      return "ok";
    case Verdict::Warn:
      return "WARN";
    case Verdict::Fail:
      return "FAIL";
    case Verdict::None:
      break;
  }
  return "";
}

QJsonObject stats_json(const std::map<std::string, Stat>& stats) {
  QJsonObject object;
  for (const auto& [key, stat] : stats) {
    object.insert(QString::fromStdString(key),
                  QJsonObject{{QStringLiteral("median"), stat.median}, {QStringLiteral("min"), stat.min},
                              {QStringLiteral("mad"), stat.mad}, {QStringLiteral("count"), stat.count}});
  }
  return object;
}

std::map<std::string, Stat> stats_from_json(const QJsonObject& object) {
  std::map<std::string, Stat> stats;
  for (auto it = object.begin(); it != object.end(); ++it) {
    const auto value = it.value().toObject();
    Stat stat;
    stat.median = value.value(QStringLiteral("median")).toDouble();
    stat.min = value.value(QStringLiteral("min")).toDouble();
    stat.mad = value.value(QStringLiteral("mad")).toDouble();
    stat.count = value.value(QStringLiteral("count")).toInt();
    stats[it.key().toStdString()] = stat;
  }
  return stats;
}

// Prints one set (and its comparison); returns the worst verdict.
Verdict print_set(const std::string& title, const std::vector<std::string>& order,
                  const std::map<std::string, Stat>& stats, const std::map<std::string, Stat>* reference,
                  const std::string& reference_name) {
  std::cout << "[PERF] startup " << title << '\n';
  std::cout << std::left << std::setw(40) << "  metric" << std::right << std::setw(10) << "median" << std::setw(9)
            << "min" << std::setw(8) << "mad";
  if (reference != nullptr) {
    std::cout << std::setw(10) << reference_name << std::setw(9) << "delta" << std::setw(7) << "ratio" << "  verdict";
  }
  std::cout << '\n';
  Verdict worst = Verdict::None;
  std::vector<std::string> keys = {"ready", "pre_main"};
  for (const auto& key : order) {
    if (key != "pre_main" && key != "phase:process.pre_main") {
      keys.push_back(key);
    }
  }
  for (const auto& key : keys) {
    const auto found = stats.find(key);
    if (found == stats.end()) {
      continue;
    }
    const auto& stat = found->second;
    std::cout << std::left << std::setw(40) << ("  " + label(key)) << std::right << std::setw(10)
              << fixed(stat.median) << std::setw(9) << fixed(stat.min) << std::setw(8) << fixed(stat.mad);
    if (reference != nullptr) {
      const auto ref = reference->find(key);
      if (ref == reference->end()) {
        std::cout << std::setw(10) << "-" << "  (new)";
      } else {
        const auto judgement = judge(key, stat, ref->second);
        std::cout << std::setw(10) << fixed(ref->second.median) << std::setw(9) << fixed(judgement.delta)
                  << std::setw(7) << fixed(judgement.ratio, 2) << "  " << verdict_text(judgement.verdict);
        worst = std::max(worst, judgement.verdict);
      }
    }
    std::cout << '\n';
  }
  if (reference != nullptr) {
    for (const auto& entry : *reference) {
      if (stats.count(entry.first) == 0 && entry.first.rfind("phase:", 0) == 0) {
        std::cout << "  " << label(entry.first) << ": in the reference, missing now\n";
      }
    }
  }
  return worst;
}

}  // namespace

int run_startup_scenario() {
  LaunchConfig config;
  config.exe = qEnvironmentVariable("PATCHY_PERF_STARTUP_EXE", default_patchy_exe());
  config.onscreen = qEnvironmentVariableIsSet("PATCHY_PERF_ONSCREEN");
  config.update_check = qEnvironmentVariableIsSet("PATCHY_PERF_STARTUP_UPDATE_CHECK");
  if (config.exe.isEmpty() || !QFileInfo(config.exe).isExecutable()) {
    std::cerr << "[FAIL] startup: no patchy executable beside the perf tests (build the patchy target, "
                 "or set PATCHY_PERF_STARTUP_EXE)\n";
    return 1;
  }
  const int runs = std::max(1, env_int("PATCHY_PERF_STARTUP_RUNS", 10));
  const bool cold = qEnvironmentVariableIsSet("PATCHY_PERF_STARTUP_COLD");
  const int cold_runs = std::max(1, env_int("PATCHY_PERF_STARTUP_COLD_RUNS", 5));
  const auto compare_exe = qEnvironmentVariable("PATCHY_PERF_STARTUP_COMPARE_EXE");
  const auto mode = std::string(config.onscreen ? "window" : "headless");
#ifdef Q_OS_WIN
  // ssh and services run in session 0, which has no interactive desktop: a real window
  // never paints there (the launch would wait out its two-minute guard).
  if (DWORD session = 0; config.onscreen && ProcessIdToSessionId(GetCurrentProcessId(), &session) != FALSE &&
                         session == 0) {
    std::cout << "[SKIP] startup window: this process runs in session 0 (ssh or a service), which has no "
                 "interactive desktop; measure a real window from a desktop session\n";
    return 0;
  }
#endif

  // Everything this scenario writes lives below test-artifacts (the scratch-delete rule).
  const auto artifacts = QDir(QStringLiteral("test-artifacts")).absolutePath();
  const auto root = QDir(artifacts).filePath(QStringLiteral("perf-startup"));
  (void)patchy::test::remove_test_scratch_tree(patchy::ui::to_filesystem_path(root));
  QDir().mkpath(root);
  config.scratch = QDir(root).filePath(QStringLiteral("profile"));
  QDir().mkpath(config.scratch);
  auto reference_config = config;
  reference_config.exe = compare_exe;
  reference_config.scratch = QDir(root).filePath(QStringLiteral("reference-profile"));
  if (!compare_exe.isEmpty()) {
    if (!QFileInfo(compare_exe).isExecutable()) {
      std::cerr << "[FAIL] startup: PATCHY_PERF_STARTUP_COMPARE_EXE is not an executable\n";
      return 1;
    }
    QDir().mkpath(reference_config.scratch);
  }

  std::cout << "[PERF] startup exe=" << QDir::toNativeSeparators(config.exe).toStdString() << " mode=" << mode
            << " runs=" << runs << " cpu=\"" << cpu_model().toStdString()
            << "\" threads=" << QThread::idealThreadCount() << " update_check=" << (config.update_check ? 1 : 0)
            << '\n';
  std::vector<std::string> order;
  std::string error;
  const auto report = [&root](const char* name, int index) {
    return QDir(root).filePath(QStringLiteral("%1-%2.json").arg(QLatin1String(name)).arg(index));
  };
  const auto run_or_fail = [&](const LaunchConfig& launch, const QString& path) -> std::optional<Sample> {
    auto sample = launch_once(launch, path, error, &order);
    if (!sample) {
      std::cerr << "[FAIL] startup launch: " << error << '\n';
    }
    return sample;
  };

  // The first launch runs on a fresh profile with whatever the file cache holds; it is
  // reported alone and kept out of the warm medians.
  const auto first = run_or_fail(config, report("first", 0));
  if (!first) {
    return 1;
  }
  std::cout << "[PERF] startup first launch (fresh profile): ready_ms=" << fixed(first->at("ready")) << '\n';
  if (!compare_exe.isEmpty() && !run_or_fail(reference_config, report("reference-first", 0))) {
    return 1;
  }

  std::vector<Sample> warm;
  std::vector<Sample> reference_warm;
  for (int index = 0; index < runs; ++index) {
    // Interleaved, so drift on the machine (thermals, a background job) hits both builds.
    const auto sample = run_or_fail(config, report("warm", index));
    if (!sample) {
      return 1;
    }
    warm.push_back(*sample);
    if (!compare_exe.isEmpty()) {
      const auto reference_sample = run_or_fail(reference_config, report("reference-warm", index));
      if (!reference_sample) {
        return 1;
      }
      reference_warm.push_back(*reference_sample);
    }
  }

  std::vector<Sample> cold_samples;
  std::vector<Sample> reference_cold;
  std::string cold_skip;
  if (cold) {
    for (int index = 0; index < cold_runs && cold_skip.empty(); ++index) {
      for (int build = 0; build < (compare_exe.isEmpty() ? 1 : 2); ++build) {
        if (!purge_file_cache(cold_skip)) {
          break;
        }
        QThread::msleep(1500);  // let the purge's own disk work finish
        const auto& launch = build == 0 ? config : reference_config;
        const auto sample = run_or_fail(launch, report(build == 0 ? "cold" : "reference-cold", index));
        if (!sample) {
          return 1;
        }
        (build == 0 ? cold_samples : reference_cold).push_back(*sample);
      }
    }
    if (!cold_skip.empty()) {
      std::cout << "[SKIP] startup cold launches: " << cold_skip << '\n';
      cold_samples.clear();
      reference_cold.clear();
    }
  }

  const auto warm_stats = statistics(warm);
  const auto cold_stats = cold_samples.empty() ? std::map<std::string, Stat>{} : statistics(cold_samples);

  // Reference: the interleaved build, else the baseline file recorded on this host.
  const auto host = QSysInfo::machineHostName();
  const auto baseline_path = qEnvironmentVariable(
      "PATCHY_PERF_STARTUP_BASELINE",
      QStringLiteral(PATCHY_SOURCE_DIR "/local-test-fixtures/startup-baselines/%1-%2.json")
          .arg(host.isEmpty() ? QStringLiteral("host") : host, QString::fromStdString(mode)));
  std::map<std::string, Stat> reference_warm_stats;
  std::map<std::string, Stat> reference_cold_stats;
  std::string reference_name;
  if (!compare_exe.isEmpty()) {
    reference_warm_stats = statistics(reference_warm);
    if (!reference_cold.empty()) {
      reference_cold_stats = statistics(reference_cold);
    }
    reference_name = "other";
  } else if (QFile baseline(baseline_path); !qEnvironmentVariableIsSet("PATCHY_PERF_STARTUP_RECORD") &&
                                            baseline.open(QIODevice::ReadOnly)) {
    const auto object = QJsonDocument::fromJson(baseline.readAll()).object();
    reference_warm_stats = stats_from_json(object.value(QStringLiteral("warm")).toObject());
    reference_cold_stats = stats_from_json(object.value(QStringLiteral("cold")).toObject());
    reference_name = "baseline";
    const auto recorded_cpu = object.value(QStringLiteral("cpu")).toString();
    std::cout << "[PERF] startup baseline " << QDir::toNativeSeparators(baseline_path).toStdString() << " (recorded "
              << object.value(QStringLiteral("recorded")).toString().toStdString() << ", build "
              << object.value(QStringLiteral("patchy_version")).toString().toStdString() << ")\n";
    if (recorded_cpu != cpu_model()) {
      std::cout << "[WARN] startup baseline was recorded on another CPU (" << recorded_cpu.toStdString()
                << "); the comparison means little\n";
    }
  }

  Verdict worst = print_set("warm (median of " + std::to_string(warm.size()) + ")", order, warm_stats,
                            reference_warm_stats.empty() ? nullptr : &reference_warm_stats, reference_name);
  if (!cold_stats.empty()) {
    worst = std::max(worst, print_set("cold (median of " + std::to_string(cold_samples.size()) +
                                          ", standby list purged before each)",
                                      order, cold_stats, reference_cold_stats.empty() ? nullptr : &reference_cold_stats,
                                      reference_name));
  }

  QJsonObject summary{
      {QStringLiteral("format"), QStringLiteral("patchy-startup-summary")},
      {QStringLiteral("version"), 1},
      {QStringLiteral("mode"), QString::fromStdString(mode)},
      {QStringLiteral("cpu"), cpu_model()},
      {QStringLiteral("threads"), QThread::idealThreadCount()},
      {QStringLiteral("recorded"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
      {QStringLiteral("patchy_version"),
       QJsonDocument::fromJson([&] {
         QFile file(report("first", 0));
         return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
       }())
           .object()
           .value(QStringLiteral("patchy_version"))
           .toString()},
      {QStringLiteral("first"), stats_json(statistics({*first}))},
      {QStringLiteral("warm"), stats_json(warm_stats)},
  };
  if (!cold_stats.empty()) {
    summary.insert(QStringLiteral("cold"), stats_json(cold_stats));
  }
  const auto summary_bytes = QJsonDocument(summary).toJson();
  const auto summary_path = QDir(artifacts).filePath(QStringLiteral("perf-startup-%1.json").arg(QString::fromStdString(mode)));
  if (QSaveFile out(summary_path); out.open(QIODevice::WriteOnly) && out.write(summary_bytes) == summary_bytes.size()) {
    (void)out.commit();
  }
  std::cout << "[PERF] startup summary " << QDir::toNativeSeparators(summary_path).toStdString() << '\n';
  if (qEnvironmentVariableIsSet("PATCHY_PERF_STARTUP_RECORD")) {
    QDir().mkpath(QFileInfo(baseline_path).absolutePath());
    QSaveFile out(baseline_path);
    if (!out.open(QIODevice::WriteOnly) || out.write(summary_bytes) != summary_bytes.size() || !out.commit()) {
      std::cerr << "[FAIL] startup: could not write the baseline " << baseline_path.toStdString() << '\n';
      return 1;
    }
    std::cout << "[PERF] startup baseline recorded: " << QDir::toNativeSeparators(baseline_path).toStdString()
              << '\n';
  } else if (reference_name.empty()) {
    std::cout << "[PERF] startup: no baseline for this host yet; PATCHY_PERF_STARTUP_RECORD=1 records "
              << QDir::toNativeSeparators(baseline_path).toStdString() << '\n';
  }

  if (worst == Verdict::Fail) {
    const bool warn_only = qEnvironmentVariableIsSet("PATCHY_PERF_STARTUP_WARN_ONLY");
    std::cout << (warn_only ? "[WARN]" : "[FAIL]") << " startup is slower than the " << reference_name
              << " beyond the regression margin\n";
    return warn_only ? 0 : 1;
  }
  if (worst == Verdict::Warn) {
    std::cout << "[WARN] startup is slower than the " << reference_name << " beyond the warning margin\n";
  }
  return 0;
}

}  // namespace patchy::perf
