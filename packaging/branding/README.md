# Patchy branding

`patchy-logo-folded.svg` is the application logo: the charcoal folded P on a
yellow tile, which fills the canvas edge to edge so taskbar and launcher icons
are as large as their neighbors. Desktop window icons, the welcome
panel, Help > About, Linux launchers, macOS bundles, and the web loading screen
use this source. It keeps its authored colors in both interface themes. The
flat and outline SVGs remain design source material.

`patchy-icon-ios-folded.svg` has an opaque square background for browser and
home-screen icons whose platform supplies the outer shape. Its export is an
ordinary icon, not a maskable icon with a guaranteed circular safe zone.

Regenerate checked-in platform assets from the repository root:

```powershell
python -m pip install PySide6-Essentials==6.8.3
python scripts/dev/generate-branding.py
```

The script renders through Qt SVG, supersamples the small sizes, and writes the
Windows ICO, macOS ICNS with Retina sizes, Linux hicolor PNGs, and web PNGs.
Only the ICNS is inset: macOS Dock icons place the tile in the middle 824 of a
1024 canvas, and the script adds that margin while rendering. Do not add a
margin to the SVG; anything that wants breathing room (`SplashArtwork`, the web
loader) adds its own.
Builds consume the checked-in files and do not require Python Qt bindings.
The Windows installer reads its packaged ICO instead of redrawing a second logo.
Linux additionally installs the original SVG under hicolor/scalable/apps.

The web build stages branding through `scripts/wasm/stage-branding.ps1`.
The shell declares an SVG favicon with an ICO fallback, a 180 px Apple touch
icon, and a manifest with 192/512 px icons. Every URL, including the manifest's
icon URLs, uses the deployment cache tag. The manifest launches in the browser;
it makes no offline support promise. Both production and beta upload lists
include every branding asset before publishing HTML.

Welcome and About share `SplashArtwork`, which renders the embedded SVG at the
display's pixel density. The welcome header pairs it with the existing title
and tagline; About aligns it with the information column. The browser loader
uses a larger centered logo and the brand yellow for download progress.
