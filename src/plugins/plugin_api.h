#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHOTOSLOP_PLUGIN_ABI_VERSION 1u

typedef enum PhotoslopPluginKind {
  PHOTOSLOP_PLUGIN_FILTER = 1,
  PHOTOSLOP_PLUGIN_FILE_FORMAT = 2,
  PHOTOSLOP_PLUGIN_PANEL = 3,
  PHOTOSLOP_PLUGIN_TOOL = 4
} PhotoslopPluginKind;

typedef struct PhotoslopHostApi {
  uint32_t abi_version;
  void (*log_info)(const char* message);
  void (*log_error)(const char* message);
} PhotoslopHostApi;

typedef struct PhotoslopPluginDescriptor {
  uint32_t abi_version;
  PhotoslopPluginKind kind;
  const char* identifier;
  const char* display_name;
  uint32_t major_version;
  uint32_t minor_version;
  uint32_t patch_version;
} PhotoslopPluginDescriptor;

typedef int (*PhotoslopPluginDescribeFn)(PhotoslopPluginDescriptor* descriptor);
typedef int (*PhotoslopPluginInitializeFn)(const PhotoslopHostApi* host_api);
typedef void (*PhotoslopPluginShutdownFn)(void);

#ifdef __cplusplus
}
#endif
