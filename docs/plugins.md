# Plugins

Patchy's plugin system: the native C ABI plus the legacy Photoshop `.8bf` adapter.

## Native plugin ABI

- The supported surface is the C ABI in `src/plugins/plugin_api.h`. `PATCHY_PLUGIN_ABI_VERSION` must match the host exactly; bump it on any breaking ABI change.
- Plugin identifiers are reverse-DNS.
- Host-owned memory stays host-owned: plugins must never store raw document pointers after a call returns.

## Legacy Photoshop .8bf filters

- Legacy `.8bf` filters go through `src/plugins/legacy_photoshop_adapter.*`. They are inherently OS/architecture-matched (a 64-bit Windows Patchy can only load 64-bit Windows plugins).
- The Windows-only bundled shims are exercised by `ui_bundled_legacy_plugin_action_applies_filter`.
