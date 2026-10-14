/* The capture side of the GPU back ends: what a frame records (draws, vertices, textures, targets), the
 * GsBackend interface each back end implements, and the state both sides share. gs_draw.c owns the
 * recording; gs_gpu.c (SDL3 GPU / Vulkan) and gs_gl.c (OpenGL) own the GPU objects, the replay of the list
 * at the end of the frame, the present, the settings window and the screenshots. gs_core.c only ever sees
 * the GsGpu_* entry points and the gGpu* counters, exactly as before the split. */
#ifndef GS_DRAW_H
#define GS_DRAW_H
#include <stdint.h>

/* The internal resolution multiplier: BT3_SCALE=1..8 (default 2). Every render target is GS_W x GS_H GS
   pixels at SCALE times the PS2's resolution, about 9 MB of video memory each at 1x, 36 MB at 2x, 144 MB at
   4x; a fight uses 7 to 16 of them. Shared: the capture scales coordinates with it, the back ends size
   their attachments with it. */
extern int gsScale;
#define SCALE gsScale
#define GS_W 1024
#define GS_H 1024

/* The part of a frame buffer the game shows (its display registers): 512 x 448 GS pixels, at SCALE in a target. */
#define GS_DISPLAY_W 512
#define GS_DISPLAY_H 448
/* The game's two display buffers: frame-buffer pages 0x000 and 0x070, 448 lines apart (its double buffering). */
#define GS_DISPLAY_BUFFERS 2
#define GS_DISPLAY_FBP(i) ((i) ? 0x70u : 0u)

/* GsDraw.native: a recorded step that is not a primitive; each back end replays it as a pass of its own. */
enum {
    GS_NATIVE_NONE = 0,
    GS_NATIVE_OUTLINE = 1,     /* the outline effect (the game's PORT_FX_OUTLINE marker, src/sys/gfxm_a.c) */
    GS_NATIVE_ALPHA_KEY = 2,   /* the see-through tint (PORT_FX_ALPHA_KEY): no longer recorded, the table pass draws it */
    GS_NATIVE_TABLE_PASS = 3,  /* the depth / alpha byte through a 256-entry table (dclut.frag) */
    GS_NATIVE_COPY_IDS = 4,    /* the alpha bytes (object ids) copied for the passes that read them */
    GS_NATIVE_COPY_DATE = 5,   /* the alpha bytes copied for the destination alpha test (TEST.DATE) */
    GS_NATIVE_FB_UPLOAD = 100, /* + display buffer: a picture uploaded into it (movies), drawn scaled into its target */
};

/* GsBackend.texSlotsLeft of a back end that uploads at once (no queue that could fill up within a frame). */
#define GS_TEX_SLOTS_UNLIMITED (1 << 20)

/* A draw's sampler (GsDraw.sampler): these flags, from TEX1.MMAG and CLAMP; a back end makes one sampler object for
   each of the GS_SAMPLER_COUNT combinations and indexes them with the value. */
#define GS_SAMPLER_LINEAR 1u  /* bilinear (TEX1.MMAG) */
#define GS_SAMPLER_CLAMP_U 2u /* CLAMP.WMS: clamped (else repeated) */
#define GS_SAMPLER_CLAMP_V 4u /* CLAMP.WMT */
#define GS_SAMPLER_MIPMAPS 8u /* a texture pack's replacement on 3D geometry: its smaller copies may be sampled */
#define GS_SAMPLER_COUNT 16
/* the samplers of the back ends' own passes (copies of targets, the present) */
#define GS_SAMPLER_NEAREST_CLAMPED (GS_SAMPLER_CLAMP_U | GS_SAMPLER_CLAMP_V)
#define GS_SAMPLER_LINEAR_CLAMPED (GS_SAMPLER_LINEAR | GS_SAMPLER_CLAMP_U | GS_SAMPLER_CLAMP_V)

/* A vertex program's vertex in gsVuVerts: the three float quadwords the program reads (position, normal or colour,
   texture coordinates). */
