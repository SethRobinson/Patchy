# Open from Clipboard

File > Open from Clipboard (`file.open_clipboard`, action `fileOpenClipboardAction`)
creates a new, unsaved document directly from the current system clipboard image.
Windows and Linux use Ctrl+Alt+Shift+N; macOS uses Command+Option+Shift+N through
Qt's normal modifier mapping. The shortcut is editable in Preferences > Hotkeys.
The action works with an empty workspace or existing documents and bypasses the
New Document dialog. A preview edit lock refuses it, like File > New.

`create_clipboard_document` also serves New Document's Clipboard preset. It keeps
the image's full pixel dimensions, including high-DPI clipboard images, and alpha,
creates an 8-bit document with one Clipboard Image layer at (0, 0), assigns 72 PPI, fits oversized images
to the view, and marks the session modified so closing warns about unsaved work.
It reads the system image rather than Patchy's private editable-layer payload.
Text, file URLs without image data, and empty clipboards report the existing
"Clipboard does not contain an image" status without creating a tab. Copy an
image itself to use this command; Edit > Paste retains its separate Files as
Layers behavior for copied file URLs.

## Platforms

Native builds read `QClipboard::Clipboard` in response to the action. This uses
the ordinary clipboard on Windows, macOS, X11, and Wayland, not X11's primary
mouse selection. There is no polling or background image decoding to enable the
menu item; the click reads the current data and validates it. Qt's
[QClipboard documentation](https://doc.qt.io/qt-6/qclipboard.html) describes the
event-loop requirement on X11 and activation-based change notification on macOS.

The command is hidden, disabled, and has no trigger handler on WebAssembly,
while its identifier stays registered. In the pinned
[Qt 6.10.3 browser backend](https://github.com/qt/qtbase/blob/v6.10.3/src/plugins/platforms/wasm/qwasmclipboard.cpp),
`mimeData()` returns cached data; `sendClipboardData()` fills it from a browser
paste event and delivers Ctrl+V. An arbitrary menu click or Ctrl+Alt+Shift+N
does not refresh that cache. Exposing the desktop implementation would risk
opening an earlier image after the host clipboard had changed.

Copy in the other direction (Patchy to the browser clipboard) does not use
Qt's write path either. The same backend serializes a copied image to PNG and
calls `navigator.clipboard.write()` with a Blob built by qstdweb's
`Blob::fromArrayBuffer`, which carries no MIME type. Chromium rejects a
`ClipboardItem` whose Blob type does not match its key (`NotAllowedError: Type
image/png does not match the blob's type`) and Qt only prints "clipboard error" on
the console, so Qt alone cannot copy an image out to a Chromium browser.
`set_system_clipboard_image` therefore also calls `wasm_clipboard::write_image_png` (`src/ui/clipboard_wasm.cpp`),
which encodes the PNG and writes it through a typed Blob from the same user
gesture; Qt's own rejected write still logs its warning. The Qt clipboard is
still set so the internal paste signature and the New Document Clipboard preset
keep working. A refused write (insecure context, no user activation, permission
denied) only warns on the console; the copy stays usable inside Patchy. Text and
SVG copies go through Qt's write unchanged (strings need no Blob). The browser
pane inside Claude's desktop app denies clipboard permissions outright, so
verify this in a real browser.

A future browser implementation needs a fresh, asynchronous read, explicit
permission/error handling, and protection against applying a late result to a
closed or locked workspace. Browser reads require a secure context and may need
user activation, permission, or a browser paste prompt; embedded frames also have
permissions-policy restrictions. See the
[Clipboard API](https://developer.mozilla.org/en-US/docs/Web/API/Clipboard/read)
and [wasm.md](wasm.md) for Patchy's promise/event-loop constraints. Do not enable
the action on the strength of `QClipboard::image()` returning a cached image.

## Legal review (2026-10-04)

Assessment: low risk for this independently implemented, user-invoked raster
command and shortcut. This is a scoped engineering review, not a worldwide
freedom-to-operate opinion or a guarantee about undiscovered claims.

- Copyright: the U.S. Copyright Office excludes methods of operation and short
  phrases from copyright protection. The functional command/shortcut can be
  implemented independently; no Affinity code, assets, documentation, or branding
  is copied. [Copyright Office FAQ](https://www.copyright.gov/help/faq/faq-protect.html).
- Published precedent: GIMP 2.4's Paste as New Image documentation records a
  2006-07-27 revision and describes creating a new image at the copied selection's
  dimensions with transparent uncovered regions.
  [GIMP manual](https://docs.gimp.org/2.4/en/gimp-edit-paste-as.html).
- Patent searches covered clipboard/new-image/new-document and clipboard
  shortcuts, including Serif/Affinity and Adobe terms. No blocking claim was
  identified for this design. Two relevant granted patents were read at the
  independent-claim level, rather than assuming that copyright also clears patents.
- [Dropbox US12099886B2](https://patents.google.com/patent/US12099886B2/en),
  independent claims 1, 17, and 20, require determining a file format from
  clipboard metadata and creating a stored content item in a content management
  system. Patchy creates an unsaved in-memory raster document; this command
  selects no output file format or destination and invokes no content-management
  service. The patent's background itself describes photo editors creating new
  in-memory image containers from clipboard contents. Do not extend this review
  to one-click clipboard-to-file/cloud storage.
- [Adobe US9396176B2](https://patents.google.com/patent/US9396176B2/en),
  independent claims 1 and 9, require stored associations between multiple
  destination locations in a target document and key sequences, followed by
  copying selected source content and pasting to the associated destination.
  Patchy's shortcut creates a new document from an existing clipboard image; it
  stores no destination associations and performs no source-copy step.

## Verification

The `ui_open_clipboard` tests exercise empty-workspace creation, File-menu
placement, shortcut dispatch with an existing document, unchanged source history,
replacement of a private copy with a current external image, exact RGBA bytes and
high-DPI dimensions, 72 PPI, unsaved/pathless state, and invalid clipboard refusal.
Run alongside `ui_new_document_presets_and_clipboard`, `ui_hotkey`,
`ui_no_widget_ships_unresolved_theme_tokens`, and the translation build gate.
