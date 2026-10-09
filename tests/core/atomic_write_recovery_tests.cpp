// The atomic byte writer every document format goes through, and the Qt-free half
// of the automatic document recovery store (docs/document-recovery.md).

#include "core/document_recovery_store.hpp"
#include "support/atomic_file_write.hpp"
#include "support/path_utils.hpp"

#include "core_test_support.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <winerror.h>
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

void recovery_sidecar_round_trips_unicode_title_and_path() {
  patchy::recovery::RecoveryEntry entry;
  entry.file_stem = "17";
  entry.title = utf8_string(kUnicodeCombinedStem) + ".psd";
  entry.original_path = utf8_string(kUnicodeDirName) + "/" + entry.title;
  entry.saved_at_unix_ms = 1758800000123;
  const auto text = patchy::recovery::encode_sidecar(entry);
  CHECK(text == "title=" + entry.title + "\npath=" + entry.original_path + "\nsavedAt=1758800000123\n");
  const auto decoded = patchy::recovery::decode_sidecar(text, "17");
  CHECK(decoded.has_value());
  CHECK(decoded->file_stem == "17");
  CHECK(decoded->title == entry.title);
  CHECK(decoded->original_path == entry.original_path);
  CHECK(decoded->saved_at_unix_ms == 1758800000123);
  // CRLF sidecars (a hand-edited file) and unknown keys decode the same way; a
  // file with no recognized key is rejected.
  const auto crlf = patchy::recovery::decode_sidecar("title=A\r\nfuture=1\r\npath=\r\nsavedAt=oops\r\n", "2");
  CHECK(crlf.has_value());
  CHECK(crlf->title == "A");
  CHECK(crlf->original_path.empty());
  CHECK(crlf->saved_at_unix_ms == 0);
  CHECK(!patchy::recovery::decode_sidecar("junk\n", "3").has_value());
  CHECK(!patchy::recovery::decode_sidecar("", "4").has_value());
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
      {"recovery_sidecar_round_trips_unicode_title_and_path", recovery_sidecar_round_trips_unicode_title_and_path},
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
