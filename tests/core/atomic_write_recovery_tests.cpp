// The atomic byte writer every document format goes through, and the Qt-free half
// of the automatic document recovery store (docs/document-recovery.md).

#include "core/document_recovery_store.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/palette_io.hpp"
#include "support/atomic_file_write.hpp"
#include "support/path_utils.hpp"

#include "core_test_support.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#include <winerror.h>
#else
#include <unistd.h>
#endif

using patchy::test::directory_holds_only;
using patchy::test::kUnicodeCombinedStem;
using patchy::test::kUnicodeDirName;
using patchy::test::read_binary_file;
using patchy::test::unicode_artifact_dir;
using patchy::test::unicode_path_piece;
using patchy::test::utf8_string;

namespace {

std::filesystem::path fresh_artifact_dir(const char* leaf) {
  const auto dir = std::filesystem::path("test-artifacts") / "atomic-write" / leaf;
  patchy::test::remove_test_scratch_tree(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

std::vector<std::uint8_t> bytes_of(const std::string& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string text_of(const std::filesystem::path& path) {
  const auto bytes = read_binary_file(path);
  return std::string(bytes.begin(), bytes.end());
}

void create_fixture_hard_link(const std::filesystem::path& target,
                             const std::filesystem::path& link, std::error_code& error) {
#if defined(__EMSCRIPTEN__)
  // Emscripten has no link/linkat syscall, even with NODERAWFS. Create the host
  // fixture through Node so the real atomic writer still checks its link count.
  const auto target_text = target.u8string();
  const auto link_text = link.u8string();
  const int failed = EM_ASM_INT({
    try {
      require('fs').linkSync(UTF8ToString(arguments[0]), UTF8ToString(arguments[1]));
      return 0;
    } catch (e) {
      console.error('Hard-link fixture: ' + e.message);
      return 1;
    }
  }, target_text.c_str(), link_text.c_str());
  error = failed ? std::make_error_code(std::errc::io_error) : std::error_code{};
#else
  std::filesystem::create_hard_link(target, link, error);
#endif
}

bool scratch_remove_refuses(const std::filesystem::path& path) {
  try {
    (void)patchy::test::remove_test_scratch_tree(path);
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

std::filesystem::path scratch_host_absolute(const std::filesystem::path& path) {
#if defined(__EMSCRIPTEN__)
  // musl getcwd rejects a Windows cwd because it does not start with '/'.
  std::error_code error;
  const auto absolute = patchy::test::resolve_node_test_scratch_path(path, true, error);
  CHECK(!error);
  return absolute;
#else
  return std::filesystem::absolute(path);
#endif
}

// Non-recursive cleanup of the test-owned roots that the recursive guard protects.
bool remove_scratch_entry(const std::filesystem::path& path) {
#if defined(__EMSCRIPTEN__)
  const auto text = path.u8string();
  return EM_ASM_INT({
    try {
      const fs = require('fs');
      const path = UTF8ToString(arguments[0]);
      if (fs.lstatSync(path).isDirectory()) fs.rmdirSync(path);
      else fs.unlinkSync(path);
      return 1;
    } catch (e) {
      return e.code === 'ENOENT' ? 1 : 0;
    }
  }, text.c_str()) != 0;
#else
  std::error_code error;
  std::filesystem::remove(path, error);
  return !error;
#endif
}

}  // namespace

void atomic_write_replaces_existing_file_and_leaves_no_temp() {
  const auto dir = fresh_artifact_dir("replace");
  const auto target = dir / "document.bin";
  patchy::write_file_bytes_atomically(target, bytes_of("first"), "open failed", "write failed");
  CHECK(text_of(target) == "first");
  // The second write replaces the first through the rename; the old bytes are
  // never truncated away before the new ones exist, and no temporary survives.
  patchy::write_file_bytes_atomically(target, bytes_of("second, longer"), "open failed", "write failed");
  CHECK(text_of(target) == "second, longer");
  CHECK(directory_holds_only(dir, {target}));
  // A shorter rewrite must not leave a tail of the longer file behind.
  patchy::write_file_bytes_atomically(target, bytes_of("3"), "open failed", "write failed");
  CHECK(text_of(target) == "3");
  CHECK(directory_holds_only(dir, {target}));
}

void atomic_write_missing_directory_throws_and_keeps_nothing() {
  const auto dir = fresh_artifact_dir("missing");
  const auto target = dir / "no-such-folder" / "document.bin";
  bool threw = false;
  try {
    patchy::write_file_bytes_atomically(target, bytes_of("x"), "open message", "write message");
  } catch (const std::runtime_error& error) {
    threw = std::string(error.what()) == "open message";
  }
  CHECK(threw);
  CHECK(!std::filesystem::exists(target));
  CHECK(directory_holds_only(dir, {}));
}

void atomic_write_failed_rename_keeps_old_file_and_removes_temp() {
  const auto dir = fresh_artifact_dir("rename-fails");
  // A directory in the target's place cannot be replaced by the rename: the
  // helper reports the write message and cleans up its temporary file, and the
  // directory (standing in for "the old file") is untouched.
  const auto target = dir / "document.bin";
  std::filesystem::create_directories(target / "child");
  bool threw = false;
  try {
    patchy::write_file_bytes_atomically(target, bytes_of("x"), "open message", "write message");
  } catch (const std::runtime_error& error) {
    threw = std::string(error.what()) == "write message";
  }
  CHECK(threw);
  CHECK(std::filesystem::is_directory(target / "child"));
  CHECK(directory_holds_only(dir, {target}));
}

// The streaming form (PDF export): the target keeps its old bytes while the new file
// is written beside it, a commit replaces it, and anything short of a commit leaves
// the old file and no temporary.
void atomic_write_streaming_replacement_commits_or_discards() {
  const auto dir = fresh_artifact_dir("streaming");
  const auto target = dir / unicode_path_piece(kUnicodeCombinedStem);
  patchy::write_file_bytes_atomically(target, bytes_of("old"), "open failed", "write failed");
  {
    patchy::AtomicFileReplacement replacement(target);
    CHECK(replacement.target_path() == target);
    CHECK(replacement.temporary_path().parent_path() == dir);
    std::ofstream(replacement.temporary_path(), std::ios::binary) << "abandoned";
    CHECK(text_of(target) == "old");
    // Destroyed without commit: a cancelled or failed export.
  }
  CHECK(text_of(target) == "old");
  CHECK(directory_holds_only(dir, {target}));
  {
    patchy::AtomicFileReplacement replacement(target);
    std::ofstream(replacement.temporary_path(), std::ios::binary) << "discarded";
    replacement.discard();
    replacement.discard();
    CHECK(directory_holds_only(dir, {target}));
  }
  {
    patchy::AtomicFileReplacement replacement(target);
    std::ofstream(replacement.temporary_path(), std::ios::binary) << "new";
    replacement.commit("write failed");
    // A second commit is a caller bug, reported like a failed write.
    bool threw = false;
    try {
      replacement.commit("write failed");
    } catch (const std::runtime_error&) {
      threw = true;
    }
    CHECK(threw);
  }
  CHECK(text_of(target) == "new");
  CHECK(directory_holds_only(dir, {target}));
  // A rename that cannot replace the target (a directory stands in its place) throws
  // the write message and removes the temporary.
  const auto blocked = dir / "blocked.pdf";
  std::filesystem::create_directories(blocked / "child");
  bool threw = false;
  {
    patchy::AtomicFileReplacement replacement(blocked);
    std::ofstream(replacement.temporary_path(), std::ios::binary) << "x";
    try {
      replacement.commit("write message");
    } catch (const std::runtime_error& error) {
      threw = std::string(error.what()) == "write message";
    }
  }
  CHECK(threw);
  CHECK(std::filesystem::is_directory(blocked / "child"));
  CHECK(directory_holds_only(dir, {target, blocked}));
}

// The temporary is reserved by exclusive creation under a name nobody can plan for,
// so an entry planted beside the target (a hard link to a canary at the name an older
// Patchy would have used) is never opened, and the canary keeps its bytes.
void atomic_write_never_opens_a_planted_entry_beside_the_target() {
  const auto dir = fresh_artifact_dir("planted");
  const auto target = dir / "document.bin";
  const auto canary = dir / "canary.txt";
  std::ofstream(canary, std::ios::binary) << "canary";
#if defined(_WIN32)
  const auto pid = static_cast<long long>(_getpid());
#else
  const auto pid = static_cast<long long>(getpid());
#endif
  // The old `<name>.<pid>-<counter>.patchy-tmp` names for the first few counters.
  std::vector<std::filesystem::path> planted;
  for (int counter = 0; counter < 4; ++counter) {
    auto name = target.filename();
    name += "." + std::to_string(pid) + "-" + std::to_string(counter) + std::string(patchy::kAtomicTemporarySuffix);
    planted.push_back(dir / name);
    std::error_code error;
    create_fixture_hard_link(canary, planted.back(), error);
    CHECK(!error);
  }
  patchy::write_file_bytes_atomically(target, bytes_of("saved"), "open failed", "write failed");
  CHECK(text_of(target) == "saved");
  CHECK(text_of(canary) == "canary");
  for (const auto& path : planted) {
    CHECK(text_of(path) == "canary");
  }
  // Two reservations for one target never share a name, and each is created on the
  // spot (empty) and removed when abandoned.
  {
    patchy::AtomicFileReplacement first(target);
    patchy::AtomicFileReplacement second(target);
    CHECK(first.temporary_path() != second.temporary_path());
    CHECK(std::filesystem::is_regular_file(first.temporary_path()));
    CHECK(std::filesystem::is_regular_file(second.temporary_path()));
    CHECK(std::filesystem::file_size(first.temporary_path()) == 0);
    CHECK(patchy::path_to_utf8(first.temporary_path().filename()).ends_with(std::string(patchy::kAtomicTemporarySuffix)));
  }
  std::vector<std::filesystem::path> expected{target, canary};
  expected.insert(expected.end(), planted.begin(), planted.end());
  CHECK(directory_holds_only(dir, expected));
}

// commit() renames only a plain single-link regular file: a hard link or a symlink
// swapped in under the reserved name is refused, the target keeps its bytes, and the
// swapped-in entry's own target is untouched.
void atomic_write_commit_refuses_a_link_under_the_reserved_name() {
  namespace fs = std::filesystem;
  const auto dir = fresh_artifact_dir("swapped-link");
  const auto target = dir / "document.bin";
  const auto canary = dir / "canary.txt";
  patchy::write_file_bytes_atomically(target, bytes_of("old"), "open failed", "write failed");
  std::ofstream(canary, std::ios::binary) << "canary";
  {
    patchy::AtomicFileReplacement replacement(target);
    std::error_code error;
    fs::remove(replacement.temporary_path(), error);
    create_fixture_hard_link(canary, replacement.temporary_path(), error);
    CHECK(!error);
    bool threw = false;
    try {
      replacement.commit("write message");
    } catch (const std::runtime_error& error_thrown) {
      threw = std::string(error_thrown.what()) == "write message";
    }
    CHECK(threw);
  }
  CHECK(text_of(target) == "old");
  CHECK(text_of(canary) == "canary");
  // The reserved name is removed on failure (the link, never the canary).
  CHECK(directory_holds_only(dir, {target, canary}));
  {
    patchy::AtomicFileReplacement replacement(target);
    std::error_code error;
    fs::remove(replacement.temporary_path(), error);
    fs::create_symlink(canary, replacement.temporary_path(), error);
    bool symlinks_unavailable = error == std::errc::permission_denied ||
        error == std::errc::operation_not_permitted || error == std::errc::function_not_supported ||
        error == std::errc::operation_not_supported;
#if defined(_WIN32)
    symlinks_unavailable = symlinks_unavailable ||
        error == std::error_code(ERROR_PRIVILEGE_NOT_HELD, std::system_category());
#endif
    if (symlinks_unavailable) {
      std::cout << "[SKIP] symlink half of atomic_write_commit_refuses_a_link_under_the_reserved_name: "
                << error.message() << '\n';
      return;
    }
    CHECK(!error);
    bool threw = false;
    try {
      replacement.commit("write message");
    } catch (const std::runtime_error&) {
      threw = true;
    }
    CHECK(threw);
  }
  CHECK(text_of(target) == "old");
  CHECK(text_of(canary) == "canary");
  CHECK(directory_holds_only(dir, {target, canary}));
}

// Palette and Curves exports go through the same replacement write: an export over
// an existing preset replaces it whole, a failed one keeps it, and nothing else is
// left in the folder.
void palette_and_curves_exports_replace_atomically() {
  const auto dir = fresh_artifact_dir("presets");
  const std::array<patchy::RgbColor, 2> first = {{{1, 2, 3}, {4, 5, 6}}};
  const std::array<patchy::RgbColor, 3> second = {{{7, 8, 9}, {10, 11, 12}, {13, 14, 15}}};
  const auto gpl = dir / "swatches.gpl";
  patchy::palette_io::write_palette_file(gpl, first, patchy::palette_io::PaletteFileFormat::Gpl, "Swatches");
  patchy::palette_io::write_palette_file(gpl, second, patchy::palette_io::PaletteFileFormat::Gpl, "Swatches");
  CHECK(read_binary_file(gpl) ==
        patchy::palette_io::write_palette_bytes(second, patchy::palette_io::PaletteFileFormat::Gpl, "Swatches"));
  CHECK(patchy::palette_io::read_palette_file(gpl).colors.size() == 3);

  const auto acv = dir / "curve.acv";
  patchy::CurvesAdjustment curves;
  curves.rgb = {{0, 0}, {64, 48}, {255, 255}};
  patchy::acv::write_file(acv, curves);
  curves.rgb = {{0, 0}, {128, 200}, {255, 255}};
  patchy::acv::write_file(acv, curves);
  CHECK(patchy::acv::read_file(acv) == curves);
  CHECK(read_binary_file(acv) == patchy::acv::write(curves));
  CHECK(directory_holds_only(dir, {gpl, acv}));

  // A target that cannot be replaced (a directory in its place) reports the writer's
  // own message, and the folder holds nothing new afterwards.
  const auto blocked_gpl = dir / "blocked.gpl";
  const auto blocked_acv = dir / "blocked.acv";
  std::filesystem::create_directories(blocked_gpl / "child");
  std::filesystem::create_directories(blocked_acv / "child");
  bool threw = false;
  try {
    patchy::palette_io::write_palette_file(blocked_gpl, first, patchy::palette_io::PaletteFileFormat::Gpl, "x");
  } catch (const std::runtime_error& error) {
    threw = std::string(error.what()) == "Could not write palette file";
  }
  CHECK(threw);
  threw = false;
  try {
    patchy::acv::write_file(blocked_acv, curves);
  } catch (const std::runtime_error& error) {
    threw = std::string(error.what()) == "Could not write Curves preset";
  }
  CHECK(threw);
  CHECK(std::filesystem::is_directory(blocked_gpl / "child"));
  CHECK(std::filesystem::is_directory(blocked_acv / "child"));
  CHECK(directory_holds_only(dir, {gpl, acv, blocked_gpl, blocked_acv}));
}

void recovery_sidecar_round_trips_unicode_title_and_path() {
  patchy::recovery::RecoveryEntry entry;
  entry.file_stem = "17";
  entry.title = utf8_string(kUnicodeCombinedStem) + ".psd";
  entry.original_path = utf8_string(kUnicodeDirName) + "/" + entry.title;
  entry.saved_at_unix_ms = 1758800000123;
  const auto text = patchy::recovery::encode_sidecar(entry);
  CHECK(text == "format=2\ntitle=" + entry.title + "\npath=" + entry.original_path + "\nsavedAt=1758800000123\n");
  const auto decoded = patchy::recovery::decode_sidecar(text, "17");
  CHECK(decoded.has_value());
  CHECK(decoded->file_stem == "17");
  CHECK(decoded->title == entry.title);
  CHECK(decoded->original_path == entry.original_path);
  CHECK(decoded->saved_at_unix_ms == 1758800000123);
  // CRLF sidecars (a hand-edited file) and unknown keys decode the same way; a
  // file with no recognized key is rejected.
  const auto crlf =
      patchy::recovery::decode_sidecar("format=2\r\ntitle=A\r\nfuture=1\r\npath=\r\nsavedAt=oops\r\n", "2");
  CHECK(crlf.has_value());
  CHECK(crlf->title == "A");
  CHECK(crlf->original_path.empty());
  CHECK(crlf->saved_at_unix_ms == 0);
  CHECK(!patchy::recovery::decode_sidecar("junk\n", "3").has_value());
  CHECK(!patchy::recovery::decode_sidecar("", "4").has_value());
  CHECK(!patchy::recovery::decode_sidecar("format=2\n", "5").has_value());
}

// Format 2 escapes the characters that could break the line layout, a sidecar from
// before format 2 decodes verbatim (Windows paths there hold raw backslashes), and a
// sidecar with a repeated key (what an unescaped line break in a title would have
// produced) keeps the document but drops the path, so Save can never be redirected.
void recovery_sidecar_escapes_line_breaks_and_rejects_duplicate_keys() {
  patchy::recovery::RecoveryEntry entry;
  entry.file_stem = "9";
  entry.title = "line one\npath=/tmp/evil.psd\r\nback\\slash = equals";
  entry.original_path = "/home/me/odd\nname/a=b\\c.psd";
  entry.saved_at_unix_ms = 42;
  const auto text = patchy::recovery::encode_sidecar(entry);
  // Exactly four lines: no value ever starts a line of its own.
  CHECK(std::count(text.begin(), text.end(), '\n') == 4);
  CHECK(text.find("\npath=/tmp/evil.psd") == std::string::npos);
  const auto decoded = patchy::recovery::decode_sidecar(text, "9");
  CHECK(decoded.has_value());
  CHECK(decoded->title == entry.title);
  CHECK(decoded->original_path == entry.original_path);
  CHECK(decoded->saved_at_unix_ms == 42);

  // Legacy (no format line): raw backslashes and `=` in values are literal.
  const auto legacy = patchy::recovery::decode_sidecar(
      "title=Poster.psd\npath=C:\\art\\new=old\\Poster.psd\nsavedAt=5\n", "3");
  CHECK(legacy.has_value());
  CHECK(legacy->title == "Poster.psd");
  CHECK(legacy->original_path == "C:\\art\\new=old\\Poster.psd");
  CHECK(legacy->saved_at_unix_ms == 5);
  // An escape the writer never produces stays literal in format 2.
  const auto odd = patchy::recovery::decode_sidecar("format=2\ntitle=a\\qb\\\n", "4");
  CHECK(odd.has_value());
  CHECK(odd->title == "a\\qb\\");

  // A crafted or damaged sidecar: the second `path=` wins nothing. The first title and
  // timestamp stay, the path goes.
  const auto injected = patchy::recovery::decode_sidecar(
      "title=Innocent\npath=/tmp/evil.psd\npath=/home/me/real.psd\nsavedAt=7\n", "5");
  CHECK(injected.has_value());
  CHECK(injected->title == "Innocent");
  CHECK(injected->original_path.empty());
  CHECK(injected->saved_at_unix_ms == 7);
  const auto doubled_title = patchy::recovery::decode_sidecar(
      "format=2\ntitle=First\ntitle=Second\npath=/home/me/real.psd\nsavedAt=1\nsavedAt=2\n", "6");
  CHECK(doubled_title.has_value());
  CHECK(doubled_title->title == "First");
  CHECK(doubled_title->original_path.empty());
  CHECK(doubled_title->saved_at_unix_ms == 1);
}

// The names the store itself creates, which is what the folder sweep may delete.
void recovery_store_file_names_are_recognized() {
  using patchy::recovery::is_store_file_name;
  CHECK(is_store_file_name("12.psb"));
  CHECK(is_store_file_name("12.recovery"));
  CHECK(is_store_file_name("12.psb.4242-0-0123456789abcdef.patchy-tmp"));
  CHECK(is_store_file_name("12.recovery.4242-1.patchy-tmp"));
  CHECK(!is_store_file_name("lock"));
  CHECK(!is_store_file_name("photo.jpg"));
  CHECK(!is_store_file_name("stray.psb"));
  CHECK(!is_store_file_name("12345678901234567890.psb"));
  CHECK(!is_store_file_name(".psb"));
  CHECK(!is_store_file_name("12.psd"));
  CHECK(!is_store_file_name("holiday.psb.patchy-tmp"));
  CHECK(!is_store_file_name("12.patchy-tmp"));
  CHECK(!is_store_file_name(""));
}

void recovery_scan_lists_psb_without_sidecar_as_untitled_and_ignores_strays() {
  const auto dir = fresh_artifact_dir("scan");
  patchy::recovery::RecoveryEntry titled;
  titled.title = "Poster.psd";
  titled.original_path = "C:/art/Poster.psd";
  titled.saved_at_unix_ms = 5;
  patchy::recovery::write_entry(dir, 12, bytes_of("psb-12"), titled);
  patchy::recovery::write_entry(dir, 3, bytes_of("psb-3"), patchy::recovery::RecoveryEntry{});
  // A PSB without a sidecar (crash between the two writes) still lists.
  std::ofstream(dir / "7.psb", std::ios::binary) << "psb-7";
  // Strays: a lock file, a temp file, a sidecar with no PSB, an unrelated file, and
  // a PSB whose stem is not a session id (the reopen would std::stoll it).
  std::ofstream(dir / "lock") << "x";
  std::ofstream(dir / "9.psb.123-4.patchy-tmp") << "x";
  std::ofstream(dir / "8.recovery") << "title=orphan sidecar\n";
  std::ofstream(dir / "notes.txt") << "x";
  std::ofstream(dir / "stray.psb", std::ios::binary) << "psb-x";
  std::ofstream(dir / "12345678901234567890.psb", std::ios::binary) << "psb-x";

  const auto entries = patchy::recovery::scan_instance_dir(dir);
  CHECK(entries.size() == 3);
  if (entries.size() == 3) {
    // Sorted by session id (creation order), not by stem text.
    CHECK(entries[0].file_stem == "3");
    CHECK(entries[0].title.empty());
    CHECK(entries[0].original_path.empty());
    CHECK(entries[1].file_stem == "7");
    CHECK(entries[1].title.empty());
    CHECK(entries[2].file_stem == "12");
    CHECK(entries[2].title == "Poster.psd");
    CHECK(entries[2].original_path == "C:/art/Poster.psd");
    CHECK(entries[2].saved_at_unix_ms == 5);
  }
  CHECK(text_of(patchy::recovery::document_path(dir, 12)) == "psb-12");
  CHECK(std::filesystem::exists(patchy::recovery::sidecar_path(dir, 12)));

  patchy::recovery::remove_entry(dir, 12);
  CHECK(!std::filesystem::exists(patchy::recovery::document_path(dir, 12)));
  CHECK(!std::filesystem::exists(patchy::recovery::sidecar_path(dir, 12)));
  patchy::recovery::remove_entry(dir, 12);  // idempotent
  CHECK(patchy::recovery::scan_instance_dir(dir).size() == 2);
  CHECK(patchy::recovery::scan_instance_dir(dir / "does-not-exist").empty());
}

void recovery_write_entry_creates_instance_dir_under_unicode_root() {
  const auto root = unicode_artifact_dir(u8"recovery-store");
  const auto instance = root / unicode_path_piece(kUnicodeCombinedStem);
  patchy::recovery::RecoveryEntry entry;
  entry.title = utf8_string(kUnicodeCombinedStem);
  patchy::recovery::write_entry(instance, 1, bytes_of("psb-1"), entry);
  CHECK(directory_holds_only(root, {instance}));
  CHECK(directory_holds_only(instance, {patchy::recovery::document_path(instance, 1),
                                        patchy::recovery::sidecar_path(instance, 1)}));
  const auto entries = patchy::recovery::scan_instance_dir(instance);
  CHECK(entries.size() == 1);
  if (!entries.empty()) {
    CHECK(entries.front().title == utf8_string(kUnicodeCombinedStem));
  }
}

// The guarded recursive delete every test uses refuses a blank path and anything not
// strictly below a test-artifacts or qttest folder, and deletes what is below one.
void test_scratch_remove_refuses_paths_outside_scratch_roots() {
  const auto refuses = scratch_remove_refuses;
  CHECK(refuses(std::filesystem::path()));
  CHECK(refuses(std::filesystem::path(".")));
  CHECK(refuses(std::filesystem::path("/")));
  CHECK(refuses(std::filesystem::path("scratch-remove-guard")));
  CHECK(refuses(std::filesystem::path("test-artifacts")));
  CHECK(refuses(std::filesystem::path("test-artifacts/")));
  CHECK(refuses(std::filesystem::path("test-artifacts/..")));
  CHECK(refuses(std::filesystem::path("test-artifacts/scratch-remove-guard/../..")));
  const auto cwd = scratch_host_absolute(".");
  CHECK(refuses(cwd));
  CHECK(refuses(cwd.root_path()));
  CHECK(refuses(std::filesystem::path("qttest")));
  CHECK(refuses(std::filesystem::path(".qttest/")));
  CHECK(!patchy::test::is_below_test_scratch_root(cwd));

  const auto dir = std::filesystem::path("test-artifacts") / "scratch-remove-guard";
  std::filesystem::create_directories(dir / "child");
  std::ofstream(dir / "child" / "file.txt") << "x";
  CHECK(patchy::test::remove_test_scratch_tree(dir / "child"));
  CHECK(!std::filesystem::exists(dir / "child"));
  CHECK(std::filesystem::exists(dir));
  CHECK(patchy::test::remove_test_scratch_tree(dir));
  CHECK(!std::filesystem::exists(dir));
  CHECK(patchy::test::remove_test_scratch_tree(dir));  // already gone

  // QTemporaryDir-style folders: below <temp>/patchy*, never the temp folder itself.
  const auto temp = std::filesystem::temp_directory_path();
  CHECK(refuses(temp));
  CHECK(refuses(temp / "unrelated-folder" / "child"));
  CHECK(refuses(temp / "patchy-scratch-remove-guard"));
  const auto owned = temp / "patchy-scratch-remove-guard";
  std::filesystem::create_directories(owned / "child");
  CHECK(patchy::test::remove_test_scratch_tree(owned / "child"));
  CHECK(!std::filesystem::exists(owned / "child"));
  CHECK(remove_scratch_entry(owned));
}

void test_scratch_remove_handles_unicode_absolute_and_missing_paths() {
  const auto root = std::filesystem::path("test-artifacts") / "scratch-remove-paths";
  CHECK(patchy::test::remove_test_scratch_tree(root));
  const auto child = root / unicode_path_piece(kUnicodeDirName) / "child";
  CHECK(patchy::test::remove_test_scratch_tree(child));  // all parents are missing
  CHECK(!std::filesystem::exists(root));
  for (int pass = 0; pass != 2; ++pass) {
    std::filesystem::create_directories(child);
    std::ofstream(child / "sentinel.txt") << "owned";
    const auto target = pass == 0 ? child : scratch_host_absolute(child);
    CHECK(patchy::test::remove_test_scratch_tree(target / ""));  // trailing separator
    CHECK(!std::filesystem::exists(child));
    CHECK(std::filesystem::exists(child.parent_path()));
    CHECK(patchy::test::remove_test_scratch_tree(target));  // already gone
  }
  CHECK(patchy::test::remove_test_scratch_tree(root));
}

void test_scratch_remove_preserves_symlink_targets() {
  namespace fs = std::filesystem;
  const auto root = fs::path("test-artifacts") / "scratch-remove-links";
  CHECK(patchy::test::remove_test_scratch_tree(root));
  // Deliberately outside every allowed scratch root, but still a test-owned sibling
  // in the build directory. Cleanup uses only non-recursive removes of owned entries.
  const auto outside = scratch_host_absolute(".") /
      ("scratch-remove-outside-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  CHECK(fs::create_directory(outside));
  struct Cleanup {
    fs::path root;
    fs::path outside;
    ~Cleanup() {
      for (const auto& path : {root / "link", root / "dangling", root,
                               outside / "child" / "sentinel.txt", outside / "child", outside}) {
        (void)remove_scratch_entry(path);
      }
    }
  } cleanup{root, outside};
  fs::create_directories(root);
  fs::create_directories(outside / "child");
  std::ofstream(outside / "child" / "sentinel.txt") << "keep";
  std::error_code error;
  fs::create_directory_symlink(outside, root / "link", error);
  bool symlinks_unavailable = error == std::errc::permission_denied || error == std::errc::operation_not_permitted ||
      error == std::errc::function_not_supported || error == std::errc::operation_not_supported;
#if defined(_WIN32)
  // MSVC does not map this missing privilege to a generic permission error.
  symlinks_unavailable = symlinks_unavailable ||
      error == std::error_code(ERROR_PRIVILEGE_NOT_HELD, std::system_category());
#endif
  if (symlinks_unavailable) {
    std::cout << "[SKIP] test_scratch_remove_preserves_symlink_targets: directory symlinks unavailable: "
              << error.message() << '\n';
    return;
  }
  CHECK(!error);
  CHECK(scratch_remove_refuses(root / "link" / "child"));
  CHECK(text_of(outside / "child" / "sentinel.txt") == "keep");
  CHECK(patchy::test::remove_test_scratch_tree(root / "link"));
  CHECK(!fs::is_symlink(fs::symlink_status(root / "link")));
  CHECK(text_of(outside / "child" / "sentinel.txt") == "keep");
#if defined(__EMSCRIPTEN__)
  // The Node adapter must distinguish a missing parent from a dangling parent link.
  fs::create_directory_symlink(outside / "missing", root / "dangling");
  CHECK(scratch_remove_refuses(root / "dangling" / "child"));
  CHECK(patchy::test::remove_test_scratch_tree(root / "dangling"));
#endif
}

std::vector<patchy::test::TestCase> atomic_write_recovery_tests() {
  return {
      {"atomic_write_replaces_existing_file_and_leaves_no_temp", atomic_write_replaces_existing_file_and_leaves_no_temp},
      {"atomic_write_missing_directory_throws_and_keeps_nothing", atomic_write_missing_directory_throws_and_keeps_nothing},
      {"atomic_write_failed_rename_keeps_old_file_and_removes_temp",
       atomic_write_failed_rename_keeps_old_file_and_removes_temp},
      {"atomic_write_streaming_replacement_commits_or_discards", atomic_write_streaming_replacement_commits_or_discards},
      {"atomic_write_never_opens_a_planted_entry_beside_the_target",
       atomic_write_never_opens_a_planted_entry_beside_the_target},
      {"atomic_write_commit_refuses_a_link_under_the_reserved_name",
       atomic_write_commit_refuses_a_link_under_the_reserved_name},
      {"palette_and_curves_exports_replace_atomically", palette_and_curves_exports_replace_atomically},
      {"recovery_sidecar_round_trips_unicode_title_and_path", recovery_sidecar_round_trips_unicode_title_and_path},
      {"recovery_sidecar_escapes_line_breaks_and_rejects_duplicate_keys",
       recovery_sidecar_escapes_line_breaks_and_rejects_duplicate_keys},
      {"recovery_store_file_names_are_recognized", recovery_store_file_names_are_recognized},
      {"recovery_scan_lists_psb_without_sidecar_as_untitled_and_ignores_strays",
       recovery_scan_lists_psb_without_sidecar_as_untitled_and_ignores_strays},
      {"recovery_write_entry_creates_instance_dir_under_unicode_root",
       recovery_write_entry_creates_instance_dir_under_unicode_root},
      {"test_scratch_remove_refuses_paths_outside_scratch_roots",
       test_scratch_remove_refuses_paths_outside_scratch_roots},
      {"test_scratch_remove_handles_unicode_absolute_and_missing_paths",
       test_scratch_remove_handles_unicode_absolute_and_missing_paths},
      {"test_scratch_remove_preserves_symlink_targets", test_scratch_remove_preserves_symlink_targets},
  };
}