#define VU_VERT_FLOATS 12
#define VU_VERT_BYTES (VU_VERT_FLOATS * sizeof(float))

/* The native outline (PORT_FX_OUTLINE): the game's ObjOutline_Draw darkens by a rectangle of 0x64 per channel. */
#define GS_OUTLINE_DARKNESS (0x64 / 255.0f)

#define MAX_VERTS (1 << 20)
#define MAX_DRAWS (1 << 16)
#define MAX_TARGETS 24
#define MAX_VU_VERTS (1 << 19)
#define MAX_VU_IDX (1 << 20)
#define MAX_VU_UNIFORMS 8192
#define MAX_TEX 8192
#define TEX_BUCKETS 32768 /* a power of two, four times MAX_TEX */
#define MAX_CLUTS 64
/* Strength of glare and glow in percent of what the game's passes give (BT3_GLOW=<percent>; F6 / F7 change
   it by 10). Default 60: the user's choice on 2026-10-06 (100 looked too strong at the glare's peaks). */
#define GLOW_DEFAULT 60

/* A vertex of a draw list, interleaved the way every back end receives it. */
typedef struct Vtx {
    float x, y, z;
    uint8_t r, g, b, a;
    float s, t, q;
} Vtx;

/* A texture or a frame-buffer attachment inside a back end. What a draw samples from is stored as this:
   the Vulkan back end keeps its SDL_GPUTexture * in it, the OpenGL back end its GLuint. 0 = none (the
   callers fall back to the white texture). */
typedef uintptr_t GsTex;

/* std140 layout of the uniform block the vertex programs take (shaders/vu0.vert and friends). */
typedef struct Vu0Uniform {
    float boneA[16], boneB[16], pivotA[4], pivotB[4], screen[16], light[4], color0[4], color1[4], misc[4];
    float light2[4]; /* program 1: the y components of 10..13 (the second texture coordinate of its layer 1) */
} Vu0Uniform;

/* The shared side of a render target: everything that is GS state (which frame-buffer address it is,
   whether it holds a picture, when it was last used). The colour / aux / depth attachments are the back
   end's, in arrays parallel to gsTargets[]. */
typedef struct GsTarget {
    uint32_t fbp;
    int cleared;
    unsigned draws; /* this frame */
    uint32_t gen;   /* upload generation of its first page when it was last drawn to */
    int stale;      /* a pass that should have filled it was dropped: its contents are not what the game expects */
    unsigned last;  /* frame it was last asked for */
} GsTarget;

/* One recorded primitive. Everything inside is GS state or a handle a back end made (`tex`, `pipeline`); a
   back end replays the list by rebuilding its passes around the current target and calling the pipeline. */
extern float gsZMax; /* gs_draw.c */
/* gs_draw.c: the depth buffer's largest value for a draw's per-pixel depth (gs.frag Params.extra.x): the vertex program's
   (its constant block) or, for the GS's own vertices, the frame's; 0 = depth with its fractions (BT3_DEPTH_FRAC). */
struct GsDraw;
float GsDraw_DepthMax(const struct GsDraw *d);

typedef struct GsDraw {
    int vu;      /* 1, 2, 3: vertex programs' vertices (gsVuVerts, uniform block at `uniform`); 0: gsVerts */
    int uniform;
    int native; /* 0: primitives; otherwise a native full-screen effect (PORT_FX_*) at this point of the frame;
                   3: a table pass (dclut.frag) with `tex` = its table and `src` = which byte indexes it */
    int src;
    uint32_t first, count;
    int target;
    GsTex tex;
    int tex_is_target;
    int sampler;
    int pipeline; /* the back end's pipeline index (GsBackend.pipeGet) */
    int32_t mode[4];
    float misc[4];
    float rect[4];  /* a frame buffer as texture: the part of it the GS would address (u0, v0, u1, v1 in its uv);
                       a texture pack's replacement drawn as 2D art: the piece's own rectangle of the sheet */
    float orig[4];  /* a texture pack's replacement: xy = the original texture's size in texels, z = its largest alpha */
    float blendc;
    int scissor[4]; /* x, y, width, height, at SCALE */
    int wall;       /* triangles of the wall at the edge of the map (gs_draw.c, Port_WallMesh): in-between pictures draw it anew */
} GsDraw;

