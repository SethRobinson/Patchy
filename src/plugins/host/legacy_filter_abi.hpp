#pragma once

// Patchy's own declaration of the classic Photoshop filter plug-in ABI: the
// record a filter receives, the callback suites a host provides, the selector
// and error codes. Written from the public plug-in documentation and from the
// behaviour of real plug-ins; no SDK file or text is reproduced here (the
// binding boundary is recorded in docs/legal-constraints.md). Layout facts that
// matter: the filter record is 4-byte packed while the callback suites use the
// compiler's natural alignment, points and rectangles are 16-bit QuickDraw
// style, every callback uses the C calling convention, and the host-owned
// record outlives the plug-in's use of it.

#include <cstddef>
#include <cstdint>

namespace patchy::abi {


using Boolean = std::uint8_t;
using Fixed = std::int32_t;
using OSErr = std::int16_t;
using Ptr = char*;
using Handle = char**;
using BufferID = void*;
using PIType = std::uint32_t;
using SPErr = std::int32_t;
using SPBoolean = std::int32_t;

struct Point {
  std::int16_t v;
  std::int16_t h;
};

struct Rect {
  std::int16_t top;
  std::int16_t left;
  std::int16_t bottom;
  std::int16_t right;
};

struct VRect {
  std::int32_t top;
  std::int32_t left;
  std::int32_t bottom;
  std::int32_t right;
};

struct RGBColor {
  std::uint16_t red;
  std::uint16_t green;
  std::uint16_t blue;
};

using FilterColor = std::uint8_t[4];
using Str255 = unsigned char[256];

struct PlugInMonitor {
  Fixed gamma;
  Fixed redX;
  Fixed redY;
  Fixed greenX;
  Fixed greenY;
  Fixed blueX;
  Fixed blueY;
  Fixed whiteX;
  Fixed whiteY;
  Fixed ambient;
};

// Selectors the host sends to the entry point.
inline constexpr std::int16_t kSelectorAbout = 0;
inline constexpr std::int16_t kSelectorParameters = 1;
inline constexpr std::int16_t kSelectorPrepare = 2;
inline constexpr std::int16_t kSelectorStart = 3;
inline constexpr std::int16_t kSelectorContinue = 4;
inline constexpr std::int16_t kSelectorFinish = 5;

// Result codes.
inline constexpr OSErr kNoErr = 0;
inline constexpr OSErr kUserCanceledErr = 1;
inline constexpr OSErr kMemFullErr = -108;
inline constexpr OSErr kFilterBadParameters = -30000;
inline constexpr OSErr kFilterBadMode = -30001;
inline constexpr OSErr kErrPlugInHostInsufficient = -30900;
inline constexpr OSErr kErrPlugInPropertyUndefined = -30901;
inline constexpr OSErr kErrHostDoesNotSupportColStep = -30902;
inline constexpr OSErr kErrInvalidSamplePoint = -30903;
inline constexpr OSErr kErrReportString = -30904;

// Image modes.
inline constexpr std::int16_t kModeGrayScale = 1;
inline constexpr std::int16_t kModeRGBColor = 3;

// Host signature plug-ins compare against.
inline constexpr std::uint32_t kHostSignature = 0x3842494D;  // '8BIM'

// --- callback suites ----------------------------------------------------------

using TestAbortProc = Boolean (*)();
using ProgressProc = void (*)(std::int32_t done, std::int32_t total);
using HostProc = void (*)(std::int16_t selector, std::intptr_t* data);
using ProcessEventProc = void (*)(void* event);
using AdvanceStateProc = OSErr (*)();

using AllocateBufferProc = OSErr (*)(std::int32_t size, BufferID* buffer);
using LockBufferProc = Ptr (*)(BufferID buffer, Boolean moveHigh);
using UnlockBufferProc = void (*)(BufferID buffer);
using FreeBufferProc = void (*)(BufferID buffer);
using BufferSpaceProc = std::int32_t (*)();

inline constexpr std::int16_t kBufferProcsVersion = 2;
inline constexpr std::int16_t kBufferProcsCount = 5;

struct BufferProcs {
  std::int16_t bufferProcsVersion;
  std::int16_t numBufferProcs;
  AllocateBufferProc allocateProc;
  LockBufferProc lockProc;
  UnlockBufferProc unlockProc;
  FreeBufferProc freeProc;
  BufferSpaceProc spaceProc;
};

using NewPIHandleProc = Handle (*)(std::int32_t size);
using DisposePIHandleProc = void (*)(Handle h);
using GetPIHandleSizeProc = std::int32_t (*)(Handle h);
using SetPIHandleSizeProc = OSErr (*)(Handle h, std::int32_t newSize);
using LockPIHandleProc = Ptr (*)(Handle h, Boolean moveHigh);
using UnlockPIHandleProc = void (*)(Handle h);
using RecoverSpaceProc = void (*)(std::int32_t size);
using DisposeRegularPIHandleProc = void (*)(Handle h);

inline constexpr std::int16_t kHandleProcsVersion = 1;
inline constexpr std::int16_t kHandleProcsCount = 8;

struct HandleProcs {
  std::int16_t handleProcsVersion;
  std::int16_t numHandleProcs;
  NewPIHandleProc newProc;
  DisposePIHandleProc disposeProc;
  GetPIHandleSizeProc getSizeProc;
  SetPIHandleSizeProc setSizeProc;
  LockPIHandleProc lockProc;
  UnlockPIHandleProc unlockProc;
  RecoverSpaceProc recoverSpaceProc;
  DisposeRegularPIHandleProc disposeRegularHandleProc;
};

using GetPropertyProc = OSErr (*)(PIType signature, PIType key, std::int32_t index, std::intptr_t* simpleProperty,
                                  Handle* complexProperty);
using SetPropertyProc = OSErr (*)(PIType signature, PIType key, std::int32_t index, std::intptr_t simpleProperty,
                                  Handle complexProperty);

inline constexpr std::int16_t kPropertyProcsVersion = 1;
inline constexpr std::int16_t kPropertyProcsCount = 2;

struct PropertyProcs {
  std::int16_t propertyProcsVersion;
  std::int16_t numPropertyProcs;
  GetPropertyProc getPropertyProc;
  SetPropertyProc setPropertyProc;
};

// Property keys (signature '8BIM').
inline constexpr PIType kPropNumberOfChannels = 0x6E756368;     // 'nuch'
inline constexpr PIType kPropChannelName = 0x6E6D6368;          // 'nmch'
inline constexpr PIType kPropImageMode = 0x6D6F6465;            // 'mode'
inline constexpr PIType kPropNumberOfPaths = 0x6E757061;        // 'nupa'
inline constexpr PIType kPropWorkPathIndex = 0x776B7061;        // 'wkpa'
inline constexpr PIType kPropClippingPathIndex = 0x63707061;    // 'cppa'
inline constexpr PIType kPropTargetPathIndex = 0x74617061;      // 'tapa'
inline constexpr PIType kPropInterpolationMethod = 0x696E7470;  // 'intp'
inline constexpr PIType kPropRulerUnits = 0x72756C72;           // 'rulr'
inline constexpr PIType kPropRulerOriginH = 0x726F7248;         // 'rorH'
inline constexpr PIType kPropRulerOriginV = 0x726F7256;         // 'rorV'
inline constexpr PIType kPropSerialString = 0x73737472;         // 'sstr'
inline constexpr PIType kPropBigNudgeH = 0x626E6848;            // 'bnhH'
inline constexpr PIType kPropBigNudgeV = 0x626E6856;            // 'bnhV'
inline constexpr PIType kPropTitle = 0x7469746C;                // 'titl'
inline constexpr PIType kPropHostName = 0x686F7374;             // 'host'

struct PSPixelMask {
  PSPixelMask* next;
  void* maskData;
  std::int32_t rowBytes;
  std::int32_t colBytes;
  std::int32_t maskDescription;
};

struct PSPixelOverlay {
  PSPixelOverlay* next;
  void* data;
  std::int32_t rowBytes;
  std::int32_t colBytes;
  unsigned char r;
  unsigned char g;
  unsigned char b;
  unsigned char opacity;
  std::int32_t overlayAlgorithm;
};

struct PSPixelMap {
  std::int32_t version;
  VRect bounds;
  std::int32_t imageMode;
  std::int32_t rowBytes;
  std::int32_t colBytes;
  std::int32_t planeBytes;
  void* baseAddr;
  // Version 1 additions.
  PSPixelMask* mat;
  PSPixelMask* masks;
  std::int32_t maskPhaseRow;
  std::int32_t maskPhaseCol;
  // Version 2 additions.
  PSPixelOverlay* pixelOverlays;
  std::uint32_t colorManagementOptions;
};

using DisplayPixelsProc = OSErr (*)(const PSPixelMap* source, const VRect* srcRect, std::int32_t dstRow,
                                    std::int32_t dstCol, void* platformContext);

// Color services.
inline constexpr std::int16_t kColorServicesChooseColor = 0;
inline constexpr std::int16_t kColorServicesConvertColor = 1;
inline constexpr std::int16_t kColorServicesSamplePoint = 2;
inline constexpr std::int16_t kColorServicesGetSpecialColor = 3;

inline constexpr std::int16_t kColorSpaceRGB = 0;
inline constexpr std::int16_t kColorSpaceHSB = 1;
inline constexpr std::int16_t kColorSpaceCMYK = 2;
inline constexpr std::int16_t kColorSpaceLab = 3;
inline constexpr std::int16_t kColorSpaceGray = 4;
inline constexpr std::int16_t kColorSpaceHSL = 5;
inline constexpr std::int16_t kColorSpaceXYZ = 6;
inline constexpr std::int16_t kColorSpaceChosen = -1;

inline constexpr std::int32_t kSpecialColorForeground = 0;
inline constexpr std::int32_t kSpecialColorBackground = 1;

struct ColorServicesInfo {
  std::int32_t infoSize;
  std::int16_t selector;
  std::int16_t sourceSpace;
  std::int16_t resultSpace;
  Boolean resultGamutInfoValid;
  Boolean resultInGamut;
  void* reservedSourceSpaceInfo;
  void* reservedResultSpaceInfo;
  std::int16_t colorComponents[4];
  void* reserved;
  union {
    unsigned char* pickerPrompt;
    Point* globalSamplePoint;
    std::int32_t specialColorID;
  } selectorParameter;
};

using ColorServicesProc = OSErr (*)(ColorServicesInfo* info);

// The PICA basic suite: plug-ins ask it for other suites. Patchy's host offers
// none, so AcquireSuite answers "not found".
inline constexpr SPErr kSPNoError = 0;
inline constexpr SPErr kSPSuiteNotFoundError = 0x53214664;  // 'S!Fd'
inline constexpr SPErr kSPBadParameterError = 0x50617261;   // 'Para'

struct SPBasicSuite {
  SPErr (*AcquireSuite)(const char* name, std::int32_t version, const void** suite);
  SPErr (*ReleaseSuite)(const char* name, std::int32_t version);
  SPBoolean (*IsEqual)(const char* token1, const char* token2);
  SPErr (*AllocateBlock)(std::size_t size, void** block);
  SPErr (*FreeBlock)(void* block);
  SPErr (*ReallocateBlock)(void* block, std::size_t newSize, void** newBlock);
  SPErr (*Undefined)();
};

// Opaque host structures the record points at but this host does not provide.
struct ResourceProcs;
struct ImageServicesProcs;
struct PIDescriptorParameters;
struct ChannelPortProcs;
struct ReadImageDocumentDesc;

// On Windows platformData points at this.
struct PlatformData {
  void* hwnd;
};

// Filter layout constants (wantLayout).
inline constexpr std::int16_t kLayoutTraditional = 0;

// inputPadding / outputPadding / maskPadding: a 0..255 fill value, or one of
// these requests.
inline constexpr std::int16_t kPaddingEdgeReplication = -1;
inline constexpr std::int16_t kPaddingNone = -2;
inline constexpr std::int16_t kPaddingErrorOnBounds = -3;

// The record itself is 4-byte packed on Windows (pointers land on 4-byte
// offsets in the 64-bit build); the callback suites above use the compiler's
// natural alignment. Both facts were confirmed against real plug-ins.
#pragma pack(push, 4)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4121)
#endif
struct FilterRecord {
  std::int32_t serialNumber;
  TestAbortProc abortProc;
  ProgressProc progressProc;
  Handle parameters;
  Point imageSize;
  std::int16_t planes;
  Rect filterRect;
  RGBColor background;
  RGBColor foreground;
  std::int32_t maxSpace;
  std::int32_t bufferSpace;
  Rect inRect;
  std::int16_t inLoPlane;
  std::int16_t inHiPlane;
  Rect outRect;
  std::int16_t outLoPlane;
  std::int16_t outHiPlane;
  void* inData;
  std::int32_t inRowBytes;
  void* outData;
  std::int32_t outRowBytes;
  Boolean isFloating;
  Boolean haveMask;
  Boolean autoMask;
  Rect maskRect;
  void* maskData;
  std::int32_t maskRowBytes;
  FilterColor backColor;
  FilterColor foreColor;
  std::uint32_t hostSig;
  HostProc hostProc;
  std::int16_t imageMode;
  Fixed imageHRes;
  Fixed imageVRes;
  Point floatCoord;
  Point wholeSize;
  PlugInMonitor monitor;
  void* platformData;
  BufferProcs* bufferProcs;
  ResourceProcs* resourceProcs;
  ProcessEventProc processEvent;
  DisplayPixelsProc displayPixels;
  HandleProcs* handleProcs;
  // Added with host version 3.
  Boolean supportsDummyChannels;
  Boolean supportsAlternateLayouts;
  std::int16_t wantLayout;
  std::int16_t filterCase;
  std::int16_t dummyPlaneValue;
  void* premiereHook;
  AdvanceStateProc advanceState;
  Boolean supportsAbsolute;
  Boolean wantsAbsolute;
  GetPropertyProc getPropertyObsolete;
  Boolean cannotUndo;
  Boolean supportsPadding;
  std::int16_t inputPadding;
  std::int16_t outputPadding;
  std::int16_t maskPadding;
  char samplingSupport;
  char reservedByte;
  Fixed inputRate;
  Fixed maskRate;
  ColorServicesProc colorServices;
  std::int16_t inLayerPlanes;
  std::int16_t inTransparencyMask;
  std::int16_t inLayerMasks;
  std::int16_t inInvertedLayerMasks;
  std::int16_t inNonLayerPlanes;
  std::int16_t outLayerPlanes;
  std::int16_t outTransparencyMask;
  std::int16_t outLayerMasks;
  std::int16_t outInvertedLayerMasks;
  std::int16_t outNonLayerPlanes;
  std::int16_t absLayerPlanes;
  std::int16_t absTransparencyMask;
  std::int16_t absLayerMasks;
  std::int16_t absInvertedLayerMasks;
  std::int16_t absNonLayerPlanes;
  std::int16_t inPreDummyPlanes;
  std::int16_t inPostDummyPlanes;
  std::int16_t outPreDummyPlanes;
  std::int16_t outPostDummyPlanes;
  std::int32_t inColumnBytes;
  std::int32_t inPlaneBytes;
  std::int32_t outColumnBytes;
  std::int32_t outPlaneBytes;
  ImageServicesProcs* imageServicesProcs;
  PropertyProcs* propertyProcs;
  // Added with host version 4.
  std::int16_t inTileHeight;
  std::int16_t inTileWidth;
  Point inTileOrigin;
  std::int16_t absTileHeight;
  std::int16_t absTileWidth;
  Point absTileOrigin;
  std::int16_t outTileHeight;
  std::int16_t outTileWidth;
  Point outTileOrigin;
  std::int16_t maskTileHeight;
  std::int16_t maskTileWidth;
  Point maskTileOrigin;
  PIDescriptorParameters* descriptorParameters;
  Str255* errorString;
  ChannelPortProcs* channelPortProcs;
  ReadImageDocumentDesc* documentInfo;
  // Added with host version 5.
  SPBasicSuite* sSPBasic;
  void* plugInRef;
  std::int32_t depth;
  // Added with later host versions. Everything from here on stays zero: a
  // plug-in probing a feature this host does not model reads a null or 0.
  Handle iCCprofileData;
  std::int32_t iCCprofileSize;
  std::int32_t canUseICCProfiles;
  std::int32_t hasImageScrap;
  void* bigDocumentData;
  void* input3DScene;
  void* output3DScene;
  Boolean createNewLayer;
  Handle iCCWorkingProfileData;
  std::int32_t iCCWorkingProfileSize;
  std::int64_t bufferSpace64;
  std::int64_t maxSpace64;
  std::uint8_t reservedTail[256];
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#pragma pack(pop)

// Entry point exported by a filter plug-in.
using FilterEntryProc = void (*)(std::int16_t selector, FilterRecord* record, std::intptr_t* data, OSErr* result);

}  // namespace patchy::abi
