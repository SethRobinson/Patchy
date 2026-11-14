// Single-instance startup (src/ui/single_instance.hpp): which of several Patchy processes
// started together becomes the one instance, and how the others hand it their files
// (GitHub issue 722: a multi-file open from the file manager started one Patchy per file).
#include "test_harness.hpp"
#include "ui/single_instance.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QImage>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>

#include <cstdio>
#include <memory>
#include <vector>

#ifndef Q_OS_WIN
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {
using namespace patchy::test::ui;

constexpr int kBackstopMs = static_cast<int>(patchy::test::kWallClockBackstopMs);

// A server name no other process uses: a pipe name on Windows, a socket path in a short
// temp folder elsewhere (a Unix socket path holds about 100 characters).
struct TestInstanceName {
  QTemporaryDir folder{QDir::tempPath() + QStringLiteral("/patchy-si-XXXXXX")};
  QString name;

  TestInstanceName() {
    CHECK(folder.isValid());
#ifdef Q_OS_WIN
    name = QStringLiteral("Patchy-SingleInstanceTest-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
#else
    name = folder.filePath(QStringLiteral("s"));
#endif
  }
  ~TestInstanceName() {
    const auto lock = patchy::ui::single_instance_lock_path(name);
    (void)QFile::remove(lock);
    (void)QFile::remove(lock + QStringLiteral(".rmlock"));
  }
};

// Every request `server` has received so far, appended to `received`; true once there are `count`.
bool wait_for_requests(patchy::ui::SingleInstanceServer& server, std::vector<QStringList>& received, int count) {
  return process_events_until(
      [&] {
        for (auto& request : server.take_requests()) {
          received.push_back(std::move(request));
        }
        return static_cast<int>(received.size()) >= count;
      },
      kBackstopMs);
}

// The first claim listens; every later launch forwards to it, and its requests wait in
// arrival order until the GUI thread takes them. The listener runs on its own thread: the
// forwards below complete while this (GUI) thread never returns to the event loop, which is
// the state of an instance still building its window.
void ui_single_instance_first_claim_listens_and_later_launches_forward() {
  TestInstanceName instance;
  CHECK(!patchy::ui::forward_to_single_instance(instance.name, {QStringLiteral("nobody listens")}));

  patchy::ui::SingleInstanceServer first;
  int ready_signals = 0;
  QObject::connect(&first, &patchy::ui::SingleInstanceServer::request_ready, &first, [&ready_signals] { ++ready_signals; });
  CHECK(patchy::ui::claim_single_instance(first, instance.name, {QStringLiteral("unused")}) ==
        patchy::ui::SingleInstanceRole::Primary);
  CHECK(first.is_listening());
  // The startup lock is released once the claim is decided.
  CHECK(!QFile::exists(patchy::ui::single_instance_lock_path(instance.name)));

  const auto unicode_file = QStringLiteral("/photos/") +
                            QString::fromUtf8(patchy::test::utf8_string(patchy::test::kUnicodeCombinedStem)) +
                            QStringLiteral(".png");
  const QStringList second_request{QStringLiteral("/photos/b.png"), QStringLiteral("patchy-cmd:run-script\nx.js\nout.txt")};
  patchy::ui::SingleInstanceServer second;
  CHECK(patchy::ui::claim_single_instance(second, instance.name, second_request) ==
        patchy::ui::SingleInstanceRole::Forwarded);
  CHECK(!second.is_listening());
  CHECK(patchy::ui::forward_to_single_instance(instance.name, {unicode_file}));
  CHECK(patchy::ui::forward_to_single_instance(instance.name, {}));  // a bare relaunch
  CHECK(first.take_requests().empty());  // nothing reaches the GUI thread behind its back

  std::vector<QStringList> received;
  CHECK(wait_for_requests(first, received, 3));
  CHECK(received.size() == 3);
  CHECK(received[0] == second_request);
  CHECK(received[1] == QStringList{unicode_file});
  CHECK(received[2].isEmpty());
  CHECK(ready_signals == 3);

  // Once closed (the instance is quitting) nothing answers, so a new launch starts its own.
  first.close();
  CHECK(!first.is_listening());
  CHECK(!patchy::ui::forward_to_single_instance(instance.name, {QStringLiteral("too late")}));
}

// A socket a live instance answers on is never removed by a later launch; one a crashed
// instance left behind (Unix: the file outlives the process) is replaced.
void ui_single_instance_claim_keeps_a_live_socket_and_replaces_a_dead_one() {
  {
    TestInstanceName instance;
    patchy::ui::SingleInstanceServer live;
    CHECK(patchy::ui::claim_single_instance(live, instance.name, {}) == patchy::ui::SingleInstanceRole::Primary);
    for (int launch = 0; launch < 3; ++launch) {
      patchy::ui::SingleInstanceServer later;
      CHECK(patchy::ui::claim_single_instance(later, instance.name, {QString::number(launch)}) ==
            patchy::ui::SingleInstanceRole::Forwarded);
    }
    std::vector<QStringList> received;
    CHECK(wait_for_requests(live, received, 3));
    CHECK(received.size() == 3);
    for (int launch = 0; launch < 3; ++launch) {
      CHECK(received[static_cast<std::size_t>(launch)] == QStringList{QString::number(launch)});
    }
  }
#ifndef Q_OS_WIN
  {
    TestInstanceName instance;
    // What a crash leaves: a bound socket file nobody listens on.
    const auto path = QFile::encodeName(instance.name);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    CHECK(static_cast<std::size_t>(path.size()) < sizeof(address.sun_path));
    std::memcpy(address.sun_path, path.constData(), static_cast<std::size_t>(path.size()));
    const int dead = ::socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(dead >= 0);
    CHECK(::bind(dead, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    ::close(dead);
    CHECK(QFileInfo::exists(instance.name));
    CHECK(!patchy::ui::forward_to_single_instance(instance.name, {QStringLiteral("dead")}));

    patchy::ui::SingleInstanceServer replacement;
    CHECK(!replacement.listen(instance.name));  // the name is taken by the dead file
    CHECK(patchy::ui::claim_single_instance(replacement, instance.name, {}) == patchy::ui::SingleInstanceRole::Primary);
    CHECK(patchy::ui::forward_to_single_instance(instance.name, {QStringLiteral("alive")}));
    std::vector<QStringList> received;
    CHECK(wait_for_requests(replacement, received, 1));
    CHECK(received.size() == 1 && received[0] == QStringList{QStringLiteral("alive")});
  }
#endif
}

// A lock file left by a launch that died is cleared, and one that cannot be taken (its owner
// is alive and stuck) delays the start but never prevents it.
void ui_single_instance_stale_or_held_lock_does_not_block_startup() {
  {
    TestInstanceName instance;
    const auto lock_path = patchy::ui::single_instance_lock_path(instance.name);
    {
      QFile stale(lock_path);
      CHECK(stale.open(QIODevice::ReadWrite));
      stale.write("left by a launch that died\n");
      CHECK(stale.flush());  // before the time is set: a later write would make the file new again
      CHECK(stale.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-3600), QFileDevice::FileModificationTime));
    }
    patchy::ui::SingleInstanceServer server;
    CHECK(patchy::ui::claim_single_instance(server, instance.name, {}) == patchy::ui::SingleInstanceRole::Primary);
    CHECK(!QFile::exists(lock_path));  // removed as stale, taken, released
  }
  {
    TestInstanceName instance;
    const auto lock_path = patchy::ui::single_instance_lock_path(instance.name);
    QLockFile held(lock_path);
    held.setStaleLockTime(0);
    CHECK(held.tryLock(0));
    patchy::ui::SingleInstanceClaimOptions impatient;
    impatient.lock_wait_ms = 200;
    impatient.stale_lock_ms = 3600 * 1000;
    patchy::ui::SingleInstanceServer server;
    CHECK(patchy::ui::claim_single_instance(server, instance.name, {}, impatient) ==
          patchy::ui::SingleInstanceRole::Primary);
    // The live owner's lock was left alone, and a launch behind it still finds the instance.
    CHECK(held.isLocked() && QFile::exists(lock_path));
    patchy::ui::SingleInstanceServer later;
    CHECK(patchy::ui::claim_single_instance(later, instance.name, {QStringLiteral("later")}, impatient) ==
          patchy::ui::SingleInstanceRole::Forwarded);
    std::vector<QStringList> received;
    CHECK(wait_for_requests(server, received, 1));
    CHECK(received.size() == 1 && received[0] == QStringList{QStringLiteral("later")});
    held.unlock();
  }
}

// The server name is a persisted identifier (an old and a new Patchy must find each other);
// PATCHY_SINGLE_INSTANCE_NAME replaces it, and the lock sits where every process that shares
// the socket can see it.
void ui_single_instance_name_and_lock_path() {
  const EnvironmentVariableRestorer restore_name("PATCHY_SINGLE_INSTANCE_NAME");
  qunsetenv("PATCHY_SINGLE_INSTANCE_NAME");
  QString user = qEnvironmentVariable("USERNAME");
  if (user.isEmpty()) {
    user = qEnvironmentVariable("USER");
  }
  // (The suite clears FLATPAK_ID at startup, so this is the name outside a Flatpak.)
  CHECK(patchy::ui::single_instance_server_name() == QStringLiteral("Patchy-SingleInstance-") + user);
  qputenv("PATCHY_SINGLE_INSTANCE_NAME", "Patchy-Elsewhere");
  CHECK(patchy::ui::single_instance_server_name() == QStringLiteral("Patchy-Elsewhere"));

  CHECK(patchy::ui::single_instance_lock_path(QStringLiteral("Patchy-SingleInstance-some user")) ==
        QDir(QDir::tempPath()).filePath(QStringLiteral("Patchy-SingleInstance-some_user.lock")));
#ifndef Q_OS_WIN
  const auto flatpak_socket = QStringLiteral("/run/user/1000/app/com.rtsoft.patchy/Patchy-SingleInstance-me");
  CHECK(patchy::ui::single_instance_lock_path(flatpak_socket) == flatpak_socket + QStringLiteral(".lock"));
#endif
}

QString patchy_executable_beside_suite() {
  const QDir build_dir(QCoreApplication::applicationDirPath());
  for (const auto& name : {QStringLiteral("patchy.exe"), QStringLiteral("patchy"),
                           QStringLiteral("Patchy.app/Contents/MacOS/Patchy"),
                           QStringLiteral("patchy.app/Contents/MacOS/patchy")}) {
    if (QFileInfo(build_dir.filePath(name)).isExecutable() && !QFileInfo(build_dir.filePath(name)).isDir()) {
      return build_dir.filePath(name);
    }
  }
  return {};
}

// GitHub issue 722 with the real app: a file manager's "open these six files with Patchy"
// starts six processes at the same moment, with no Patchy running. One of them must end up
// holding all six documents and the other five must exit after handing theirs over. Before
// the startup lock each process found nobody listening, built its window and stayed. Then two
// more launches while that instance runs, the case that already worked.
void ui_single_instance_simultaneous_launches_share_one_window() {
  const auto exe = patchy_executable_beside_suite();
  if (exe.isEmpty()) {
    patchy::test::skip("the patchy executable is not built beside the suite");
  }
  TestInstanceName instance;
  QTemporaryDir temp;
  CHECK(temp.isValid());

  QStringList stems{QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three"), QStringLiteral("four"),
                    QString::fromUtf8(patchy::test::utf8_string(patchy::test::kUnicodePathStems[0])),
                    QString::fromUtf8(patchy::test::utf8_string(patchy::test::kUnicodePathStems[4])),
                    QStringLiteral("seven"), QStringLiteral("eight")};
  QStringList files;
  for (int index = 0; index < stems.size(); ++index) {
    QImage image(48, 32, QImage::Format_RGB32);
    image.fill(QColor::fromHsv(index * 40, 200, 220));
    files.push_back(temp.filePath(stems[index] + QStringLiteral(".png")));
    CHECK(image.save(files.back(), "PNG"));
  }
  const int kTogether = 6;  // started at once; the last two files come later

  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("PATCHY_SINGLE_INSTANCE_NAME"), instance.name);
  // A patchy executable older than that variable (a stale build beside a fresh suite) derives
  // its name from the user: give it one nobody has, so it can never reach the developer's own
  // running Patchy. It then fails this test instead.
  const auto nobody = QStringLiteral("patchy-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  environment.insert(QStringLiteral("USERNAME"), nobody);
  environment.insert(QStringLiteral("USER"), nobody);
  environment.insert(QStringLiteral("PATCHY_SETTINGS_DIR"), temp.filePath(QStringLiteral("settings")));
  environment.insert(QStringLiteral("PATCHY_USER_FONTS_DIR"), temp.filePath(QStringLiteral("user-fonts")));
  environment.insert(QStringLiteral("PATCHY_USER_SCRIPTS_DIR"), temp.filePath(QStringLiteral("user-scripts")));
  environment.insert(QStringLiteral("PATCHY_RECOVERY_DIR"), temp.filePath(QStringLiteral("recovery")));
  environment.insert(QStringLiteral("PATCHY_MCP_ENDPOINT"),
                     QStringLiteral("PatchyMcp-single-instance-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
  environment.insert(QStringLiteral("PATCHY_NO_SOUND"), QStringLiteral("1"));
  environment.insert(QStringLiteral("PATCHY_NO_UPDATE_CHECK"), QStringLiteral("1"));
  environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
  environment.remove(QStringLiteral("PATCHY_NO_SINGLE_INSTANCE"));
  environment.remove(QStringLiteral("PATCHY_HEADLESS"));
  environment.remove(QStringLiteral("PATCHY_STARTUP_TRACE"));

  std::vector<std::unique_ptr<QProcess>> launches;
  const auto stop_everything = qScopeGuard([&launches] {
    for (auto& process : launches) {
      if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(30000);
      }
    }
  });
  const auto launch = [&](const QStringList& arguments) -> QProcess& {
    auto process = std::make_unique<QProcess>();
    process->setProcessEnvironment(environment);
    const auto log = temp.filePath(QStringLiteral("launch-%1.log").arg(launches.size()));
    process->setStandardOutputFile(log);
    process->setStandardErrorFile(log, QIODevice::Append);
    process->start(exe, arguments);
    launches.push_back(std::move(process));
    return *launches.back();
  };
  const auto running_count = [&launches] {
    int running = 0;
    for (const auto& process : launches) {
      running += process->state() != QProcess::NotRunning ? 1 : 0;
    }
    return running;
  };
  const auto describe = [&] {
    for (std::size_t index = 0; index < launches.size(); ++index) {
      const auto& process = *launches[index];
      QFile log(temp.filePath(QStringLiteral("launch-%1.log").arg(index)));
      const auto text = log.open(QIODevice::ReadOnly) ? log.readAll().left(600) : QByteArray();
      std::printf("  launch %d: %s, exit %d %s\n", static_cast<int>(index),
                  process.state() == QProcess::NotRunning ? "ended" : "running", process.exitCode(),
                  text.simplified().constData());
    }
    std::fflush(stdout);
  };

  // The running instance answers what it has open: a forwarded script lists the documents.
  const auto script_path = temp.filePath(QStringLiteral("documents.js"));
  {
    QFile script(script_path);
    CHECK(script.open(QIODevice::WriteOnly | QIODevice::Text));
    script.write("var docs = app.documents;\n"
                 "for (var i = 0; i < docs.length; i++) console.log('doc\\t' + docs[i].path);\n");
  }
  int question = 0;
  const auto open_documents = [&]() -> QStringList {
    const auto output = temp.filePath(QStringLiteral("documents-%1.txt").arg(question++));
    auto& ask = launch({QStringLiteral("--run-script"), script_path, QStringLiteral("--script-output"), output});
    CHECK(ask.waitForStarted(30000));
    CHECK(ask.waitForFinished(kBackstopMs));
    CHECK(ask.exitStatus() == QProcess::NormalExit && ask.exitCode() == 0);
    QString text;
    const bool answered = process_events_until(
        [&] {
          QFile file(output);
          if (!file.open(QIODevice::ReadOnly)) {
            return false;
          }
          text = QString::fromUtf8(file.readAll());
          return text.contains(QStringLiteral("[done]")) || text.contains(QStringLiteral("[failed]"));
        },
        kBackstopMs);
    CHECK(answered && text.contains(QStringLiteral("[done]")));
    QStringList documents;
    for (const auto& line : text.split(QLatin1Char('\n'))) {
      if (line.startsWith(QStringLiteral("doc\t"))) {
        documents.push_back(QFileInfo(line.mid(4).trimmed()).canonicalFilePath());
      }
    }
    documents.sort();
    return documents;
  };
  const auto expected_documents = [&files](int count) {
    QStringList expected;
    for (int index = 0; index < count; ++index) {
      expected.push_back(QFileInfo(files[index]).canonicalFilePath());
    }
    expected.sort();
    return expected;
  };

  // Cold start: all at once, one file each.
  for (int index = 0; index < kTogether; ++index) {
    (void)launch({files[index]});
  }
  for (int index = 0; index < kTogether; ++index) {
    CHECK(launches[static_cast<std::size_t>(index)]->waitForStarted(30000));
  }
  const bool settled = process_events_until([&] { return running_count() <= 1; }, kBackstopMs);
  if (!settled || running_count() != 1) {
    std::printf("  %d of %d launches still run (one instance expected)\n", running_count(), kTogether);
    describe();
  }
  CHECK(settled);
  CHECK(running_count() == 1);
  for (const auto& process : launches) {
    if (process->state() == QProcess::NotRunning) {
      CHECK(process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0);
    }
  }
  auto documents = open_documents();
  if (documents != expected_documents(kTogether)) {
    std::printf("  the instance holds %d documents, %d expected\n", static_cast<int>(documents.size()), kTogether);
    describe();
  }
  CHECK(documents == expected_documents(kTogether));
  CHECK(running_count() == 1);

  // Warm: two more launches while the instance runs.
  auto& seventh = launch({files[6]});
  auto& eighth = launch({files[7]});
  CHECK(seventh.waitForStarted(30000) && eighth.waitForStarted(30000));
  CHECK(seventh.waitForFinished(kBackstopMs) && eighth.waitForFinished(kBackstopMs));
  CHECK(seventh.exitStatus() == QProcess::NormalExit && seventh.exitCode() == 0);
  CHECK(eighth.exitStatus() == QProcess::NormalExit && eighth.exitCode() == 0);
  documents = open_documents();
  CHECK(documents == expected_documents(static_cast<int>(files.size())));
  CHECK(running_count() == 1);
  // stop_everything ends the instance; its socket lived in instance.folder, removed with it.
}

}  // namespace

std::vector<patchy::test::TestCase> single_instance_tests() {
  return {
      {"ui_single_instance_first_claim_listens_and_later_launches_forward",
       ui_single_instance_first_claim_listens_and_later_launches_forward},
      {"ui_single_instance_claim_keeps_a_live_socket_and_replaces_a_dead_one",
       ui_single_instance_claim_keeps_a_live_socket_and_replaces_a_dead_one},
      {"ui_single_instance_stale_or_held_lock_does_not_block_startup",
       ui_single_instance_stale_or_held_lock_does_not_block_startup},
      {"ui_single_instance_name_and_lock_path", ui_single_instance_name_and_lock_path},
      {"ui_single_instance_simultaneous_launches_share_one_window",
       ui_single_instance_simultaneous_launches_share_one_window},
  };
}