/* The formats a texture pack's replacement can be in (matches gs_texpack.h's TexPackImage.format). */
#define GS_TEXFMT_RGBA8 0
#define GS_TEXFMT_BC1 1
#define GS_TEXFMT_BC2 2
#define GS_TEXFMT_BC3 3

/* The frame being recorded. A back end's frameEnd reads these, uploads the vertices, replays the list and
   clears them for the next frame with GsDraw_FrameReset. */
extern GsDraw *gsDraws;
extern uint32_t gsDrawCount;
extern Vtx *gsVerts;            /* MAX_VERTS of them, malloc'd in GsGpu_Init */
extern uint32_t gsVertCount;
extern float *gsVuVerts;        /* 12 floats per vertex, as the vertex programs get them */
extern uint32_t gsVuVertCount;
/* The triangles of the vertex programs' draws: three indices into gsVuVerts each. A draw's `first` and `count`
   are a range of these. (A strip's vertices are stored once; as a list of whole triangles they were stored three
   times, 11 MB a frame in a two-player fight, built here and copied to the graphics card.) */
extern uint32_t *gsVuIdx;
extern uint32_t gsVuIdxCount;
extern Vu0Uniform *gsVuUni;
extern uint32_t gsVuUniCount;
extern GsTarget gsTargets[MAX_TARGETS];
extern int gsTargetCount;
extern int gsTexCount;          /* textures in the shared decode cache */
extern GsTex gsWhite;           /* the back end's 1x1 white texture */
extern int gsPendingScale;      /* the user asked for a new SCALE: applied between two frames */
extern unsigned gsFxOff;        /* effects the user can switch off (BT3_FX_OFF, or F1..F5 while running) */
extern int gsGlowPercent;
extern int gsAnchor;            /* widescreen: which part of the screen the 2D pieces being drawn belong to */
extern int gsDepthByteIsFog;    /* the top byte of the depth page as the game last left it */
extern unsigned gsNative;       /* native effect markers seen this frame */
extern unsigned gsSkipped;      /* primitives of PS2-only passes dropped this frame */
extern int gGsMainFbp;          /* the frame buffer the game shows (gs_sony.c), -1 before the first swap */
/* created / spent since the front end last cleared them (gs_core.c's slow-frame report) */
extern unsigned gGpuNewTex, gGpuNewTexPixels, gGpuNewPipes;
extern unsigned gGpuTexReplaced; /* textures taken from a texture pack so far */
extern uint64_t gGpuTexNs, gGpuPipeNs, gGpuEndNs;

/* The colour write mask FRAME.FBMSK turns into, in the bit values every back end's masks use (R=1, G=2,
   B=4, A=8: the SDL_GPU_COLORCOMPONENT_* values, and the same bits behind GL's glColorMask). */
#define GS_CC_R 1u
#define GS_CC_G 2u
#define GS_CC_B 4u
#define GS_CC_A 8u

/* ---- the pipeline key -------------------------------------------------------------------------------------
   Everything a pipeline depends on, from GS state (gs_draw.c's pipeline_get): a back end makes one pipeline per key
   value (GsBackend.pipeGet) and turns the key back into its own state with GsPipeKey_Decode. The values are kept
   between runs (the pipelines file of the Vulkan back end), so the layout must not change:
     bits 0..8    blend (GS_KEY_BLEND_*): enabled, source factor, destination factor, which C
     bits 10..11  depth test (GsDepthTest)       bit 12  depth write
     bits 13..14  topology (GsTopology)          bits 16..19  colour write mask (GS_CC_*)
     bits 20..21  vertices (GsVertexSource)      bit 22  the full-screen table pass (dclut.frag), nothing else set
     bit 23       the fragment shader without its own depth output (gsnz.frag: gs.frag with NO_FRAG_DEPTH)
   (gs_gpu.c still decodes these bits itself; it is out of the browser port's scope.) */

