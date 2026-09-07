#pragma once
#include <QByteArray>
#include <algorithm>

namespace patchy::ui {
// Retains at most the input limit plus one byte, allowing a protocol error at
// the next newline without retaining an unbounded malformed message.
class McpLineBuffer {
 public:
  template <typename Receive> void append(const QByteArray& bytes, Receive receive) {
    qsizetype offset = 0;
    while (offset < bytes.size()) {
      const auto newline = bytes.indexOf('\n', offset);
      const auto end = newline < 0 ? bytes.size() : newline;
      const auto count = std::min(end - offset, kLimit + 1 - line_.size());
      if (count > 0) { line_.append(bytes.constData() + offset, count); }
      if (newline < 0) { break; }
      receive(line_);
      line_.clear();
      offset = newline + 1;
    }
  }
 private:
  static constexpr qsizetype kLimit = 16 * 1024 * 1024;
  QByteArray line_;
};
}  // namespace patchy::ui
