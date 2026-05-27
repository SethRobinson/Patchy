#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PATCHY_PLUGIN_ABI_VERSION 1u

typedef enum PatchyPluginKind {
  PATCHY_PLUGIN_FILTER = 1,
  PATCHY_PLUGIN_FILE_FORMAT = 2,
  PATCHY_PLUGIN_PANEL = 3,
  PATCHY_PLUGIN_TOOL = 4
} PatchyPluginKind;

typedef struct PatchyHostApi {
  uint32_t abi_version;
  void (*log_info)(const char* message);
  void (*log_error)(const char* message);
} PatchyHostApi;

typedef struct PatchyPluginDescriptor {
  uint32_t abi_version;
  PatchyPluginKind kind;
  const char* identifier;
  const char* display_name;
  uint32_t major_version;
  uint32_t minor_version;
  uint32_t patch_version;
} PatchyPluginDescriptor;

typedef int (*PatchyPluginDescribeFn)(PatchyPluginDescriptor* descriptor);
typedef int (*PatchyPluginInitializeFn)(const PatchyHostApi* host_api);
typedef void (*PatchyPluginShutdownFn)(void);

#ifdef __cplusplus
}
#endif
