/*
 * Graphics Synthesizer, WebGPU back end (the browser build only; Dawn's webgpu.h for Emscripten).
 *
 * The same model as the other back ends: gs_draw.c records the frame; this file owns the GPU objects and at the end
 * of the frame uploads what is new, replays the recorded list, and presents. The design (resources, binding layouts,
 * what does not map directly from OpenGL / Vulkan) is in BROWSER_PORT_STATUS.md, "WebGPU back end: design".
 *
 * Shaders: the GLSL in port/src/gs/shaders is the only source; port/tools/wasm_shaders.py translates it to WGSL with
 * the pinned naga (wgsl_shaders.h). The GLSL sets follow SDL GPU's convention, kept as the WGSL groups:
 *   group 0  unused (an empty group: WebGPU binds every group up to the highest one used)
 *   group 1  the vertex programs' constants (U), a dynamic offset into the frame's uniform buffer
 *   group 2  the fragment stage's textures, each with its sampler at the next binding (SEPARATE_SAMPLERS)
 *   group 3  the fragment stage's Params, a dynamic offset into the frame's uniform buffer
 *
 * Waiting (for the adapter, the device, a buffer to map, the queue) uses wgpuInstanceWaitAny: the game runs under
 * JSPI, so its thread waits there without blocking the page, as it does for its files.
 */
#ifdef __EMSCRIPTEN__
#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webgpu/webgpu.h>
#include "gs_internal.h"
#include "gs_draw.h"
#include "ui.h"
#include "wgsl_shaders.h"

/* SDL's default canvas (SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR when the hint is not set): the surface is that canvas. */
#define SDL_DEFAULT_CANVAS_SELECTOR "#canvas"

/* A target's attachments, as in the other back ends: the colour, the alpha byte kept apart (the object ids the
   outline and the DATE test read), and depth stored as z / w. */
#define WG_COLOR_FORMAT WGPUTextureFormat_RGBA8Unorm
#define WG_AUX_FORMAT WGPUTextureFormat_R8Unorm
#define WG_DEPTH_FORMAT WGPUTextureFormat_Depth32Float
#define WG_MAX_PIPES 1024
#define WG_ENTRY_POINT "main" /* (naga names every stage's entry point main) */
/* WebGPU: a texture-to-buffer copy's bytes per row are a multiple of this */
#define WG_COPY_ROW_ALIGNMENT 256u
/* BC1 / BC2 / BC3: 4 x 4 texel blocks of 8 or 16 bytes */
#define WG_BC_BLOCK 4u
#define WG_BC1_BLOCK_BYTES 8u
#define WG_BC23_BLOCK_BYTES 16u
#define WG_FULLSCREEN_VERTICES 3 /* fx.vert's one triangle over the target */

enum { WG_GROUP_UNUSED, WG_GROUP_PROGRAM, WG_GROUP_TEXTURES, WG_GROUP_PARAMS, WG_GROUP_COUNT };

/* gs.frag's Params (std140, as gs_gl.c fills it) */
typedef struct GsParams {
    int32_t mode[4];
    float misc[4];
    float rect[4];
    float orig[4];
    float extra[4]; /* x: the depth buffer's largest value (GsDraw_DepthMax) */
} GsParams;
/* the full-screen passes' Params (dclut.frag, outline.frag, blit.frag): one vec4. Stored in a GsParams-sized slot, so
   that one bind group of sizeof(GsParams) serves every fragment stage. */
typedef struct FxParams {
    float v[4];
} FxParams;

static SDL_Window *sWindow;
static WGPUInstance sInstance;
static WGPUAdapter sAdapter;
static WGPUDevice sDevice;
static WGPUQueue sQueue;
static WGPUSurface sSurface;
static WGPUTextureFormat sSurfaceFormat;
static uint32_t sSurfaceW, sSurfaceH; /* the size the surface is configured for (0: not yet) */
static WGPULimits sLimits;            /* the device's */
static int sUnclippedDepth;           /* depth-clip-control: depth clamped instead of clipped, as SDL GPU and GL do it */
static int sBcTextures;               /* texture-compression-bc: texture packs' BC1 / BC2 / BC3 replacements */

/* Errors the device reports (validation, out of memory, internal) and a lost device: counted and logged, never hidden.
   The tests read the log (bench.js). */
static unsigned sDeviceErrors;
static int sDeviceLost, sDeviceLostReported;

/* Objects made since the start (BT3_GS_VERBOSE prints them with the frame's counts; the performance comparison). */
static struct {
    unsigned textures, bindGroups, pipelines, readbacks;
} sMade;
static int sTexturesLive;

static WGPUStringView string_view(const char *s) {
    WGPUStringView v = {s, WGPU_STRLEN};
    return v;
}

static int string_length(WGPUStringView s) {
    return s.data == NULL ? 0 : (int)(s.length == WGPU_STRLEN ? strlen(s.data) : s.length);
}

static uint32_t align_up(uint32_t v, uint32_t alignment) {
    return (v + alignment - 1) / alignment * alignment;
}

/* Waits for a future (the game's thread suspends: JSPI). 1 = it completed. */
static int wait_for(WGPUFuture future) {
    WGPUFutureWaitInfo info = {future, 0};
    return wgpuInstanceWaitAny(sInstance, 1, &info, UINT64_MAX) == WGPUWaitStatus_Success && info.completed;
}

/* ---- callbacks ------------------------------------------------------------------------------------------------ */

static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *result, void *unused) {
    (void)unused;
    if (status == WGPURequestAdapterStatus_Success) {
        *(WGPUAdapter *)result = adapter;
    } else {
        fprintf(stderr, "bt3: webgpu: no adapter: %.*s\n", string_length(message), message.data);
    }
}

static void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *result, void *unused) {
    (void)unused;
    if (status == WGPURequestDeviceStatus_Success) {
        *(WGPUDevice *)result = device;
    } else {
        fprintf(stderr, "bt3: webgpu: no device: %.*s\n", string_length(message), message.data);
    }
}

static const char *error_name(WGPUErrorType type) {
    switch (type) {
    case WGPUErrorType_Validation: return "validation";
    case WGPUErrorType_OutOfMemory: return "out of memory";
    case WGPUErrorType_Internal: return "internal";
    default: return "unknown";
    }
}

/* The latest device errors, for the page's Developer section (GsWgpu_Error): a fixed ring of short copies. */
#define WG_ERRORS_KEPT 8
#define WG_ERROR_TEXT 240
static char sErrorText[WG_ERRORS_KEPT][WG_ERROR_TEXT];

static void on_device_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message, void *unused1, void *unused2) {
    (void)device; (void)unused1; (void)unused2;
    snprintf(sErrorText[sDeviceErrors % WG_ERRORS_KEPT], WG_ERROR_TEXT, "%s: %.*s", error_name(type), string_length(message),
             message.data != NULL ? message.data : "");
    sDeviceErrors++;
    fprintf(stderr, "bt3: webgpu: %s error: %.*s\n", error_name(type), string_length(message), message.data);
}

/* GPU memory the engine asked for: the sizes of its live textures and buffers. An estimate, not a measurement: a page
   cannot see what the driver really holds (alignment, compression, its own copies). */
static uint64_t sGpuBytes;
/* GPU time (timestamp-query): whether the device has it, and frames that could not be timed (every slot was busy) */
static int sTimestamps;
static unsigned sTimingSkipped;
static void timing_create(void); /* (with the fixed objects; the timing itself is with the replay) */

static void on_device_lost(WGPUDevice const *device, WGPUDeviceLostReason reason, WGPUStringView message, void *unused1, void *unused2) {
    (void)device; (void)unused1; (void)unused2;
    if (reason == WGPUDeviceLostReason_Destroyed || reason == WGPUDeviceLostReason_CallbackCancelled) {
        return; /* (released on purpose, or the program ending) */
    }
    sDeviceLost = 1;
    fprintf(stderr, "bt3: webgpu: the device was lost (reason %d): %.*s\n", (int)reason, string_length(message), message.data);
}

/* The first error inside an error scope (init: the fixed objects are checked before the game starts). */
typedef struct ScopeResult {
    WGPUErrorType type;
    char message[512];
} ScopeResult;

static void on_scope(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void *result, void *unused) {
    ScopeResult *r = result;
    (void)unused;
    r->type = status == WGPUPopErrorScopeStatus_Success ? type : WGPUErrorType_Unknown;
    snprintf(r->message, sizeof(r->message), "%.*s", string_length(message), message.data != NULL ? message.data : "");
}

static void on_mapped(WGPUMapAsyncStatus status, WGPUStringView message, void *result, void *unused) {
    (void)unused;
    *(WGPUMapAsyncStatus *)result = status;
    if (status != WGPUMapAsyncStatus_Success) {
        fprintf(stderr, "bt3: webgpu: reading a texture back failed: %.*s\n", string_length(message), message.data);
    }
}

static void on_work_done(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *unused1, void *unused2) {
    (void)unused1; (void)unused2;
    if (status != WGPUQueueWorkDoneStatus_Success) {
        fprintf(stderr, "bt3: webgpu: waiting for the queue failed: %.*s\n", string_length(message), message.data);
    }
}

/* Waits until the GPU has done everything submitted so far. */
static void queue_wait_idle(void) {
    WGPUQueueWorkDoneCallbackInfo cb = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    cb.mode = WGPUCallbackMode_WaitAnyOnly;
    cb.callback = on_work_done;
    wait_for(wgpuQueueOnSubmittedWorkDone(sQueue, cb));
}

/* ---- device and surface --------------------------------------------------------------------------------------- */

static void print_adapter(void) {
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(sAdapter, &info) == WGPUStatus_Success) {
        fprintf(stderr, "bt3: webgpu adapter: %.*s %.*s %.*s %.*s\n", string_length(info.vendor), info.vendor.data,
                string_length(info.architecture), info.architecture.data, string_length(info.device), info.device.data,
                string_length(info.description), info.description.data);
        wgpuAdapterInfoFreeMembers(info);
    }
}