/* What one side of ((A - B) * C >> 7) + D is multiplied by (the GS's ALPHA register: each of A, B, D is source,
   destination or zero). */
typedef enum { GS_BLEND_K_ZERO, GS_BLEND_K_ONE, GS_BLEND_K_C, GS_BLEND_K_MINUS_C, GS_BLEND_K_ONE_MINUS_C, GS_BLEND_K_ONE_PLUS_C } GsBlendK;
/* ALPHA.C: where C comes from. The reserved value 3 is kept as it is (so the key stays what it was) and blends
   like FIX. */
typedef enum { GS_BLEND_C_SRC_ALPHA, GS_BLEND_C_DST_ALPHA, GS_BLEND_C_FIX, GS_BLEND_C_RESERVED } GsBlendC;
typedef struct GsBlend {
    int enabled;  /* PRIM.ABE */
    GsBlendK src, dst;
    GsBlendC c;
} GsBlend;

/* TEST.ZTST (with the test off counted as ALWAYS). */
typedef enum { GS_DEPTH_NEVER, GS_DEPTH_ALWAYS, GS_DEPTH_GEQUAL, GS_DEPTH_GREATER } GsDepthTest;
typedef enum { GS_TOPOLOGY_TRIANGLES, GS_TOPOLOGY_LINES, GS_TOPOLOGY_POINTS } GsTopology;
/* Where a draw's vertices come from: the GS's own (gsVerts), or a vertex program's run as a shader (gsVuVerts). */
typedef enum { GS_VERTS_GS, GS_VERTS_PROGRAM0, GS_VERTS_PROGRAM4, GS_VERTS_PROGRAM6 } GsVertexSource;

typedef struct GsPipeState {
    GsBlend blend;
    GsDepthTest depthTest;
    int depthWrite;
    GsTopology topology;
    uint32_t writeMask;  /* GS_CC_* */
    GsVertexSource vertices;
    int tablePass;       /* the full-screen table pass: blend and write mask only, no vertices, no depth */
    int noFragDepth;     /* gs.frag without its depth output (gsnz.frag): see GsPipeState_FragDepthRedundant */
} GsPipeState;

/* gs_pipekey.c. FromGs: the state of a draw from the GS registers of its context (ALPHA, PRIM.ABE, TEST, ZBUF, the
   FBMSK half of FRAME); tablePass: the full-screen table pass, for which only blending and the write mask count. */
GsPipeState GsPipeState_FromGs(uint64_t alpha, int abe, uint64_t test, uint64_t zbuf, uint32_t fbmsk, GsTopology topology,
                               GsVertexSource vertices, int tablePass);
/* 1 = the shader's own depth output (whole numbers per pixel, the end of gs.frag) changes nothing for a draw in this
   state, so it can use the variant without (it lets the graphics chip leave out hidden pixels early). */
int GsPipeState_FragDepthRedundant(const GsPipeState *s);
uint32_t GsPipeKey_Encode(const GsPipeState *s);
void GsPipeKey_Decode(uint32_t key, GsPipeState *s);

/* The blend a GPU can do for a GsBlend: dst' = op(src * srcFactor, dst * dstFactor), factors from C. -C is the
   subtracting operation; 1 + C is approximated by 1 (blend factors do not go above one). The alpha channel is not
   blended: the frame buffer keeps the source alpha. Shared so that every back end blends the same way and only maps
   these values to its own API. */
typedef enum { GS_FACTOR_ZERO, GS_FACTOR_ONE, GS_FACTOR_C, GS_FACTOR_ONE_MINUS_C } GsBlendFactor;
typedef enum { GS_OP_ADD, GS_OP_SUBTRACT /* src - dst */, GS_OP_REVERSE_SUBTRACT /* dst - src */ } GsBlendOp;
typedef struct GsBlendDesc {
    GsBlendFactor srcFactor, dstFactor;
    GsBlendOp op;
    GsBlendC c;
} GsBlendDesc;
GsBlendDesc GsBlend_Describe(GsBlend b); /* (meaningful when b.enabled) */

