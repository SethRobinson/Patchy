# Third-Party Notices

Patchy's current Windows release package uses the Qt 6 desktop runtime and
app-local Microsoft Visual C++ runtime DLLs. Planned libraries that are not
linked into the application are intentionally not listed here.

## Qt 6

The Windows application dynamically links Qt 6 modules from the local Qt
installation used for the release build:

- Qt Core
- Qt GUI
- Qt Widgets
- Qt PrintSupport
- Qt SVG

The package also includes the Qt plugins needed by the current app:

- `platforms/qwindows.dll`
- `styles/qmodernwindowsstyle.dll`
- `iconengines/qsvgicon.dll`
- `imageformats/qjpeg.dll`
- `imageformats/qsvg.dll`

Qt is available under a commercial Qt license or under open-source licenses.
This local zip uses dynamic Qt DLLs and includes the Qt module SPDX notice files
under `licenses/qt/` so downstream review can see the exact Qt build metadata,
third-party components, and license text extracted by Qt.

## Microsoft Visual C++ Runtime DLLs

The Windows package includes the app-local Microsoft Visual C++ runtime DLLs
from the local Visual Studio redistributable CRT directory. These DLLs are
provided under Microsoft's runtime redistribution terms.