static int device_create(void) {
    WGPUFeatureName features[3];
    size_t featureCount = 0;
    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;

    sUnclippedDepth = wgpuAdapterHasFeature(sAdapter, WGPUFeatureName_DepthClipControl);
    sBcTextures = wgpuAdapterHasFeature(sAdapter, WGPUFeatureName_TextureCompressionBC);
    if (sUnclippedDepth) {
        features[featureCount++] = WGPUFeatureName_DepthClipControl;
    } else {
        fprintf(stderr, "bt3: webgpu: no depth-clip-control: geometry beyond the far plane is cut instead of clamped "
                        "(differs from the OpenGL and Vulkan back ends)\n");
    }
    if (sBcTextures) {
        features[featureCount++] = WGPUFeatureName_TextureCompressionBC;
    }
    /* the GPU's own time per frame for the page's Developer section, where the browser offers it (it may round it) */
    sTimestamps = wgpuAdapterHasFeature(sAdapter, WGPUFeatureName_TimestampQuery) &&
                  (getenv("BT3_GPU_TIMING") == NULL || atoi(getenv("BT3_GPU_TIMING")) != 0); /* (=0: off, to measure its cost) */
    if (sTimestamps) {
        features[featureCount++] = WGPUFeatureName_TimestampQuery;
    }
    desc.label = string_view("bt3");
    desc.requiredFeatureCount = featureCount;
    desc.requiredFeatures = features;
    desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    desc.deviceLostCallbackInfo.callback = on_device_lost;
    desc.uncapturedErrorCallbackInfo.callback = on_device_error;
    {
        WGPURequestDeviceCallbackInfo cb = {NULL, WGPUCallbackMode_WaitAnyOnly, on_device, &sDevice, NULL};
        if (!wait_for(wgpuAdapterRequestDevice(sAdapter, &desc, cb)) || sDevice == NULL) {
            return 0;
        }
    }
    sLimits.nextInChain = NULL;
    wgpuDeviceGetLimits(sDevice, &sLimits);
    sQueue = wgpuDeviceGetQueue(sDevice);
    return 1;
}

static int surface_create(void) {
    const char *hint = SDL_GetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR);
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    WGPUSurfaceDescriptor desc = WGPU_SURFACE_DESCRIPTOR_INIT;
    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;

    canvas.selector = string_view(hint != NULL ? hint : SDL_DEFAULT_CANVAS_SELECTOR);
    desc.nextInChain = &canvas.chain;
    sSurface = wgpuInstanceCreateSurface(sInstance, &desc);
    if (sSurface == NULL) {
        return 0;
    }
    if (wgpuSurfaceGetCapabilities(sSurface, sAdapter, &caps) != WGPUStatus_Success || caps.formatCount == 0) {
        fprintf(stderr, "bt3: webgpu: the canvas offers no surface format\n");
        return 0;
    }
    sSurfaceFormat = caps.formats[0]; /* the first is the preferred one (getPreferredCanvasFormat) */
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    return 1;
}

/* The surface at the window's size in pixels; configured again when that changes. */
static void surface_fit(void) {
    int w, h;
    WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;

    SDL_GetWindowSizeInPixels(sWindow, &w, &h);
    if (w <= 0 || h <= 0 || ((uint32_t)w == sSurfaceW && (uint32_t)h == sSurfaceH)) {
        return;
    }
    config.device = sDevice;
    config.format = sSurfaceFormat;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = (uint32_t)w;
    config.height = (uint32_t)h;
    config.alphaMode = WGPUCompositeAlphaMode_Opaque;
    config.presentMode = WGPUPresentMode_Fifo; /* (the only mode a canvas has; the game paces itself) */
    wgpuSurfaceConfigure(sSurface, &config);
    sSurfaceW = (uint32_t)w;
    sSurfaceH = (uint32_t)h;
}

/* ---- textures ------------------------------------------------------------------------------------------------- */

/* A texture of the back end: what a GsTex handle points to (the decoded textures, the targets' attachments, the
   copies). The bind groups that sample it are made when first needed and kept with it. */
typedef struct WgTexture {
    WGPUTexture texture;
    WGPUTextureView view;
    unsigned copiesEpoch;                 /* sCopiesEpoch when `draw` was made (they hold the DATE copy) */
    WGPUBindGroup draw[GS_SAMPLER_COUNT]; /* gs.frag's group 2: this texture with each sampler, and the DATE copy */
    WGPUBindGroup blit;                   /* blit.frag's group 2: this texture, bilinear and clamped */
    uint64_t bytes;                       /* its size as asked for (sGpuBytes) */
} WgTexture;

typedef struct WgTarget {
    WgTexture *color, *aux, *depth;
} WgTarget;

static WGPUSampler sSamplers[GS_SAMPLER_COUNT]; /* indexed by GS_SAMPLER_* flags */
static WgTexture *sWhite;
static WgTarget sTargets[MAX_TARGETS];
/* the alpha bytes copied for the passes that read them while the target is drawn to (GS_NATIVE_COPY_DATE / _IDS) */
static WgTexture *sDateCopy, *sAuxCopy;
static unsigned sCopiesEpoch;                   /* made again (a new SCALE): every cached draw group is out of date */
static WgTexture *sFbTex[GS_DISPLAY_BUFFERS];   /* pictures uploaded into the display buffers (GS_NATIVE_FB_UPLOAD) */
static int sBcWarned;

static WgTexture *wg_texture(GsTex handle) {
    return (WgTexture *)handle;
}

/* The bytes of a texture of this size and format, every level counted (BC: 4 x 4 blocks of 8 or 16 bytes). */
static uint64_t texture_bytes(uint32_t w, uint32_t h, WGPUTextureFormat format, uint32_t levels) {
    uint64_t bytes = 0;
    uint32_t level;
    for (level = 0; level < levels; level++) {
        uint32_t lw = w >> level ? w >> level : 1, lh = h >> level ? h >> level : 1;
        uint64_t blocks = (uint64_t)((lw + WG_BC_BLOCK - 1) / WG_BC_BLOCK) * ((lh + WG_BC_BLOCK - 1) / WG_BC_BLOCK);
        switch (format) {
        case WGPUTextureFormat_R8Unorm: bytes += (uint64_t)lw * lh; break;
        case WGPUTextureFormat_BC1RGBAUnorm: bytes += blocks * WG_BC1_BLOCK_BYTES; break;
        case WGPUTextureFormat_BC2RGBAUnorm:
        case WGPUTextureFormat_BC3RGBAUnorm: bytes += blocks * WG_BC23_BLOCK_BYTES; break;
        default: bytes += (uint64_t)lw * lh * 4; break; /* RGBA8, Depth32Float */
        }
    }
    return bytes;
}

static WgTexture *texture_new(uint32_t w, uint32_t h, WGPUTextureFormat format, uint32_t levels, WGPUTextureUsage usage,
                              const char *label) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    WgTexture *t = calloc(1, sizeof(*t));
    desc.label = string_view(label);
    desc.usage = usage;
    desc.size.width = w;
    desc.size.height = h;
    desc.size.depthOrArrayLayers = 1;
    desc.format = format;
    desc.mipLevelCount = levels;
    t->texture = wgpuDeviceCreateTexture(sDevice, &desc);
    t->view = wgpuTextureCreateView(t->texture, NULL);
    t->copiesEpoch = sCopiesEpoch;
    t->bytes = texture_bytes(w, h, format, levels);
    sGpuBytes += t->bytes;
    sMade.textures++;
    sTexturesLive++;
    return t;
}

static void texture_draw_groups_release(WgTexture *t) {
    int i;
    for (i = 0; i < GS_SAMPLER_COUNT; i++) {
        if (t->draw[i] != NULL) {
            wgpuBindGroupRelease(t->draw[i]);
            t->draw[i] = NULL;
        }
    }
}

/* Releases a texture at once: the shared layer only destroys textures and targets the frame being recorded does not
   use (textures not used this frame, targets not used for two seconds; gs_draw.c), and work already submitted keeps
   what it uses until it is done (WebGPU's destroy). */
static void texture_free(WgTexture *t) {
    if (t == NULL) {
        return;
    }
    texture_draw_groups_release(t);
    if (t->blit != NULL) {
        wgpuBindGroupRelease(t->blit);
    }
    wgpuTextureViewRelease(t->view);
    wgpuTextureDestroy(t->texture);
    wgpuTextureRelease(t->texture);
    sGpuBytes -= t->bytes;
    free(t);
    sTexturesLive--;
}

/* One level of a texture from tightly packed texels (RGBA8 or BC blocks). */
static void texture_write(WgTexture *t, uint32_t level, uint32_t w, uint32_t h, int format, const void *px, size_t bytes) {
    WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    WGPUExtent3D size = {w, h, 1};
    dst.texture = t->texture;
    dst.mipLevel = level;
    if (format == GS_TEXFMT_RGBA8) {
        layout.bytesPerRow = w * 4;
        layout.rowsPerImage = h;
    } else {
        /* a level smaller than a block still takes one whole block (its physical size) */
        uint32_t blocksW = (w + WG_BC_BLOCK - 1) / WG_BC_BLOCK, blocksH = (h + WG_BC_BLOCK - 1) / WG_BC_BLOCK;
        layout.bytesPerRow = blocksW * (format == GS_TEXFMT_BC1 ? WG_BC1_BLOCK_BYTES : WG_BC23_BLOCK_BYTES);
        layout.rowsPerImage = blocksH;
        size.width = blocksW * WG_BC_BLOCK;
        size.height = blocksH * WG_BC_BLOCK;
    }
    wgpuQueueWriteTexture(sQueue, &dst, px, bytes, &layout, &size);
}

static GsTex wg_white_tex(void) {
    return (GsTex)sWhite;
}

static GsTex wg_tex_create(uint32_t w, uint32_t h, int format, int levels) {
    WGPUTextureFormat f = WGPUTextureFormat_RGBA8Unorm;
    if (format != GS_TEXFMT_RGBA8) {
        const char *why = !sBcTextures ? "this device has no texture-compression-bc"
                          : (w % WG_BC_BLOCK != 0 || h % WG_BC_BLOCK != 0) ? "its size is not a multiple of 4 (WebGPU's rule for BC)"
                                                                         : NULL;
        if (why != NULL) {
            if (!sBcWarned) {
                fprintf(stderr, "bt3: webgpu: a texture pack's BC replacement (%ux%u) cannot be used: %s; the game draws "
                                "it white (reported once)\n", w, h, why);
                sBcWarned = 1;
            }
            return 0;
        }
        f = format == GS_TEXFMT_BC1 ? WGPUTextureFormat_BC1RGBAUnorm
            : format == GS_TEXFMT_BC2 ? WGPUTextureFormat_BC2RGBAUnorm : WGPUTextureFormat_BC3RGBAUnorm;
    }
    return (GsTex)texture_new(w, h, f, levels > 0 ? (uint32_t)levels : 1, WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
                              "texture");
}

/* Copies the level into the queue at once (writeTexture copies the data); takes ownership of px. */
static int wg_tex_upload(GsTex tex, int level, uint32_t w, uint32_t h, int format, const void *px, uint32_t bytes) {
    texture_write(wg_texture(tex), (uint32_t)level, w, h, format, px, bytes);
    free((void *)px);
    return 1;
}

static void wg_tex_destroy(GsTex tex) {
    texture_free(wg_texture(tex));
}

static int wg_tex_slots_left(void) {
    return GS_TEX_SLOTS_UNLIMITED; /* (uploads go to the queue at once) */
}

