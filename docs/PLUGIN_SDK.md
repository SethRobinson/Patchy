# Patchy Plug-in SDK

The first SDK surface is a C ABI in `src/plugins/plugin_api.h`.

## ABI Rules

- Plug-ins export descriptor, initialize, and shutdown functions using the function pointer shapes in `plugin_api.h`.
- `PATCHY_PLUGIN_ABI_VERSION` must match the host.
- Plug-in identifiers must be reverse-DNS style, for example `com.example.my-filter`.
- Host-owned memory remains host-owned. Plug-ins must not store raw document pointers after a call returns.

## Compatibility Strategy

The stable Patchy SDK is the supported extension API. Classic Photoshop plug-ins are handled by `LegacyPhotoshopAdapter` as a compatibility layer and will be limited to OS/architecture-matched binaries.

The first compatibility implementation should:

- Probe `.8bf` and `.8bi` candidates.
- Run plug-ins out-of-process.
- Marshal pixel buffers through an IPC-safe command protocol.
- Maintain a documented supported/unsupported matrix.

UXP and JSX compatibility are deferred until the native document, action, and scripting APIs are stable.