/* The interface a back end implements (gs_gpu.c today, gs_gl.c in time). All GPU state stays the back
   end's: the shared layer only hands it handles and asks for the few numbers it must know (whether it can
   still take a texture this frame, a target's attachments). */
typedef struct GsBackend {
    const char *name;                     /* "vulkan", "opengl" */
    int (*init)(void);                    /* window + device + fixed pipelines + white texture; 0 = failed */
    void (*frameEnd)(void);               /* events, uploads, replay the list, present, UI, screenshots */
    void (*scaleChanged)(void);           /* gsScale just changed: wait idle, drop and rebuild what is sized */
    GsTex (*whiteTex)(void);              /* the 1x1 white texture made in init */
    GsTex (*texCreate)(uint32_t w, uint32_t h, int format, int levels); /* 0 = at capacity / unsupported */
    int (*texUpload)(GsTex tex, int level, uint32_t w, uint32_t h, int format, const void *px, uint32_t bytes); /* takes ownership of px; 0 = dropped */
    void (*texDestroy)(GsTex tex);
    int (*texSlotsLeft)(void);            /* how many more textures it can take this frame */
    void (*targetEnsure)(int i);          /* create the attachments of shared target i */
    void (*targetDrop)(int i);            /* release them */
    GsTex (*targetColor)(int i);
    GsTex (*targetAux)(int i);
    GsTex (*targetDepth)(int i);
    int (*pipeGet)(uint32_t key);         /* the back end's pipeline for a key, creating it if new */
    int (*targetRead)(int i, uint8_t *rgba); /* target i's picture as 512 x 448 RGBA, top row first; 0 = not read */
    void (*targetCopy)(int src, int dst);    /* the colour of target src into target dst, as it is now */
} GsBackend;

extern GsBackend sVulkanBackend; /* gs_gpu.c */
extern GsBackend sGlBackend;     /* gs_gl.c */
#ifdef __EMSCRIPTEN__
extern GsBackend sWebGpuBackend; /* gs_wgpu.c (the browser build only) */
#endif

/* gs_draw.c: the shared target table's entry for a frame-buffer address (create: one is made if absent,
   with the back end's attachments through GsBackend.targetEnsure). */
int GsDraw_TargetGet(uint32_t fbp, int create);
int GsDraw_DateRegion(uint32_t k, int *x, int *y, int *w, int *h); /* what the snapshot at draw k has to cover (0: nothing) */

void GsGpu_TitlePoll(void); /* gs_draw.c */
void GsDraw_FrameReset(void); /* gs_draw.c: the frame was replayed: an empty list for the next one */
/* gs_draw.c: the picture uploaded into display buffer `buffer` (GS_NATIVE_FB_UPLOAD), GS_W x GS_H RGBA from GS memory,
   opaque, top row first, into px (GS_W * GS_H words). */
void GsDraw_DisplayBufferPixels(int buffer, uint32_t *px);
/* gs_draw.c: a target's picture (GsBackend.targetRead) at the GS's own resolution: px is the display part of the target
   (GS_DISPLAY_W x GS_DISPLAY_H at SCALE, RGBA, top row first), rgba gets the middle sample of each GS pixel. */
void GsDraw_DisplayReduce(const uint8_t *px, uint8_t *rgba);
/* gs_draw.c: the screenshots (BT3_SHOT=<every n frames>, BT3_SHOT_FROM / BT3_SHOT_TO, BT3_SHOT_AUX for the alpha byte
   instead of the colour). Due: this frame is one to take. Write: the shown target's picture (w x h, top row first; RGBA,
   or one byte per pixel when aux) to port/build/shots/gpu_<frame>.ppm. */
int GsDraw_ShotDue(void);
int GsDraw_ShotAux(void);
void GsDraw_ShotWrite(const uint8_t *px, int w, int h, int aux);

#endif