static void samplers_create(void) {
    int i;
    for (i = 0; i < GS_SAMPLER_COUNT; i++) {
        WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
        WGPUFilterMode filter = (i & GS_SAMPLER_LINEAR) ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
        desc.addressModeU = (i & GS_SAMPLER_CLAMP_U) ? WGPUAddressMode_ClampToEdge : WGPUAddressMode_Repeat;
        desc.addressModeV = (i & GS_SAMPLER_CLAMP_V) ? WGPUAddressMode_ClampToEdge : WGPUAddressMode_Repeat;
        desc.addressModeW = WGPUAddressMode_ClampToEdge;
        desc.magFilter = filter;
        /* as gs_gl.c: a texture pack's mipmapped replacement is minified with the trilinear filter */
        desc.minFilter = (i & GS_SAMPLER_MIPMAPS) ? WGPUFilterMode_Linear : filter;
        desc.mipmapFilter = (i & GS_SAMPLER_MIPMAPS) ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
        if (!(i & GS_SAMPLER_MIPMAPS)) {
            desc.lodMaxClamp = 0.0f; /* only the full-size level, as GL's minifying filters without mipmaps */
        }
        sSamplers[i] = wgpuDeviceCreateSampler(sDevice, &desc);
    }
}

/* ---- binding layouts ------------------------------------------------------------------------------------------ */

static WGPUBindGroupLayout sLayoutEmpty, sLayoutProgram, sLayoutDrawTextures, sLayoutTableTextures, sLayoutOneTexture,
    sLayoutParams;
static WGPUPipelineLayout sPipeLayoutDraw, sPipeLayoutTable, sPipeLayoutOneTexture;
static WGPUBindGroup sEmptyGroup;
static WGPUBindGroup sOutlineGroup; /* outline.frag's group 2: the alpha-byte copy (made again with the copies) */

static WGPUBindGroupLayoutEntry entry_uniform(uint32_t binding, WGPUShaderStage stage, uint64_t size) {
    WGPUBindGroupLayoutEntry e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    e.binding = binding;
    e.visibility = stage;
    e.buffer.type = WGPUBufferBindingType_Uniform;
    e.buffer.hasDynamicOffset = 1;
    e.buffer.minBindingSize = size;
    return e;
}

static WGPUBindGroupLayoutEntry entry_texture(uint32_t binding, WGPUTextureSampleType type) {
    WGPUBindGroupLayoutEntry e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    e.binding = binding;
    e.visibility = WGPUShaderStage_Fragment;
    e.texture.sampleType = type;
    e.texture.viewDimension = WGPUTextureViewDimension_2D;
    return e;
}

static WGPUBindGroupLayoutEntry entry_sampler(uint32_t binding, WGPUSamplerBindingType type) {
    WGPUBindGroupLayoutEntry e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    e.binding = binding;
    e.visibility = WGPUShaderStage_Fragment;
    e.sampler.type = type;
    return e;
}

static WGPUBindGroupLayout layout_create(const WGPUBindGroupLayoutEntry *entries, size_t count, const char *label) {
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.label = string_view(label);
    desc.entryCount = count;
    desc.entries = entries;
    return wgpuDeviceCreateBindGroupLayout(sDevice, &desc);
}

static WGPUPipelineLayout pipeline_layout_create(WGPUBindGroupLayout program, WGPUBindGroupLayout textures, const char *label) {
    WGPUBindGroupLayout groups[WG_GROUP_COUNT];
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    groups[WG_GROUP_UNUSED] = sLayoutEmpty;
    groups[WG_GROUP_PROGRAM] = program;
    groups[WG_GROUP_TEXTURES] = textures;
    groups[WG_GROUP_PARAMS] = sLayoutParams;
    desc.label = string_view(label);
    desc.bindGroupLayoutCount = WG_GROUP_COUNT;
    desc.bindGroupLayouts = groups;
    return wgpuDeviceCreatePipelineLayout(sDevice, &desc);
}

static void layouts_create(void) {
    /* gs.frag: tex (any sampler) and dateTex (read with texelFetch) */
    const WGPUBindGroupLayoutEntry drawTextures[] = {
        entry_texture(0, WGPUTextureSampleType_Float), entry_sampler(1, WGPUSamplerBindingType_Filtering),
        entry_texture(2, WGPUTextureSampleType_Float), entry_sampler(3, WGPUSamplerBindingType_Filtering),
    };
    /* dclut.frag: the target's depth (a depth format is sampled as unfilterable float), the alpha-byte copy, the table */
    const WGPUBindGroupLayoutEntry tableTextures[] = {
        entry_texture(0, WGPUTextureSampleType_UnfilterableFloat), entry_sampler(1, WGPUSamplerBindingType_NonFiltering),
        entry_texture(2, WGPUTextureSampleType_Float), entry_sampler(3, WGPUSamplerBindingType_NonFiltering),
        entry_texture(4, WGPUTextureSampleType_Float), entry_sampler(5, WGPUSamplerBindingType_NonFiltering),
    };
    /* outline.frag, blit.frag */
    const WGPUBindGroupLayoutEntry oneTexture[] = {
        entry_texture(0, WGPUTextureSampleType_Float), entry_sampler(1, WGPUSamplerBindingType_Filtering),
    };
    const WGPUBindGroupLayoutEntry program = entry_uniform(0, WGPUShaderStage_Vertex, sizeof(Vu0Uniform));
    /* (every fragment stage's Params fits in gs.frag's, the size of the one params group) */
    const WGPUBindGroupLayoutEntry params = entry_uniform(0, WGPUShaderStage_Fragment, sizeof(GsParams));

    sLayoutEmpty = layout_create(NULL, 0, "empty");
    sLayoutProgram = layout_create(&program, 1, "program constants");
    sLayoutDrawTextures = layout_create(drawTextures, sizeof(drawTextures) / sizeof(drawTextures[0]), "draw textures");
    sLayoutTableTextures = layout_create(tableTextures, sizeof(tableTextures) / sizeof(tableTextures[0]), "table pass textures");
    sLayoutOneTexture = layout_create(oneTexture, sizeof(oneTexture) / sizeof(oneTexture[0]), "one texture");
    sLayoutParams = layout_create(&params, 1, "params");
    sPipeLayoutDraw = pipeline_layout_create(sLayoutProgram, sLayoutDrawTextures, "draw");
    sPipeLayoutTable = pipeline_layout_create(sLayoutEmpty, sLayoutTableTextures, "table pass");
    sPipeLayoutOneTexture = pipeline_layout_create(sLayoutEmpty, sLayoutOneTexture, "one texture");
    {
        WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        desc.layout = sLayoutEmpty;
        sEmptyGroup = wgpuDeviceCreateBindGroup(sDevice, &desc);
        sMade.bindGroups++;
    }
}

static WGPUBindGroupEntry group_texture(uint32_t binding, const WgTexture *t) {
    WGPUBindGroupEntry e = WGPU_BIND_GROUP_ENTRY_INIT;
    e.binding = binding;
    e.textureView = t->view;
    return e;
}

static WGPUBindGroupEntry group_sampler(uint32_t binding, int sampler) {
    WGPUBindGroupEntry e = WGPU_BIND_GROUP_ENTRY_INIT;
    e.binding = binding;
    e.sampler = sSamplers[sampler];
    return e;
}

static WGPUBindGroup group_create(WGPUBindGroupLayout layout, const WGPUBindGroupEntry *entries, size_t count) {
    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = count;
    desc.entries = entries;
    sMade.bindGroups++;
    return wgpuDeviceCreateBindGroup(sDevice, &desc);
}

/* gs.frag's textures for a draw: t with its sampler, and the DATE copy. */
static WGPUBindGroup draw_group(WgTexture *t, int sampler) {
    if (t->copiesEpoch != sCopiesEpoch) {
        texture_draw_groups_release(t);
        t->copiesEpoch = sCopiesEpoch;
    }
    if (t->draw[sampler] == NULL) {
        const WGPUBindGroupEntry entries[] = {
            group_texture(0, t), group_sampler(1, sampler),
            group_texture(2, sDateCopy), group_sampler(3, GS_SAMPLER_NEAREST_CLAMPED),
        };
        t->draw[sampler] = group_create(sLayoutDrawTextures, entries, sizeof(entries) / sizeof(entries[0]));
    }
    return t->draw[sampler];
}

/* blit.frag's texture: t, bilinear (the scaled copies: a display buffer's picture into its target, the present). */
static WGPUBindGroup blit_group(WgTexture *t) {
    if (t->blit == NULL) {
        const WGPUBindGroupEntry entries[] = {group_texture(0, t), group_sampler(1, GS_SAMPLER_LINEAR_CLAMPED)};
        t->blit = group_create(sLayoutOneTexture, entries, sizeof(entries) / sizeof(entries[0]));
    }
    return t->blit;
}

/* dclut.frag's textures: made for each table pass (a few a frame) and released once the pass holds them. */
static WGPUBindGroup table_group(int target, WgTexture *table) {
    const WGPUBindGroupEntry entries[] = {
        group_texture(0, sTargets[target].depth), group_sampler(1, GS_SAMPLER_NEAREST_CLAMPED),
        group_texture(2, sAuxCopy), group_sampler(3, GS_SAMPLER_NEAREST_CLAMPED),
        group_texture(4, table), group_sampler(5, GS_SAMPLER_NEAREST_CLAMPED),
    };
    return group_create(sLayoutTableTextures, entries, sizeof(entries) / sizeof(entries[0]));
}

/* ---- the copies of the alpha bytes, the targets --------------------------------------------------------------- */

static void copies_create(void) {
    const WGPUTextureUsage usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    sCopiesEpoch++;
    sDateCopy = texture_new(GS_W * SCALE, GS_H * SCALE, WG_AUX_FORMAT, 1, usage, "DATE copy");
    sAuxCopy = texture_new(GS_W * SCALE, GS_H * SCALE, WG_AUX_FORMAT, 1, usage, "alpha-byte copy");
    {
        const WGPUBindGroupEntry entries[] = {group_texture(0, sAuxCopy), group_sampler(1, GS_SAMPLER_NEAREST_CLAMPED)};
        sOutlineGroup = group_create(sLayoutOneTexture, entries, sizeof(entries) / sizeof(entries[0]));
    }
}

static void copies_drop(void) {
    wgpuBindGroupRelease(sOutlineGroup);
    texture_free(sDateCopy);
    texture_free(sAuxCopy);
    sOutlineGroup = NULL;
    sDateCopy = sAuxCopy = NULL;
}

static void wg_target_ensure(int i) {
    const uint32_t w = GS_W * SCALE, h = GS_H * SCALE;
    WgTarget *t = &sTargets[i];
    t->color = texture_new(w, h, WG_COLOR_FORMAT, 1,
                           WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc |
                               WGPUTextureUsage_CopyDst, "target colour");
    t->aux = texture_new(w, h, WG_AUX_FORMAT, 1,
                         WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc,
                         "target alpha byte");
    t->depth = texture_new(w, h, WG_DEPTH_FORMAT, 1, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
                           "target depth");
}

static void wg_target_drop(int i) {
    WgTarget *t = &sTargets[i];
    texture_free(t->color);
    texture_free(t->aux);
    texture_free(t->depth);
    t->color = t->aux = t->depth = NULL;
}

static GsTex wg_target_color(int i) { return (GsTex)sTargets[i].color; }
static GsTex wg_target_aux(int i) { return (GsTex)sTargets[i].aux; }
static GsTex wg_target_depth(int i) { return (GsTex)sTargets[i].depth; }

static void wg_scale_changed(void) {
    int i;
    queue_wait_idle();
    for (i = 0; i < gsTargetCount; i++) {
        wg_target_drop(i);
    }
    copies_drop();
    copies_create();
}

/* ---- shaders and pipelines ------------------------------------------------------------------------------------ */

enum {
    WG_SHADER_GS_VERT, WG_SHADER_VU0_VERT, WG_SHADER_VU4_VERT, WG_SHADER_VU6_VERT, WG_SHADER_FX_VERT,
    WG_SHADER_GS_FRAG, WG_SHADER_GSNZ_FRAG, WG_SHADER_DCLUT_FRAG, WG_SHADER_OUTLINE_FRAG, WG_SHADER_BLIT_FRAG, WG_SHADER_COUNT
};
static const struct {
    const char *name;
    const char *wgsl;
} kShaders[WG_SHADER_COUNT] = {
    [WG_SHADER_GS_VERT] = {"gs.vert", kGsVertWgsl},           [WG_SHADER_VU0_VERT] = {"vu0.vert", kVu0VertWgsl},
    [WG_SHADER_VU4_VERT] = {"vu4.vert", kVu4VertWgsl},        [WG_SHADER_VU6_VERT] = {"vu6.vert", kVu6VertWgsl},
    [WG_SHADER_FX_VERT] = {"fx.vert", kFxVertWgsl},           [WG_SHADER_GS_FRAG] = {"gs.frag", kGsFragWgsl},
    [WG_SHADER_GSNZ_FRAG] = {"gsnz.frag", kGsnzFragWgsl}, /* (gs.frag without its depth output: key bit 23) */
    [WG_SHADER_DCLUT_FRAG] = {"dclut.frag", kDclutFragWgsl},  [WG_SHADER_OUTLINE_FRAG] = {"outline.frag", kOutlineFragWgsl},
    [WG_SHADER_BLIT_FRAG] = {"blit.frag", kBlitFragWgsl},
};
/* (alphakey.frag is GS_NATIVE_ALPHA_KEY's, which nothing records any more: not made here) */
static const int kVertexShader[] = {[GS_VERTS_GS] = WG_SHADER_GS_VERT, [GS_VERTS_PROGRAM0] = WG_SHADER_VU0_VERT,
                                    [GS_VERTS_PROGRAM4] = WG_SHADER_VU4_VERT, [GS_VERTS_PROGRAM6] = WG_SHADER_VU6_VERT};
static WGPUShaderModule sShaders[WG_SHADER_COUNT];

/* The vertices as gs_gl.c's vertex arrays take them: the GS's (Vtx) and the vertex programs' (three vec4). */
static const WGPUVertexAttribute kGsAttributes[] = {
    {NULL, WGPUVertexFormat_Float32x3, offsetof(Vtx, x), 0},
    {NULL, WGPUVertexFormat_Unorm8x4, offsetof(Vtx, r), 1},
    {NULL, WGPUVertexFormat_Float32x3, offsetof(Vtx, s), 2},
};
static const WGPUVertexAttribute kVuAttributes[] = {
    {NULL, WGPUVertexFormat_Float32x4, 0 * 4 * sizeof(float), 0},
    {NULL, WGPUVertexFormat_Float32x4, 1 * 4 * sizeof(float), 1},
    {NULL, WGPUVertexFormat_Float32x4, 2 * 4 * sizeof(float), 2},
};

/* The shared state values (gs_draw.h) as WebGPU's. */
static const WGPUCompareFunction kWgDepthCompare[] = {[GS_DEPTH_NEVER] = WGPUCompareFunction_Never, [GS_DEPTH_ALWAYS] = WGPUCompareFunction_Always,
                                                      [GS_DEPTH_GEQUAL] = WGPUCompareFunction_GreaterEqual,
                                                      [GS_DEPTH_GREATER] = WGPUCompareFunction_Greater};
static const WGPUPrimitiveTopology kWgTopology[] = {[GS_TOPOLOGY_TRIANGLES] = WGPUPrimitiveTopology_TriangleList,
                                                    [GS_TOPOLOGY_LINES] = WGPUPrimitiveTopology_LineList,
                                                    [GS_TOPOLOGY_POINTS] = WGPUPrimitiveTopology_PointList};
static const WGPUBlendOperation kWgBlendOp[] = {[GS_OP_ADD] = WGPUBlendOperation_Add, [GS_OP_SUBTRACT] = WGPUBlendOperation_Subtract,
                                                [GS_OP_REVERSE_SUBTRACT] = WGPUBlendOperation_ReverseSubtract};

static WGPUBlendFactor wg_blend_factor(GsBlendFactor f, GsBlendC c) {
    switch (f) {
    case GS_FACTOR_ZERO: return WGPUBlendFactor_Zero;
    case GS_FACTOR_ONE: return WGPUBlendFactor_One;
    case GS_FACTOR_C:
        return c == GS_BLEND_C_SRC_ALPHA ? WGPUBlendFactor_SrcAlpha : c == GS_BLEND_C_DST_ALPHA ? WGPUBlendFactor_DstAlpha
                                                                                              : WGPUBlendFactor_Constant;
    case GS_FACTOR_ONE_MINUS_C: break;
    }
    return c == GS_BLEND_C_SRC_ALPHA ? WGPUBlendFactor_OneMinusSrcAlpha
           : c == GS_BLEND_C_DST_ALPHA ? WGPUBlendFactor_OneMinusDstAlpha : WGPUBlendFactor_OneMinusConstant;
}

static WGPUColorWriteMask wg_write_mask(uint32_t cc) {
    return ((cc & GS_CC_R) ? WGPUColorWriteMask_Red : WGPUColorWriteMask_None) |
           ((cc & GS_CC_G) ? WGPUColorWriteMask_Green : WGPUColorWriteMask_None) |
           ((cc & GS_CC_B) ? WGPUColorWriteMask_Blue : WGPUColorWriteMask_None) |
           ((cc & GS_CC_A) ? WGPUColorWriteMask_Alpha : WGPUColorWriteMask_None);
}

/* The alpha channel is not blended (gs_draw.h, GsBlend_Describe): the frame buffer keeps the source alpha. */
static const WGPUBlendComponent kAlphaKept = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_Zero};

static int shaders_create(void) {
    int i;
    for (i = 0; i < WG_SHADER_COUNT; i++) {
        WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
        WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        source.code = string_view(kShaders[i].wgsl);
        desc.nextInChain = &source.chain;
        desc.label = string_view(kShaders[i].name);
        sShaders[i] = wgpuDeviceCreateShaderModule(sDevice, &desc);
        if (sShaders[i] == NULL) {
            fprintf(stderr, "bt3: webgpu: shader %s not made\n", kShaders[i].name);
            return 0;
        }
    }
    return 1;
}

/* What every pipeline into a target has in common: the stages, the colour and the alpha byte as targets 0 and 1. */
typedef struct PipeDesc {
    WGPURenderPipelineDescriptor desc;
    WGPUFragmentState fragment;
    WGPUColorTargetState targets[2];
    WGPUBlendState blend;
    WGPUDepthStencilState depth;
    WGPUVertexBufferLayout buffer;
} PipeDesc;

static void pipe_desc_init(PipeDesc *p, WGPUPipelineLayout layout, int vertexShader, int fragmentShader, const char *label) {
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    WGPUVertexBufferLayout buffer = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    p->desc = desc;
    p->fragment = fragment;
    p->targets[0] = target;
    p->targets[1] = target;
    p->depth = depth;
    p->buffer = buffer;
    p->desc.label = string_view(label);
    p->desc.layout = layout;
    p->desc.vertex.module = sShaders[vertexShader];
    p->desc.vertex.entryPoint = string_view(WG_ENTRY_POINT);
    p->desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    p->desc.primitive.cullMode = WGPUCullMode_None;
    p->desc.primitive.unclippedDepth = sUnclippedDepth;
    p->fragment.module = sShaders[fragmentShader];
    p->fragment.entryPoint = string_view(WG_ENTRY_POINT);
    p->fragment.targets = p->targets;
    p->fragment.targetCount = 2;
    p->targets[0].format = WG_COLOR_FORMAT;
    p->targets[1].format = WG_AUX_FORMAT;
    p->desc.fragment = &p->fragment;
}

static WGPURenderPipeline pipe_desc_create(PipeDesc *p) {
    sMade.pipelines++;
    gGpuNewPipes++;
    return wgpuDeviceCreateRenderPipeline(sDevice, &p->desc);
}

/* The depth state of a pass with the target's depth attached (every pass but the table pass). */
static void pipe_desc_depth(PipeDesc *p, WGPUCompareFunction compare, int write) {
    p->depth.format = WG_DEPTH_FORMAT;
    p->depth.depthCompare = compare;
    p->depth.depthWriteEnabled = write ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    p->desc.depthStencil = &p->depth;
}

/* A pipeline for a key: the primitives (gs.frag after the GS's or a vertex program's stage), or the table pass. */
static WGPURenderPipeline pipeline_create(const GsPipeState *s) {
    PipeDesc p;
    if (s->tablePass) {
        /* drawn in a pass without the depth attachment: it samples the target's depth (dclut.frag) */
        pipe_desc_init(&p, sPipeLayoutTable, WG_SHADER_FX_VERT, WG_SHADER_DCLUT_FRAG, "table pass");
    } else {
        pipe_desc_init(&p, sPipeLayoutDraw, kVertexShader[s->vertices], s->noFragDepth ? WG_SHADER_GSNZ_FRAG : WG_SHADER_GS_FRAG, "draw");
        if (s->vertices == GS_VERTS_GS) {
            p.buffer.arrayStride = sizeof(Vtx);
            p.buffer.attributeCount = sizeof(kGsAttributes) / sizeof(kGsAttributes[0]);
            p.buffer.attributes = kGsAttributes;
            p.desc.primitive.topology = kWgTopology[s->topology];
        } else {
            /* (the vertex programs' draws are indexed triangle lists, whatever the key's topology: gs_gl.c draws them so) */
            p.buffer.arrayStride = VU_VERT_BYTES;
            p.buffer.attributeCount = sizeof(kVuAttributes) / sizeof(kVuAttributes[0]);
            p.buffer.attributes = kVuAttributes;
        }
        p.desc.vertex.bufferCount = 1;
        p.desc.vertex.buffers = &p.buffer;
        pipe_desc_depth(&p, kWgDepthCompare[s->depthTest], s->depthWrite);
    }
    if (s->blend.enabled) {
        GsBlendDesc d = GsBlend_Describe(s->blend);
        p.blend.color.operation = kWgBlendOp[d.op];
        p.blend.color.srcFactor = wg_blend_factor(d.srcFactor, d.c);
        p.blend.color.dstFactor = wg_blend_factor(d.dstFactor, d.c);
        p.blend.alpha = kAlphaKept;
        p.targets[0].blend = &p.blend;
    }
    p.targets[0].writeMask = wg_write_mask(s->writeMask);
    p.targets[1].writeMask = (s->writeMask & GS_CC_A) ? WGPUColorWriteMask_Red : WGPUColorWriteMask_None;
    return pipe_desc_create(&p);
}

typedef struct WgPipe {
    uint32_t key;
    GsPipeState s; /* the key decoded (gs_draw.h) */
    WGPURenderPipeline pipeline;
} WgPipe;
static WgPipe sPipes[WG_MAX_PIPES];
static int sPipeCount;
static int sPipesFullReported;
/* the back end's own passes */
static WGPURenderPipeline sOutlinePipe, sBlitTargetPipe, sBlitSurfacePipe;

static int wg_pipe_get(uint32_t key) {
    int i;
    for (i = 0; i < sPipeCount; i++) {
        if (sPipes[i].key == key) {
            return i;
        }
    }
    if (sPipeCount == WG_MAX_PIPES) {
        if (!sPipesFullReported) {
            fprintf(stderr, "bt3: webgpu: more than %d pipelines: key %08x is drawn with pipeline 0's state (reported once)\n",
                    WG_MAX_PIPES, key);
            sPipesFullReported = 1;
        }
        return 0;
    }
    sPipes[sPipeCount].key = key;
    GsPipeKey_Decode(key, &sPipes[sPipeCount].s);
    sPipes[sPipeCount].pipeline = pipeline_create(&sPipes[sPipeCount].s);
    return sPipeCount++;
}

/* The outline (GS_NATIVE_OUTLINE): darkens the colour where outline.frag finds an edge, in the target's own pass; the
   alpha byte and depth stay as they are. */
static WGPURenderPipeline outline_pipeline_create(void) {
    static const WGPUBlendState darken = {{WGPUBlendOperation_ReverseSubtract, WGPUBlendFactor_One, WGPUBlendFactor_One},
                                          {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_Zero}};
    PipeDesc p;
    pipe_desc_init(&p, sPipeLayoutOneTexture, WG_SHADER_FX_VERT, WG_SHADER_OUTLINE_FRAG, "outline");
    p.blend = darken;
    p.targets[0].blend = &p.blend;
    p.targets[0].writeMask = WGPUColorWriteMask_Red | WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue;
    p.targets[1].writeMask = WGPUColorWriteMask_None;
    pipe_desc_depth(&p, WGPUCompareFunction_Always, 0);
    return pipe_desc_create(&p);
}

/* blit.frag into one colour target of this format (no alpha byte, no depth). */
static WGPURenderPipeline blit_pipeline_create(WGPUTextureFormat format, const char *label) {
    PipeDesc p;
    pipe_desc_init(&p, sPipeLayoutOneTexture, WG_SHADER_FX_VERT, WG_SHADER_BLIT_FRAG, label);
    p.targets[0].format = format;
    p.fragment.targetCount = 1;
    return pipe_desc_create(&p);
}

/* ---- buffers -------------------------------------------------------------------------------------------------- */

static WGPUBuffer sVertexBuffer, sVuVertexBuffer, sVuIndexBuffer;

/* The frame's uniforms: every vertex program constant block (U) and each draw's Params, in one buffer bound with
   dynamic offsets. Filled on the CPU while the list is replayed, written to the queue once before the submit. */
static struct {
    WGPUBuffer buffer;
    uint8_t *cpu;
    uint64_t capacity, used;
    uint32_t programSlot, paramsSlot; /* bytes per U / per Params, at the device's offset alignment */
    WGPUBindGroup program, params;    /* groups 1 and 3 over the buffer */
} sUniforms;

static WGPUBuffer buffer_create(uint64_t size, WGPUBufferUsage usage, const char *label) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.label = string_view(label);
    desc.size = size;
    desc.usage = usage;
    sGpuBytes += size;
    return wgpuDeviceCreateBuffer(sDevice, &desc);
}

static void buffer_free(WGPUBuffer buffer) {
    sGpuBytes -= wgpuBufferGetSize(buffer);
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);
}

static WGPUBindGroup uniform_group(WGPUBindGroupLayout layout, uint64_t size) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = sUniforms.buffer;
    entry.size = size;
    return group_create(layout, &entry, 1);
}

/* Room for `bytes` this frame: the buffer and its groups are made again, larger, when they are short. */
static void uniforms_reserve(uint64_t bytes) {
    if (bytes <= sUniforms.capacity) {
        return;
    }
    if (bytes < sUniforms.capacity * 2) {
        bytes = sUniforms.capacity * 2;
    }
    if (sUniforms.buffer != NULL) {
        wgpuBindGroupRelease(sUniforms.program);
        wgpuBindGroupRelease(sUniforms.params);
        buffer_free(sUniforms.buffer);
    }
    sUniforms.cpu = realloc(sUniforms.cpu, (size_t)bytes);
    sUniforms.capacity = bytes;
    sUniforms.buffer = buffer_create(bytes, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "uniforms");
    sUniforms.program = uniform_group(sLayoutProgram, sizeof(Vu0Uniform));
    sUniforms.params = uniform_group(sLayoutParams, sizeof(GsParams));
}

static uint32_t uniforms_push(const void *data, size_t size, uint32_t slot) {
    uint32_t offset = (uint32_t)sUniforms.used;
    memcpy(sUniforms.cpu + offset, data, size);
    memset(sUniforms.cpu + offset + size, 0, slot - size);
    sUniforms.used += slot;
    return offset;
}

static void buffers_create(void) {
    const WGPUBufferUsage vertex = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    sVertexBuffer = buffer_create((uint64_t)MAX_VERTS * sizeof(Vtx), vertex, "GS vertices");
    sVuVertexBuffer = buffer_create((uint64_t)MAX_VU_VERTS * VU_VERT_BYTES, vertex, "vertex programs' vertices");
    sVuIndexBuffer = buffer_create((uint64_t)MAX_VU_IDX * sizeof(uint32_t), WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
                                   "vertex programs' triangles");
    sUniforms.programSlot = align_up(sizeof(Vu0Uniform), sLimits.minUniformBufferOffsetAlignment);
    sUniforms.paramsSlot = align_up(sizeof(GsParams), sLimits.minUniformBufferOffsetAlignment);
    uniforms_reserve((uint64_t)sUniforms.programSlot + sUniforms.paramsSlot);
}

/* ---- reading textures back ------------------------------------------------------------------------------------ */

/* The top-left w x h texels of t (texelBytes each), rows packed, top row first, into out. 1 = read. */
static int texture_read(WgTexture *t, uint32_t w, uint32_t h, uint32_t texelBytes, uint8_t *out) {
    const uint32_t row = align_up(w * texelBytes, WG_COPY_ROW_ALIGNMENT);
    const uint64_t size = (uint64_t)row * h;
    WGPUBuffer buffer = buffer_create(size, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst, "read back");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(sDevice, NULL);
    WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    WGPUExtent3D extent = {w, h, 1};
    WGPUCommandBuffer commands;
    WGPUBufferMapCallbackInfo cb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
    int ok = 0;
    uint32_t y;

    sMade.readbacks++;
    src.texture = t->texture;
    dst.buffer = buffer;
    dst.layout.bytesPerRow = row;
    dst.layout.rowsPerImage = h;
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
    commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(sQueue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    cb.mode = WGPUCallbackMode_WaitAnyOnly;
    cb.callback = on_mapped;
    cb.userdata1 = &status;
    if (wait_for(wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, (size_t)size, cb)) && status == WGPUMapAsyncStatus_Success) {
        const uint8_t *mapped = wgpuBufferGetConstMappedRange(buffer, 0, (size_t)size);
        if (mapped != NULL) {
            for (y = 0; y < h; y++) {
                memcpy(out + (size_t)y * w * texelBytes, mapped + (size_t)y * row, (size_t)w * texelBytes);
            }
            ok = 1;
        }
        wgpuBufferUnmap(buffer);
    }
    buffer_free(buffer);
    return ok;
}

static int wg_target_read(int i, uint8_t *rgba) {
    const uint32_t w = GS_DISPLAY_W * SCALE, h = GS_DISPLAY_H * SCALE;
    uint8_t *px;
    int ok;
    if (sTargets[i].color == NULL || (px = malloc((size_t)w * h * 4)) == NULL) {
        return 0;
    }
    ok = texture_read(sTargets[i].color, w, h, 4, px);
    if (ok) {
        GsDraw_DisplayReduce(px, rgba);
    }
    free(px);
    return ok;
}

static void wg_target_copy(int src, int dst) {
    WGPUCommandEncoder encoder;
    WGPUTexelCopyTextureInfo from = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT, to = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    WGPUExtent3D extent = {GS_W * SCALE, GS_H * SCALE, 1};
    WGPUCommandBuffer commands;
    if (sTargets[src].color == NULL || sTargets[dst].color == NULL) {
        return;
    }
    /* submitted now: after the frames before, before this frame's replay (as gs_gl.c copies at once) */
    encoder = wgpuDeviceCreateCommandEncoder(sDevice, NULL);
    from.texture = sTargets[src].color->texture;
    to.texture = sTargets[dst].color->texture;
    wgpuCommandEncoderCopyTextureToTexture(encoder, &from, &to, &extent);
    commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(sQueue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

/* ---- init ----------------------------------------------------------------------------------------------------- */

static int sMagentaClear; /* BT3_GPU_MAGENTA: targets cleared to magenta (what was never drawn shows), as gs_gl.c */
static int sFxDebug;      /* BT3_FX_DEBUG: outline.frag's debug view, as gs_gl.c */

/* The fixed objects (layouts, shaders, the back end's pipelines, buffers, samplers, the white texture, the copies),
   checked in an error scope: an error here stops the game with WebGPU's message instead of drawing wrongly. */
static int objects_create(void) {
    static const uint32_t white = 0xFFFFFFFFu;
    ScopeResult scope = {WGPUErrorType_NoError, ""};
    WGPUPopErrorScopeCallbackInfo cb = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;

    wgpuDevicePushErrorScope(sDevice, WGPUErrorFilter_Validation);
    layouts_create();
    if (!shaders_create()) {
        return 0;
    }
    samplers_create();
    buffers_create();
    sOutlinePipe = outline_pipeline_create();
    sBlitTargetPipe = blit_pipeline_create(WG_COLOR_FORMAT, "blit into a target");
    sBlitSurfacePipe = blit_pipeline_create(sSurfaceFormat, "present");
    sWhite = texture_new(1, 1, WGPUTextureFormat_RGBA8Unorm, 1, WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, "white");
    texture_write(sWhite, 0, 1, 1, GS_TEXFMT_RGBA8, &white, sizeof(white));
    copies_create();
    timing_create();
    cb.mode = WGPUCallbackMode_WaitAnyOnly;
    cb.callback = on_scope;
    cb.userdata1 = &scope;
    if (!wait_for(wgpuDevicePopErrorScope(sDevice, cb)) || scope.type != WGPUErrorType_NoError) {
        fprintf(stderr, "bt3: webgpu: the back end's objects could not be made (%s): %s\n", error_name(scope.type), scope.message);
        return 0;
    }
    return 1;
}

static int wg_init(void) {
    static const WGPUInstanceFeatureName instanceFeatures[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor desc = WGPU_INSTANCE_DESCRIPTOR_INIT;

    sWindow = GsDraw_WindowCreate(0); /* the shared window, without an OpenGL context: the canvas gets a WebGPU one */
    if (sWindow == NULL) {
        return 0;
    }
    desc.requiredFeatureCount = sizeof(instanceFeatures) / sizeof(instanceFeatures[0]);
    desc.requiredFeatures = instanceFeatures;
    sInstance = wgpuCreateInstance(&desc);
    if (sInstance == NULL) {
        fprintf(stderr, "bt3: webgpu: not available in this browser\n");
        return 0;
    }
    {
        WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        WGPURequestAdapterCallbackInfo cb = {NULL, WGPUCallbackMode_WaitAnyOnly, on_adapter, &sAdapter, NULL};
        options.powerPreference = WGPUPowerPreference_HighPerformance;
        if (!wait_for(wgpuInstanceRequestAdapter(sInstance, &options, cb)) || sAdapter == NULL) {
            return 0;
        }
    }
    print_adapter();
    if (!device_create() || !surface_create()) {
        return 0;
    }
    surface_fit();
    sMagentaClear = getenv("BT3_GPU_MAGENTA") != NULL;
    sFxDebug = getenv("BT3_FX_DEBUG") != NULL;
    if (!objects_create()) {
        return 0;
    }
    fprintf(stderr, "bt3: webgpu: surface %ux%u, depth clamp %s, BC textures %s, uniform offset alignment %u\n", sSurfaceW,
            sSurfaceH, sUnclippedDepth ? "yes" : "NO", sBcTextures ? "yes" : "no", sLimits.minUniformBufferOffsetAlignment);
    /* the settings are the page's (port/src/web/web_ui.c): nothing is drawn over the game */
    return Ui_Init(sWindow, NULL, NULL);
}

/* Validation, out-of-memory and internal errors the device has reported (the page's developer panel shows them). */
unsigned GsWgpu_DeviceErrors(void) {
    return sDeviceErrors;
}

/* Error i, 0 the latest; "" past what is kept. */
const char *GsWgpu_Error(unsigned i) {
    return i < WG_ERRORS_KEPT && i < sDeviceErrors ? sErrorText[(sDeviceErrors - 1 - i) % WG_ERRORS_KEPT] : "";
}

/* What the engine's live GPU textures and buffers take by their sizes (an estimate: see sGpuBytes), in KB. */
unsigned GsWgpu_GpuKBytes(void) {
    return (unsigned)(sGpuBytes / 1024u);
}

/* 1 = frames are timed on the GPU (timestamp-query); the frames left untimed because every slot was busy. */
int GsWgpu_GpuTiming(void) {
    return sTimestamps;
}
unsigned GsWgpu_GpuTimingSkipped(void) {
    return sTimingSkipped;
}

/* ---- GPU time per frame (timestamp-query) --------------------------------------------------------------------
   The first render pass of a frame writes a timestamp as it begins, the present pass one as it ends; the pair is
   resolved and copied into a small buffer that is mapped without waiting (the game never waits for it). A few slots
   go round; a frame that finds none free is not timed (sTimingSkipped). The result goes to the page's ring
   (web_ui.c Web_GpuSample) when the map completes. */
#define WG_TIMING_SLOTS 4
#define WG_TIMESTAMPS_PER_FRAME 2
#define WG_QUERY_RESOLVE_ALIGNMENT 256u /* WebGPU: a resolve's destination offset */
/* Emscripten's WebGPU glue (emdawnwebgpu v20260423.175430, library_webgpu.js) copies a pass's two timestamp indices as
   numbers and does not turn WGPU_QUERY_SET_INDEX_UNDEFINED into "none": the browser then rejects the pass
   ("endOfPassWriteIndex (4294967295) is too large") and the frame's commands with it. So a pass that times one end
   writes its other end into this query, which is never read. */
#define WG_TIMESTAMP_SCRATCH_QUERY (WG_TIMING_SLOTS * WG_TIMESTAMPS_PER_FRAME)
extern void Web_GpuSample(unsigned frame, double ms); /* port/src/web/web_ui.c */
static WGPUQuerySet sQuerySet;
static WGPUBuffer sQueryResolve;
static struct {
    WGPUBuffer readback;
    int busy;
    unsigned frame;
} sTiming[WG_TIMING_SLOTS];
static struct {
    int slot;   /* this frame's, -1: not timed */
    int begun;  /* the begin timestamp has a pass */
    int ended;  /* the end one has (the present pass ran) */
} sFrameTiming = {-1, 0, 0};

static void timing_create(void) {
    WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    int i;
    if (!sTimestamps) {
        return;
    }
    desc.label = string_view("frame time");
    desc.type = WGPUQueryType_Timestamp;
    desc.count = WG_TIMING_SLOTS * WG_TIMESTAMPS_PER_FRAME + 1; /* (+ WG_TIMESTAMP_SCRATCH_QUERY) */
    sQuerySet = wgpuDeviceCreateQuerySet(sDevice, &desc);
    sQueryResolve = buffer_create(WG_TIMING_SLOTS * WG_QUERY_RESOLVE_ALIGNMENT, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
                                  "frame time resolve");
    for (i = 0; i < WG_TIMING_SLOTS; i++) {
        sTiming[i].readback = buffer_create(WG_TIMESTAMPS_PER_FRAME * sizeof(uint64_t), WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
                                            "frame time read-back");
    }
}

static void timing_frame_begin(void) {
    int i;
    sFrameTiming.slot = -1;
    sFrameTiming.begun = sFrameTiming.ended = 0;
    if (!sTimestamps) {
        return;
    }
    for (i = 0; i < WG_TIMING_SLOTS && sTiming[i].busy; i++) {
    }
    if (i == WG_TIMING_SLOTS) {
        sTimingSkipped++;
        return;
    }
    sFrameTiming.slot = i;
}

/* The timestamp writes of a pass: the frame's begin on its first pass, its end on the present pass (`last`). NULL when
   the frame is not timed or the pass writes none. */
static const WGPUPassTimestampWrites *timing_pass(WGPUPassTimestampWrites *w, int last) {
    WGPUPassTimestampWrites init = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    if (sFrameTiming.slot < 0 || (sFrameTiming.begun && !last)) {
        return NULL;
    }
    *w = init;
    w->querySet = sQuerySet;
    w->beginningOfPassWriteIndex = sFrameTiming.begun ? WG_TIMESTAMP_SCRATCH_QUERY : (uint32_t)(sFrameTiming.slot * WG_TIMESTAMPS_PER_FRAME);
    w->endOfPassWriteIndex = last ? (uint32_t)(sFrameTiming.slot * WG_TIMESTAMPS_PER_FRAME + 1) : WG_TIMESTAMP_SCRATCH_QUERY;
    sFrameTiming.begun = 1;
    sFrameTiming.ended = last;
    return w;
}

/* Into the encoder before it is finished: the frame's pair resolved and copied for reading. */
static void timing_resolve(WGPUCommandEncoder encoder) {
    const int s = sFrameTiming.slot;
    if (s < 0 || !sFrameTiming.ended) {
        sFrameTiming.slot = -1; /* (no present this frame: nothing to read) */
        return;
    }
    wgpuCommandEncoderResolveQuerySet(encoder, sQuerySet, (uint32_t)(s * WG_TIMESTAMPS_PER_FRAME), WG_TIMESTAMPS_PER_FRAME,
                                      sQueryResolve, (uint64_t)s * WG_QUERY_RESOLVE_ALIGNMENT);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, sQueryResolve, (uint64_t)s * WG_QUERY_RESOLVE_ALIGNMENT, sTiming[s].readback, 0,
                                         WG_TIMESTAMPS_PER_FRAME * sizeof(uint64_t));
}

static void on_timing_mapped(WGPUMapAsyncStatus status, WGPUStringView message, void *slot, void *unused) {
    const int s = (int)(intptr_t)slot;
    (void)unused;
    if (status == WGPUMapAsyncStatus_Success) {
        const uint64_t *ns = wgpuBufferGetConstMappedRange(sTiming[s].readback, 0, WG_TIMESTAMPS_PER_FRAME * sizeof(uint64_t));
        if (ns != NULL && ns[1] > ns[0]) { /* (a pair the GPU did not write, or that the browser rounded to equal: no time) */
            Web_GpuSample(sTiming[s].frame, (double)(ns[1] - ns[0]) / 1e6);
        }
        wgpuBufferUnmap(sTiming[s].readback);
    } else if (status != WGPUMapAsyncStatus_CallbackCancelled) {
        fprintf(stderr, "bt3: webgpu: a frame time could not be read: %.*s\n", string_length(message), message.data);
    }
    sTiming[s].busy = 0;
}

/* After the submit: the read-back is mapped when the GPU is done with the frame (the game does not wait). */
static void timing_frame_end(void) {
    const int s = sFrameTiming.slot;
    WGPUBufferMapCallbackInfo cb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    if (s < 0) {
        return;
    }
    sTiming[s].busy = 1;
    sTiming[s].frame = gGsFrame;
    cb.mode = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = on_timing_mapped;
    cb.userdata1 = (void *)(intptr_t)s;
    wgpuBufferMapAsync(sTiming[s].readback, WGPUMapMode_Read, 0, WG_TIMESTAMPS_PER_FRAME * sizeof(uint64_t), cb);
}

/* ---- the replay ----------------------------------------------------------------------------------------------- */

/* Which attachments a pass has. The table pass samples the target's depth, so its pass leaves depth out (it neither
   tests nor writes it); a picture uploaded into a display buffer is drawn into the colour alone, as gs_gl.c's blit. */
typedef enum { WG_PASS_NONE, WG_PASS_DRAW, WG_PASS_TABLE, WG_PASS_UPLOAD } WgPassKind;

/* The replay's state: the open pass and what it has bound (a pass starts with nothing bound). */
static struct {
    WGPUCommandEncoder encoder;
    WGPURenderPassEncoder pass;
    WgPassKind kind;
    int target;
    WGPURenderPipeline pipeline;
    WGPUBindGroup groups[WG_GROUP_COUNT];
    uint32_t offsets[WG_GROUP_COUNT];
    int vertices; /* -1, or GsDraw.vu != 0 */
    float blendc;
    int scissor[4];
    GsParams lastParams; /* the Params last pushed (the next draw with the same reuses them) */
    uint32_t lastParamsOffset;
    int haveLastParams;
} sR;

static void pass_end(void) {
    if (sR.pass != NULL) {
        wgpuRenderPassEncoderEnd(sR.pass);
        wgpuRenderPassEncoderRelease(sR.pass);
        sR.pass = NULL;
    }
    sR.kind = WG_PASS_NONE;
    sR.target = -1;
}

static void pass_begin(WgPassKind kind, int target, int clear) {
    const WgTarget *t = &sTargets[target];
    WGPURenderPassColorAttachment colors[2] = {WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT, WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT};
    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    const WGPULoadOp load = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    WGPUPassTimestampWrites timestamps;
    int i;

    pass_end();
    /* cleared as the other back ends clear: colour and alpha 0 (magenta with the aid), the alpha byte 0, depth 0 */
    colors[0].view = t->color->view;
    colors[0].loadOp = load;
    colors[0].storeOp = WGPUStoreOp_Store;
    if (sMagentaClear) {
        colors[0].clearValue.r = colors[0].clearValue.b = colors[0].clearValue.a = 1.0;
    }
    colors[1].view = t->aux->view;
    colors[1].loadOp = load;
    colors[1].storeOp = WGPUStoreOp_Store;
    depth.view = t->depth->view;
    depth.depthLoadOp = load;
    depth.depthStoreOp = WGPUStoreOp_Store;
    depth.depthClearValue = 0.0f;
    desc.colorAttachments = colors;
    desc.colorAttachmentCount = kind == WG_PASS_UPLOAD ? 1 : 2;
    desc.depthStencilAttachment = kind == WG_PASS_DRAW ? &depth : NULL;
    desc.timestampWrites = timing_pass(&timestamps, 0);
    sR.pass = wgpuCommandEncoderBeginRenderPass(sR.encoder, &desc);
    sR.kind = kind;
    sR.target = target;
    sR.pipeline = NULL;
    for (i = 0; i < WG_GROUP_COUNT; i++) {
        sR.groups[i] = NULL;
    }
    sR.vertices = -1;
    sR.blendc = 0.0f; /* (a pass's blend constant starts at 0) */
    sR.scissor[2] = -1;
}

static void set_pipeline(WGPURenderPipeline pipeline) {
    if (sR.pipeline != pipeline) {
        wgpuRenderPassEncoderSetPipeline(sR.pass, pipeline);
        sR.pipeline = pipeline;
    }
}

static void set_group(int index, WGPUBindGroup group, int dynamic, uint32_t offset) {
    if (sR.groups[index] != group || (dynamic && sR.offsets[index] != offset)) {
        wgpuRenderPassEncoderSetBindGroup(sR.pass, (uint32_t)index, group, dynamic ? 1 : 0, dynamic ? &offset : NULL);
        sR.groups[index] = group;
        sR.offsets[index] = offset;
    }
}

/* Groups 0 and 1 of the back end's full-screen passes (they use neither). */
static void set_fx_groups(void) {
    set_group(WG_GROUP_UNUSED, sEmptyGroup, 0, 0);
    set_group(WG_GROUP_PROGRAM, sEmptyGroup, 0, 0);
}

static void set_fx_params(float a, float b, float c, float d) {
    FxParams p = {{a, b, c, d}};
    set_group(WG_GROUP_PARAMS, sUniforms.params, 1, uniforms_push(&p, sizeof(p), sUniforms.paramsSlot));
}

static void set_blend_constant(float c) {
    if (sR.blendc != c) {
        WGPUColor color = {c, c, c, c};
        wgpuRenderPassEncoderSetBlendConstant(sR.pass, &color);
        sR.blendc = c;
    }
}

/* The draw's scissor (x, y, width, height at SCALE). WebGPU requires the rectangle inside the target, which OpenGL
   and Vulkan do not: it is cut to the target, which changes no pixel (nothing is drawn outside the target). */
static void set_scissor(const int *sc) {
    const int w = GS_W * SCALE, h = GS_H * SCALE;
    int x0 = sc[0] < 0 ? 0 : sc[0] > w ? w : sc[0], y0 = sc[1] < 0 ? 0 : sc[1] > h ? h : sc[1];
    int x1 = sc[0] + sc[2] > w ? w : sc[0] + sc[2], y1 = sc[1] + sc[3] > h ? h : sc[1] + sc[3];
    if (x1 < x0) { x1 = x0; }
    if (y1 < y0) { y1 = y0; }
    if (sR.scissor[0] != x0 || sR.scissor[1] != y0 || sR.scissor[2] != x1 - x0 || sR.scissor[3] != y1 - y0) {
        wgpuRenderPassEncoderSetScissorRect(sR.pass, (uint32_t)x0, (uint32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0));
        sR.scissor[0] = x0;
        sR.scissor[1] = y0;
        sR.scissor[2] = x1 - x0;
        sR.scissor[3] = y1 - y0;
    }
}

/* GS_NATIVE_FB_UPLOAD + buffer: the picture uploaded into a display buffer, drawn scaled over its target. */
static void replay_fb_upload(const GsDraw *d) {
    WgTexture *picture = sFbTex[d->native - GS_NATIVE_FB_UPLOAD];
    if (picture == NULL) {
        return;
    }
    pass_begin(WG_PASS_UPLOAD, d->target, 0);
    set_pipeline(sBlitTargetPipe);
    set_fx_groups();
    set_group(WG_GROUP_TEXTURES, blit_group(picture), 0, 0);
    set_fx_params(0.0f, 0.0f, 1.0f, 1.0f); /* (all of it) */
    wgpuRenderPassEncoderDraw(sR.pass, WG_FULLSCREEN_VERTICES, 1, 0, 0);
    pass_end();
    gsTargets[d->target].cleared = 1;
}

/* GS_NATIVE_COPY_IDS / _COPY_DATE: the target's alpha bytes as they are now, for the passes that read them. The DATE
   copy (draw `n`) covers only what the draws after it read (GsDraw_DateRegion, gs_draw.c; upstream 0.1.33 does the
   same in its GL and Vulkan back ends: the HUD makes nine of these a frame, each a small piece); none when nothing
   reads it. Texture rows are in the same order as Vulkan's, so the box applies as it is. */
static void replay_copy(const GsDraw *d, uint32_t n) {
    WGPUTexelCopyTextureInfo from = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT, to = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    WGPUExtent3D extent = {GS_W * SCALE, GS_H * SCALE, 1};
    int rx = 0, ry = 0, rw = GS_W * SCALE, rh = GS_H * SCALE;
    if (!gsTargets[d->target].cleared) {
        return;
    }
    if (d->native == GS_NATIVE_COPY_DATE && !GsDraw_DateRegion(n, &rx, &ry, &rw, &rh)) {
        return;
    }
    pass_end();
    from.origin.x = to.origin.x = (uint32_t)rx;
    from.origin.y = to.origin.y = (uint32_t)ry;
    extent.width = (uint32_t)rw;
    extent.height = (uint32_t)rh;
    from.texture = sTargets[d->target].aux->texture;
    to.texture = (d->native == GS_NATIVE_COPY_DATE ? sDateCopy : sAuxCopy)->texture;
    wgpuCommandEncoderCopyTextureToTexture(sR.encoder, &from, &to, &extent);
}

/* GS_NATIVE_TABLE_PASS: the depth or alpha byte through the table `tex` (dclut.frag), in a pass without depth. */
static void replay_table_pass(const GsDraw *d) {
    WGPUBindGroup group;
    if (!gsTargets[d->target].cleared) {
        return;
    }
    if (sR.kind != WG_PASS_TABLE || sR.target != d->target) {
        pass_begin(WG_PASS_TABLE, d->target, 0);
    }
    set_pipeline(sPipes[d->pipeline].pipeline);
    set_fx_groups();
    group = table_group(d->target, wg_texture(d->tex));
    set_group(WG_GROUP_TEXTURES, group, 0, 0);
    wgpuBindGroupRelease(group); /* (the pass holds it) */
    sR.groups[WG_GROUP_TEXTURES] = NULL; /* (never the same object again: the next table pass binds its own) */
    set_fx_params((float)d->src, d->misc[0], 0.0f, 0.0f);
    set_blend_constant(d->blendc);
    set_scissor(d->scissor);
    wgpuRenderPassEncoderDraw(sR.pass, WG_FULLSCREEN_VERTICES, 1, 0, 0);
}

/* GS_NATIVE_OUTLINE, in the target's own pass. */
static void replay_outline(const GsDraw *d) {
    if (!gsTargets[d->target].cleared) {
        return;
    }
    if (sR.kind != WG_PASS_DRAW || sR.target != d->target) {
        pass_begin(WG_PASS_DRAW, d->target, 0);
    }
    set_pipeline(sOutlinePipe);
    set_fx_groups();
    set_group(WG_GROUP_TEXTURES, sOutlineGroup, 0, 0);
    set_fx_params(1.0f / (float)GS_W, 1.0f / (float)GS_H, GS_OUTLINE_DARKNESS, sFxDebug ? 1.0f : 0.0f);
    set_scissor(d->scissor);
    wgpuRenderPassEncoderDraw(sR.pass, WG_FULLSCREEN_VERTICES, 1, 0, 0);
}

/* An ordinary primitive. */
static void replay_primitive(const GsDraw *d) {
    GsParams p;
    if (sR.kind != WG_PASS_DRAW || sR.target != d->target) {
        pass_begin(WG_PASS_DRAW, d->target, !gsTargets[d->target].cleared);
    }
    gsTargets[d->target].cleared = 1;
    set_pipeline(sPipes[d->pipeline].pipeline);
    if (sR.vertices != (d->vu != 0)) {
        if (d->vu) {
            wgpuRenderPassEncoderSetVertexBuffer(sR.pass, 0, sVuVertexBuffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(sR.pass, sVuIndexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        } else {
            wgpuRenderPassEncoderSetVertexBuffer(sR.pass, 0, sVertexBuffer, 0, WGPU_WHOLE_SIZE);
        }
        sR.vertices = d->vu != 0;
    }
    set_group(WG_GROUP_UNUSED, sEmptyGroup, 0, 0);
    /* the frame's U blocks are at the start of the uniform buffer, in order (frame_uniforms); gs.vert has none and
       takes the first */
    set_group(WG_GROUP_PROGRAM, sUniforms.program, 1, d->vu ? (uint32_t)d->uniform * sUniforms.programSlot : 0);
    set_group(WG_GROUP_TEXTURES, draw_group(wg_texture(d->tex), d->sampler), 0, 0);
    memset(&p, 0, sizeof(p));
    memcpy(p.mode, d->mode, sizeof(p.mode));
    memcpy(p.misc, d->misc, sizeof(p.misc));
    memcpy(p.rect, d->rect, sizeof(p.rect));
    memcpy(p.orig, d->orig, sizeof(p.orig));
    p.extra[0] = GsDraw_DepthMax(d);
    if (!sR.haveLastParams || memcmp(&p, &sR.lastParams, sizeof(p)) != 0) {
        sR.lastParams = p;
        sR.lastParamsOffset = uniforms_push(&p, sizeof(p), sUniforms.paramsSlot);
        sR.haveLastParams = 1;
    }
    set_group(WG_GROUP_PARAMS, sUniforms.params, 1, sR.lastParamsOffset);
    set_blend_constant(d->blendc);
    set_scissor(d->scissor);
    if (d->vu) {
        wgpuRenderPassEncoderDrawIndexed(sR.pass, d->count, 1, d->first, 0, 0);
    } else {
        wgpuRenderPassEncoderDraw(sR.pass, d->count, 1, d->first, 0);
    }
}

static void replay(void) {
    uint32_t n;
    sR.haveLastParams = 0;
    sR.kind = WG_PASS_NONE;
    sR.target = -1;
    for (n = 0; n < gsDrawCount; n++) {
        const GsDraw *d = &gsDraws[n];
        if (d->native >= GS_NATIVE_FB_UPLOAD) {
            replay_fb_upload(d);
        } else if (d->native == GS_NATIVE_COPY_DATE || d->native == GS_NATIVE_COPY_IDS) {
            replay_copy(d, n);
        } else if (d->native == GS_NATIVE_TABLE_PASS) {
            replay_table_pass(d);
        } else if (d->native == GS_NATIVE_OUTLINE) {
            replay_outline(d);
        } else if (d->native != GS_NATIVE_NONE) {
            static int reported;
            if (!reported) {
                fprintf(stderr, "bt3: webgpu: native pass %d is not drawn by this back end (gs_draw.h: no longer recorded) "
                                "(reported once)\n", d->native);
                reported = 1;
            }
        } else {
            replay_primitive(d);
        }
    }
    pass_end();
}

/* ---- the frame ------------------------------------------------------------------------------------------------ */

/* What the frame drew that the GPU needs before the replay: vertices, the display buffers' pictures, the U blocks. */
static void frame_uploads(void) {
    uint32_t i;
    if (gsVertCount != 0) {
        wgpuQueueWriteBuffer(sQueue, sVertexBuffer, 0, gsVerts, (size_t)gsVertCount * sizeof(Vtx));
    }
    if (gsVuVertCount != 0) {
        wgpuQueueWriteBuffer(sQueue, sVuVertexBuffer, 0, gsVuVerts, (size_t)gsVuVertCount * VU_VERT_BYTES);
    }
    if (gsVuIdxCount != 0) {
        wgpuQueueWriteBuffer(sQueue, sVuIndexBuffer, 0, gsVuIdx, (size_t)gsVuIdxCount * sizeof(uint32_t));
    }
    for (i = 0; i < GS_DISPLAY_BUFFERS; i++) {
        uint32_t *px;
        if (!(gGsFbUploads & (1u << i))) {
            continue;
        }
        if (sFbTex[i] == NULL) {
            sFbTex[i] = texture_new(GS_W, GS_H, WGPUTextureFormat_RGBA8Unorm, 1,
                                    WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, "display buffer picture");
        }
        px = malloc((size_t)GS_W * GS_H * 4);
        GsDraw_DisplayBufferPixels((int)i, px);
        texture_write(sFbTex[i], 0, GS_W, GS_H, GS_TEXFMT_RGBA8, px, (size_t)GS_W * GS_H * 4);
        free(px);
    }
    gGsFbUploads = 0;
}

/* Every U block of the frame at the start of the uniform buffer (U i at i * programSlot), and room for one Params per
   recorded draw and the present. */
static void frame_uniforms(void) {
    uint32_t i;
    uniforms_reserve((uint64_t)(gsVuUniCount > 0 ? gsVuUniCount : 1) * sUniforms.programSlot +
                     (uint64_t)(gsDrawCount + 1) * sUniforms.paramsSlot);
    sUniforms.used = 0;
    if (gsVuUniCount == 0) {
        Vu0Uniform none;
        memset(&none, 0, sizeof(none));
        uniforms_push(&none, sizeof(none), sUniforms.programSlot); /* (gs.vert's draws still bind group 1) */
    }
    for (i = 0; i < gsVuUniCount; i++) {
        uniforms_push(&gsVuUni[i], sizeof(Vu0Uniform), sUniforms.programSlot);
    }
}

/* The shown target's picture into the canvas, letterboxed as gs_gl.c does it. */
static void present(int best) {
    WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    WGPUPassTimestampWrites timestamps;
    WGPUTextureView view;

    surface_fit();
    wgpuSurfaceGetCurrentTexture(sSurface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        fprintf(stderr, "bt3: webgpu: no surface texture this frame (status %d): not presented\n", (int)surfaceTexture.status);
        return;
    }
    view = wgpuTextureCreateView(surfaceTexture.texture, NULL);
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue.a = 1.0; /* black around the picture */
    desc.colorAttachmentCount = 1;
    desc.colorAttachments = &color;
    desc.timestampWrites = timing_pass(&timestamps, 1);
    sR.pass = wgpuCommandEncoderBeginRenderPass(sR.encoder, &desc);
    if (best >= 0 && gsTargets[best].cleared) {
        const int sw = (int)sSurfaceW, sh = (int)sSurfaceH;
        int w, h, dx, top, i;
        GsDraw_PictureSize(sw, sh, &w, &h);
        dx = (sw - w) / 2;
        top = sh - (sh - h) / 2 - h; /* (gs_gl.c centres from the bottom; the same rows, counted from the top) */
        gUiPresentX = dx;
        gUiPresentY = top;
        gUiPresentW = w;
        gUiPresentH = h;
        for (i = 0; i < WG_GROUP_COUNT; i++) {
            sR.groups[i] = NULL;
        }
        sR.pipeline = NULL;
        wgpuRenderPassEncoderSetViewport(sR.pass, (float)dx, (float)top, (float)w, (float)h, 0.0f, 1.0f);
        set_pipeline(sBlitSurfacePipe);
        set_fx_groups();
        set_group(WG_GROUP_TEXTURES, blit_group(sTargets[best].color), 0, 0);
        /* the display area of the target: its top-left GS_DISPLAY_W x GS_DISPLAY_H */
        set_fx_params(0.0f, 0.0f, (float)GS_DISPLAY_W / (float)GS_W, (float)GS_DISPLAY_H / (float)GS_H);
        wgpuRenderPassEncoderDraw(sR.pass, WG_FULLSCREEN_VERTICES, 1, 0, 0);
    }
    wgpuRenderPassEncoderEnd(sR.pass);
    wgpuRenderPassEncoderRelease(sR.pass);
    sR.pass = NULL;
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surfaceTexture.texture);
    /* (no wgpuSurfacePresent: a canvas shows its texture when the page gets control back, at the vertical blank) */
}

/* BT3_SHOT: the shown target's display area (colour, or the alpha byte with BT3_SHOT_AUX), as gs_gl.c writes it. */
static void screenshot(int best) {
    const uint32_t w = GS_DISPLAY_W * SCALE, h = GS_DISPLAY_H * SCALE;
    const int aux = GsDraw_ShotAux();
    uint8_t *px;
    if (best < 0 || !gsTargets[best].cleared || !GsDraw_ShotDue()) {
        return;
    }
    px = malloc((size_t)w * h * 4);
    if (texture_read(aux ? sTargets[best].aux : sTargets[best].color, w, h, aux ? 1 : 4, px)) {
        GsDraw_ShotWrite(px, (int)w, (int)h, aux);
    }
    free(px);
}

static void wg_frame_end(void) {
    WGPUCommandBuffer commands;
    int best, i;

    GsDraw_PollEvents();
    if (getenv("BT3_GS_VERBOSE") != NULL && gGsFrame % 30 == 0) {
        fprintf(stderr, "gpu: frame %u: %u draws, %u vertices, %d targets, %d textures, %d pipelines, %u primitives of PS2-only "
                        "passes dropped, %u native effects; webgpu: %u errors, made %u textures (%d live), %u bind groups, %u "
                        "pipelines, %u read-backs; uniforms %u KB\n",
                gGsFrame, gsDrawCount, gsVertCount + gsVuVertCount, gsTargetCount, gsTexCount, sPipeCount, gsSkipped, gsNative,
                sDeviceErrors, sMade.textures, sTexturesLive, sMade.bindGroups, sMade.pipelines, sMade.readbacks,
                (unsigned)(sUniforms.capacity / 1024));
    }
    if (sDeviceLost) {
        if (!sDeviceLostReported) {
            fprintf(stderr, "bt3: webgpu: nothing more is drawn (the device is lost; reload the page)\n");
            sDeviceLostReported = 1;
        }
        GsDraw_FrameReset();
        return;
    }
    frame_uploads();
    frame_uniforms();
    sR.encoder = wgpuDeviceCreateCommandEncoder(sDevice, NULL);
    timing_frame_begin();
    replay();
    /* the shown buffer: the one sceGsSwapDBuff set up for this frame, else the first target */
    best = gGsMainFbp >= 0 ? GsDraw_TargetGet((uint32_t)gGsMainFbp, 0) : -1;
    for (i = 0; best < 0 && i < gsTargetCount; i++) {
        best = i;
    }
    present(best);
    timing_resolve(sR.encoder);
    wgpuQueueWriteBuffer(sQueue, sUniforms.buffer, 0, sUniforms.cpu, (size_t)sUniforms.used);
    commands = wgpuCommandEncoderFinish(sR.encoder, NULL);
    wgpuQueueSubmit(sQueue, 1, &commands);
    timing_frame_end();
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(sR.encoder);
    sR.encoder = NULL;
    screenshot(best);
    GsDraw_FrameReset();
}

GsBackend sWebGpuBackend = {
    "webgpu",
    wg_init,
    wg_frame_end,
    wg_scale_changed,
    wg_white_tex,
    wg_tex_create,
    wg_tex_upload,
    wg_tex_destroy,
    wg_tex_slots_left,
    wg_target_ensure,
    wg_target_drop,
    wg_target_color,
    wg_target_aux,
    wg_target_depth,
    wg_pipe_get,
    wg_target_read,
    wg_target_copy,
};
#endif
