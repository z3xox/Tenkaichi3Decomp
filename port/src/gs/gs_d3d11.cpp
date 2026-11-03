/*
 * Graphics Synthesizer, Direct3D 11 back end (Windows; feature level 10_0 and up, so also Fermi / Intel HD 4000).
 *
 * gs_draw.c records the frame (every primitive as a GsDraw, the vertices, the decoded textures) and installs this
 * file as sD3d11Backend. It owns the Direct3D objects (shaders written in HLSL 4.0 from the same sources as the
 * GLSL ones in shaders/, vertex buffers, textures, sampler states, three textures per GS frame-buffer address) and
 * replays the list, exactly as gs_gl.c does for OpenGL. Same conventions as the Vulkan and OpenGL back ends: a
 * target's row 0 is the top of the picture (clip space +Y is the top in Direct3D too, so no flip is needed), depth
 * runs 0..1 and is compared "greater or equal", an 8-bit "aux" attachment carries the GS alpha byte.
 *
 * Nothing is linked: d3d11.dll, dxgi.dll and d3dcompiler_47.dll are loaded when the back end starts, so a machine
 * without them (or a Linux build, which gets the stub at the end) only fails to start this back end.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
extern "C" {
#include "gs_internal.h"
#include "gs_draw.h"
#include "ui.h"
extern int Port_AspectMilli(void);
extern int Port_PageAspectMilli(void); /* plat_stub.c: the shape of the page shown now */
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingsWrite(void);
extern void PortA11y_Event(const SDL_Event *ev); /* a11y.c: the repeat key and button */
extern int gsInterp;
int Ui_InitD3D11(SDL_Window *window, void *device, void *context, const char *gpu_name); /* ui.cpp */
void GsD3d11_Vsync(int on);
void *GsD3d11_StripTexture(const void *rgba, int w, int h);
void GsD3d11_StripTextureFree(void *srv);
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>

/* ---- HLSL (4.0): the shaders of shaders/ (vert, frag), written again for Direct3D ------------------------- */
/* Conventions: the constant buffer U (b0) is Vu0Uniform (std140 = C layout, matrices column-major as in the C
   struct); Params (b1) is the draw's state; textures t0..t2 with the sampler s0. The names that GLSL could use
   but HLSL reserves (step, sample, pass, target, table) are renamed. */

static const char kHlslGsVs[] = R"HLSL(
struct VOut { float4 pos : SV_Position; float4 col : COLOR0; float3 stq : TEXCOORD0; };
VOut main(float3 inPos : POSITION, float4 inColor : COLOR0, float3 inStq : TEXCOORD0) {
    VOut o;
    o.pos = float4(inPos.x / 512.0 - 1.0, 1.0 - inPos.y / 512.0, inPos.z, 1.0);
    o.col = inColor;
    o.stq = inStq;
    return o;
}
)HLSL";

/* The vertex programs' common block (the same memory for all three; vu6 names some of it differently). */
#define HLSL_U_BLOCK \
    "cbuffer U : register(b0) {\n" \
    "    column_major float4x4 boneA;\n" \
    "    column_major float4x4 boneB;\n" \
    "    float4 pivotA; float4 pivotB;\n" \
    "    column_major float4x4 screen;\n" \
    "    float4 light; float4 color0; float4 color1; float4 misc; float4 light2; float4 tgt;\n" \
    "};\n" \
    "struct VOut { float4 pos : SV_Position; float4 col : COLOR0; float3 stq : TEXCOORD0; };\n"

static const char kHlslVu0Vs[] = HLSL_U_BLOCK R"HLSL(
VOut main(float4 inPos : TEXCOORD0, float4 inNormal : TEXCOORD1, float4 inSt : TEXCOORD2) {
    VOut o;
    float w = misc.w == 4.0 ? 1.0 : inPos.w;
    float3 pa = mul(boneA, float4(inPos.xyz - pivotA.xyz, 1.0)).xyz;
    float3 pb = mul(boneB, float4(inPos.xyz - pivotB.xyz, 1.0)).xyz;
    float4 s = mul(screen, float4(lerp(pb, pa, w), 1.0));
    if (misc.w == 2.0 && s.w < 0.0) s = -s;
    float tx = tgt.x > 0.0 ? 512.0 * tgt.x : 512.0;
    float4 gp = float4((s.x - misc.x * s.w) / tx - s.w, s.w - (s.y - misc.y * s.w) / tx, s.z / abs(misc.z), s.w);
    if (s.w > 0.0 && misc.z > 0.0) gp.z = (floor(s.z / s.w) + 0.5) / misc.z * s.w;
    if (misc.w == 2.0) gp.z = clamp(s.z / abs(misc.z), 0.0, s.w);
    o.pos = gp;
    if (misc.w == 5.0) {
        float4 n1 = float4(inNormal.xyz, 1.0);
        o.col = color1 / 255.0;
        o.stq = float3(dot(light, n1) * 0.5 + 0.5, dot(light2, n1) * 0.5 + 0.5, 1.0);
    } else if (misc.w >= 3.0) {
        float aa = misc.w == 4.0 ? color0.a : inNormal.a;
        o.col = clamp(floor(float4(inNormal.rgb, aa)), 0.0, 255.0) / 255.0;
        o.stq = float3(inSt.xy, 1.0);
    } else if (misc.w != 1.0) {
        o.col = color0 / 255.0;
        o.stq = float3(inSt.xy, 1.0);
    } else {
        float na = dot(light.xyz, mul((float3x3)boneA, inNormal.xyz));
        float nb = dot(light.xyz, mul((float3x3)boneB, inNormal.xyz));
        o.col = color1 / 255.0;
        o.stq = float3((lerp(nb, na, w) + light.w) * 0.5 + 0.5, 0.0, 1.0);
    }
    return o;
}
)HLSL";

static const char kHlslVu4Vs[] = HLSL_U_BLOCK R"HLSL(
VOut main(float4 inPos : TEXCOORD0, float4 inColor : TEXCOORD1, float4 inSt : TEXCOORD2) {
    VOut o;
    float4 s = mul(screen, inPos);
    float tx = tgt.x > 0.0 ? 512.0 * tgt.x : 512.0;
    float4 gp = float4((s.x - misc.x * s.w) / tx - s.w, s.w - (s.y - misc.y * s.w) / tx, s.z / abs(misc.z), s.w);
    if (s.w > 0.0 && misc.z > 0.0) gp.z = (floor(s.z / s.w) + 0.5) / misc.z * s.w;
    o.pos = gp;
    o.col = clamp(floor(inColor), 0.0, 255.0) / 255.0;
    o.stq = float3(inSt.xy, 1.0);
    return o;
}
)HLSL";

static const char kHlslVu6Vs[] = HLSL_U_BLOCK R"HLSL(
VOut main(float4 inPos : TEXCOORD0, float4 inColor : TEXCOORD1, float4 inUnused : TEXCOORD2) {
    VOut o;
    /* shadowCam = boneA, scale = pivotA, texScale = pivotB (the same memory under other names) */
    float4 p = float4(inPos.xyz, 1.0);
    float4 s = mul(screen, p);
    float2 st = (mul(boneA, p).xy * pivotA.x + 1.0) * 0.5;
    float tx = tgt.x > 0.0 ? 512.0 * tgt.x : 512.0;
    float4 gp = float4((s.x - misc.x * s.w) / tx - s.w, s.w - (s.y - misc.y * s.w) / tx, s.z / abs(misc.z), s.w);
    if (s.w > 0.0 && misc.z > 0.0) gp.z = (floor(s.z / s.w) + 0.5) / misc.z * s.w;
    o.pos = gp;
    o.col = clamp(floor(inColor), 0.0, 255.0) / 255.0;
    o.stq = float3(st * pivotB.xy, 1.0);
    return o;
}
)HLSL";

/* One triangle covering the target; uv in render-target space (fx.vert). */
static const char kHlslFxVs[] = R"HLSL(
struct FxOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
FxOut main(uint vid : SV_VertexID) {
    FxOut o;
    float2 p = float2((float)((vid << 1) & 2), (float)(vid & 2));
    o.uv = float2(p.x, 1.0 - p.y);
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    return o;
}
)HLSL";

/* The present (and the copy of a frame buffer the game uploaded): one textured triangle. */
static const char kHlslPresentVs[] = R"HLSL(
struct FxOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
FxOut main(uint vid : SV_VertexID) {
    FxOut o;
    float2 p = float2(vid == 1 ? 3.0 : -1.0, vid == 2 ? 3.0 : -1.0);
    o.uv = float2((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
    o.pos = float4(p, 0.0, 1.0);
    return o;
}
)HLSL";

static const char kHlslPresentPs[] = R"HLSL(
Texture2D tex : register(t0);
SamplerState smp : register(s0);
cbuffer Params : register(b1) { float4 pscale; };
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    return tex.SampleLevel(smp, uv * pscale.xy, 0);
}
)HLSL";

/* gs.frag. NO_FRAG_DEPTH: gsnz.frag; GS_BLOCK: gswb.frag (see port/tools/undefined.py). */
static const char kHlslGsPs[] = R"HLSL(
cbuffer Params : register(b1) {
    int4 pmode;   // x: textured (1 GS memory, 2 a frame buffer, 3 a texture pack's replacement), y: TFX, z: TCC, w: alpha test
    float4 pmisc; // x: AREF, y: destination alpha test, z: FBA, w: 2D sprite scale
    float4 prect;
    float4 porig;
    float4 pextra; // x: depth maximum, y: colour scaled by alpha here
};
Texture2D tex : register(t0);
Texture2D dateTex : register(t1);
SamplerState smp : register(s0);

struct PSOut {
    float4 c0 : SV_Target0;
    float4 c1 : SV_Target1;
#ifndef NO_FRAG_DEPTH
    float d : SV_Depth;
#endif
};

float2 step2(float2 v) { return float2(v.x >= 0.0 ? 1.0 : 0.0, v.y >= 0.0 ? 1.0 : 0.0); }

#ifdef GS_BLOCK
float4 gs_block(float2 c, float2 g) {
    c = clamp(c, prect.xy, prect.zw);
    if (porig.x < 2.5) return tex.Sample(smp, c);
    float2 q = 0.25 / g;
    return 0.25 * (tex.Sample(smp, c + float2(-q.x, -q.y)) + tex.Sample(smp, c + float2(q.x, -q.y)) +
                   tex.Sample(smp, c + float2(-q.x, q.y)) + tex.Sample(smp, c + float2(q.x, q.y)));
}
float4 gs_bilinear(float2 uv) {
    uint tw, th;
    tex.GetDimensions(tw, th);
    float2 g = float2(tw, th) / porig.x;
    float2 at = uv * g - 0.5, i = floor(at), f = at - i;
    float2 c = (i + 0.5) / g, d = 1.0 / g;
    return lerp(lerp(gs_block(c, g), gs_block(c + float2(d.x, 0.0), g), f.x),
                lerp(gs_block(c + float2(0.0, d.y), g), gs_block(c + d, g), f.x), f.y);
}
#endif

PSOut main(float4 fragPos : SV_Position, float4 vColor : COLOR0, float3 vStq : TEXCOORD0) {
    PSOut res;
    float k = 255.0 / 128.0;
    if (pmisc.y != 0.0) {
        bool bset = dateTex.Load(int3(int2(fragPos.xy), 0)).r >= 127.5 / 255.0;
        if (bset != (pmisc.y > 1.5)) discard;
    }
    float3 rgb = vColor.rgb;
    float a = vColor.a * k;
    if (pmode.x != 0) {
        uint tw, th;
        tex.GetDimensions(tw, th);
        float2 texsize = float2(tw, th);
        float2 uv = vStq.xy / vStq.z;
        if (pmisc.w != 0.0) {
            float s = abs(pmisc.w);
            float2 f = fragPos.xy, g = floor(f / s) * s;
            float2 dx = ddx(uv), dy = ddy(uv);
            float2 at = uv - (dx * (f.x - g.x) + dy * (f.y - g.y));
            if (pmisc.w > 0.0) {
                uv = at;
            } else if (pmode.x != 3 || porig.w != 0.0) {
                float2 inset = 0.5 / texsize;
                uv = clamp(uv, prect.xy + inset, prect.zw - inset);
            } else {
                float2 pl = (f - g) / s, stp = float2(dx.x, dy.y) * s * porig.xy;
                float2 snapped = (floor(at * porig.xy + sign(stp) / 64.0) + lerp(1.0 - pl, pl, step2(stp))) / porig.xy;
                float2 carried = at + (pl - 0.5) * float2(dx.x, dy.y) * s;
                bool2 one = abs(abs(stp) - 1.0) < 0.02;
                uv = float2(one.x ? snapped.x : carried.x, one.y ? snapped.y : carried.y);
                float2 inset = 0.5 / texsize;
                uv = clamp(uv, prect.xy + inset, prect.zw - inset);
            }
        }
        if (pmode.x == 2) uv = clamp(uv, prect.xy, prect.zw);
        float4 t;
#ifdef GS_BLOCK
        if (pmode.x == 2 && porig.x > 1.5) t = gs_bilinear(uv); else t = tex.Sample(smp, uv);
#else
        t = tex.Sample(smp, uv);
#endif
        if (pmode.x == 1 || pmode.x == 3) t.a *= k;
        if (pmode.x == 3) {
            if (porig.z < 1.0) t.a = min(t.a, porig.z);
            else if (t.a > 0.93 && t.a < 1.0) t.a = 1.0;
        }
        if (pmode.y == 0) {
            rgb = t.rgb * vColor.rgb * k;
            if (pmode.z != 0) a = t.a * a;
        } else if (pmode.y == 1) {
            rgb = t.rgb;
            if (pmode.z != 0) a = t.a;
        } else {
            rgb = t.rgb * vColor.rgb * k + float3(vColor.a, vColor.a, vColor.a);
            if (pmode.z != 0) a = (pmode.y == 2) ? t.a + a : t.a;
        }
    }
    if (pmode.w != 0) {
        float ag = floor(a * 128.0 + 0.5);
        int fn = pmode.w - 1;
        float aref = pmode.x == 3 ? pmisc.x * 0.5 : pmisc.x;
        float tol = pmode.x == 3 ? max(aref, 0.5) : 0.5;
        bool ok;
        if (fn == 0) ok = false;
        else if (fn == 1) ok = true;
        else if (fn == 2) ok = ag < aref;
        else if (fn == 3) ok = ag <= aref;
        else if (fn == 4) ok = abs(ag - pmisc.x) < tol;
        else if (fn == 5) ok = ag >= aref;
        else if (fn == 6) ok = ag > aref;
        else ok = abs(ag - pmisc.x) >= tol;
        if (!ok) discard;
    }
#ifndef NO_FRAG_DEPTH
    if (pextra.x > 0.0 && pextra.x < 2.0e7) {
        float zn = floor(fragPos.z * pextra.x), zq = zn / pextra.x;
        res.d = zq > fragPos.z ? (zn - 1.0) / pextra.x : zq;
    } else {
        res.d = fragPos.z;
    }
#endif
    if (pextra.y != 0.0) rgb *= a;
    res.c0 = float4(clamp(rgb, 0.0, 1.0), clamp(a, 0.0, 1.0));
    float ab = clamp(a * 128.0 / 255.0, 0.0, 1.0);
    if (pmisc.z != 0.0) ab = float(int(ab * 255.0 + 0.5) | 128) / 255.0;
    res.c1 = float4(ab, 0.0, 0.0, 1.0);
    return res;
}
)HLSL";

static const char kHlslOutlinePs[] = R"HLSL(
Texture2D aux : register(t0);
SamplerState smp : register(s0);
cbuffer Params : register(b1) { float4 ostep; };
float lut(float2 uv) {
    int id = int(aux.SampleLevel(smp, uv, 0).r * 255.0 + 0.5);
    if (id == 255) return 0.0;
    int c = (id << 3) & 0xFF;
    return float(c == 0 ? 0x80 : c);
}
float4 main(float4 pos : SV_Position, float2 vUv : TEXCOORD0) : SV_Target0 {
    uint tw, th;
    aux.GetDimensions(tw, th);
    float tx = 1.0 / float(tw), width = ostep.x / tx;
    float here = lut(vUv);
    if (here == 0.0 && ostep.w == 0.0) discard;
    float down = lut(vUv + float2(0.0, ostep.y)), up = lut(vUv - float2(0.0, ostep.y));
    float amount = (here > down || here > up) ? 1.0 : 0.0;
    [loop] for (int k = 1; k <= 8 && float(k) < width + 1.0; k++) {
        if (here > lut(vUv + float2(float(k) * tx, 0.0)) || here > lut(vUv - float2(float(k) * tx, 0.0))) {
            amount = max(amount, clamp(width - float(k) + 1.0, 0.0, 1.0));
            break;
        }
    }
    if (amount == 0.0 && ostep.w == 0.0) discard;
    if (ostep.w != 0.0) {
        float id = aux.SampleLevel(smp, vUv, 0).r * 255.0;
        return float4(id > 254.5 ? float3(0.0, 0.0, 0.0) : float3(0.3 + id / 16.0, id == 0.0 ? 0.6 : 0.0, 0.0), 0.0);
    }
    return float4(ostep.z * amount, ostep.z * amount, ostep.z * amount, 0.0);
}
)HLSL";

static const char kHlslDclutPs[] = R"HLSL(
Texture2D depthTex : register(t0);
Texture2D auxTex : register(t1);
Texture2D clutTex : register(t2);
SamplerState smp : register(s0);
cbuffer Params : register(b1) { float4 dp; };
struct DOut { float4 c0 : SV_Target0; float4 c1 : SV_Target1; };
DOut main(float4 pos : SV_Position, float2 vUv : TEXCOORD0) {
    int idx;
    if (dp.x < 0.5) {
        float z = depthTex.SampleLevel(smp, vUv, 0).r * dp.y;
        idx = z >= 65448.0 ? 0 : clamp(255 - int(z / 256.0), 0, 255);
    } else {
        idx = int(auxTex.SampleLevel(smp, vUv, 0).r * 255.0 + 0.5);
    }
    float4 c = clutTex.Load(int3(idx, 0, 0));
    DOut r;
    r.c0 = float4(c.rgb, clamp(c.a * 255.0 / 128.0, 0.0, 1.0));
    r.c1 = float4(c.a, 0.0, 0.0, 1.0);
    return r;
}
)HLSL";

static const char kHlslAlphakeyPs[] = R"HLSL(
Texture2D aux : register(t0);
SamplerState smp : register(s0);
static const float3 kTable[11] = {
    float3(0x3F, 0x3F, 0xFF), float3(0x80, 0x00, 0xFF), float3(0xFF, 0xFF, 0x20), float3(0xFF, 0x00, 0x00),
    float3(0x00, 0xFF, 0x00), float3(0x54, 0xFD, 0xFF), float3(0xFF, 0x00, 0xFF), float3(0x40, 0x00, 0xFF),
    float3(0xFF, 0xFF, 0x20), float3(0xFF, 0xFF, 0x2A), float3(0xFF, 0xFF, 0x34) };
float4 main(float4 pos : SV_Position, float2 vUv : TEXCOORD0) : SV_Target0 {
    int id = int(aux.SampleLevel(smp, vUv, 0).r * 255.0 + 0.5);
    if (id < 0xF4 || id == 0xFF) discard;
    return float4(kTable[id - 0xF4] / 255.0, float(0x30) / 128.0);
}
)HLSL";

/* ---- Direct3D entry points (loaded at run time) ------------------------------------------------------- */
typedef HRESULT(WINAPI *PFN_CreateDXGIFactory1_)(REFIID, void **);
typedef HRESULT(WINAPI *PFN_D3DCompile_)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR,
                                         UINT, UINT, ID3DBlob **, ID3DBlob **);

template <class T> static void rel(T *&p) {
    if (p != NULL) {
        p->Release();
        p = NULL;
    }
}

/* A texture handle (GsTex): the resource and its view. The targets' are static, so a draw recorded before a
   target is dropped still points at something valid (its view is then null: the white texture is used). */
struct Tex {
    ID3D11Texture2D *res;
    ID3D11ShaderResourceView *srv;
};

typedef struct Pipe {
    uint32_t key, bkey, wmask;
    int ztst, zwrite, topo, vu, fx, nz, wb, shadow;
    ID3D11BlendState *bs;
    ID3D11DepthStencilState *ds;
} Pipe;

/* ---- state -------------------------------------------------------------------------------------------- */
static HMODULE sDllD3D, sDllDxgi, sDllCompiler;
static PFN_D3DCompile_ sD3DCompile;
static SDL_Window *sWindow;
static ID3D11Device *sDev;
static ID3D11DeviceContext *sCtx;
static IDXGISwapChain *sSwap;
static ID3D11RenderTargetView *sBackRtv;
static int sBackW, sBackH, sVsync, sFlip;
static char sAdapterName[128];

static ID3D11VertexShader *sVsGs, *sVsVu[3], *sVsFx, *sVsPresent; /* sVsVu[0..2]: programs 0, 4, 6 (key vu 1..3) */
static ID3D11PixelShader *sPsGs, *sPsNz, *sPsWb, *sPsOutline, *sPsKey, *sPsDclut, *sPsPresent;
static ID3D11InputLayout *sLayoutGs, *sLayoutVu;
static ID3D11Buffer *sVboGs, *sVboVu, *sIboVu, *sCbU, *sCbParams;
static ID3D11SamplerState *sSamplers[16];
static ID3D11RasterizerState *sRast, *sRastNoScissor;
static ID3D11BlendState *sBsOff, *sBsKey, *sBsOutline;
static ID3D11DepthStencilState *sDsOff;

static Tex sWhite;
static Tex sTgCol[MAX_TARGETS], sTgAux[MAX_TARGETS], sTgDep[MAX_TARGETS];
static ID3D11RenderTargetView *sTgColRtv[MAX_TARGETS], *sTgAuxRtv[MAX_TARGETS];
static ID3D11DepthStencilView *sTgDsv[MAX_TARGETS];
static Tex sDateCopy, sAuxCopy; /* R8 copies of a target's alpha bytes (DATE test, outline) */
static Tex sFbTex[2];           /* the frame buffers the game uploaded (gs_draw.c native 100 / 101) */
static ID3D11Texture2D *sStaging;
static int sStagingW, sStagingH;
static Tex *sDead[4096]; /* textures destroyed this frame: released after the replay, which may still name them */
static int sDeadCount;

static Pipe sPipes[1024];
static int sPipeCount;

static int sCurPipe = -1, sCurBound = -1, sCurDepth, sCurIa = -1, sScissorKnown;
static int sLastScissor[4];
static float sBlendc = -1.0f;
static ID3D11BlendState *sCurBs;

static void target_drop(int i);

/* ---- shaders ------------------------------------------------------------------------------------------ */
static ID3DBlob *compile_hlsl(const char *src, const char *target, const D3D_SHADER_MACRO *defs, const char *tag) {
    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr = sD3DCompile(src, strlen(src), tag, defs, NULL, "main", target, 0, 0, &code, &err);
    if (FAILED(hr) || code == NULL) {
        fprintf(stderr, "bt3: d3d11: %s (%s) did not compile:\n%s\n", tag, target,
                err != NULL ? (const char *)err->GetBufferPointer() : "(no message)");
        rel(err);
        rel(code);
        return NULL;
    }
    rel(err);
    return code;
}

static ID3D11VertexShader *make_vs(const char *src, const char *tag, ID3DBlob **keep) {
    ID3D11VertexShader *vs = NULL;
    ID3DBlob *b = compile_hlsl(src, "vs_4_0", NULL, tag);
    if (b == NULL) {
        return NULL;
    }
    if (FAILED(sDev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &vs))) {
        fprintf(stderr, "bt3: d3d11: CreateVertexShader %s failed\n", tag);
        vs = NULL;
    }
    if (keep != NULL) {
        *keep = b;
    } else {
        b->Release();
    }
    return vs;
}

static ID3D11PixelShader *make_ps(const char *src, const char *tag, const D3D_SHADER_MACRO *defs) {
    ID3D11PixelShader *ps = NULL;
    ID3DBlob *b = compile_hlsl(src, "ps_4_0", defs, tag);
    if (b == NULL) {
        return NULL;
    }
    if (FAILED(sDev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &ps))) {
        fprintf(stderr, "bt3: d3d11: CreatePixelShader %s failed\n", tag);
        ps = NULL;
    }
    b->Release();
    return ps;
}

/* ---- textures ----------------------------------------------------------------------------------------- */
static int make_texture(Tex *t, UINT w, UINT h, UINT mips, DXGI_FORMAT fmt, UINT bind, DXGI_FORMAT srvFmt, const void *init, UINT pitch) {
    D3D11_TEXTURE2D_DESC d;
    D3D11_SUBRESOURCE_DATA sd, *psd = NULL;
    HRESULT hr;
    memset(&d, 0, sizeof(d));
    d.Width = w;
    d.Height = h;
    d.MipLevels = mips;
    d.ArraySize = 1;
    d.Format = fmt;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = bind;
    if (init != NULL) {
        sd.pSysMem = init;
        sd.SysMemPitch = pitch;
        sd.SysMemSlicePitch = 0;
        psd = &sd;
    }
    t->res = NULL;
    t->srv = NULL;
    hr = sDev->CreateTexture2D(&d, psd, &t->res);
    if (FAILED(hr)) {
        t->res = NULL;
        return 0;
    }
    if (bind & D3D11_BIND_SHADER_RESOURCE) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sv;
        memset(&sv, 0, sizeof(sv));
        sv.Format = srvFmt;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MostDetailedMip = 0;
        sv.Texture2D.MipLevels = mips;
        if (FAILED(sDev->CreateShaderResourceView(t->res, &sv, &t->srv))) {
            rel(t->res);
            return 0;
        }
    }
    return 1;
}

static void free_texture(Tex *t) {
    rel(t->srv);
    rel(t->res);
}

static DXGI_FORMAT tex_format(int format) {
    return format == GS_TEXFMT_RGBA8 ? DXGI_FORMAT_R8G8B8A8_UNORM : format == GS_TEXFMT_BC1 ? DXGI_FORMAT_BC1_UNORM :
           format == GS_TEXFMT_BC2 ? DXGI_FORMAT_BC2_UNORM : DXGI_FORMAT_BC3_UNORM;
}

static GsTex tex_create(uint32_t w, uint32_t h, int format, int levels) {
    Tex *t = (Tex *)calloc(1, sizeof(Tex));
    if (t == NULL) {
        return 0;
    }
    if (!make_texture(t, w, h, levels > 0 ? (UINT)levels : 1, tex_format(format), D3D11_BIND_SHADER_RESOURCE, tex_format(format), NULL, 0)) {
        free(t);
        return 0;
    }
    return (GsTex)t;
}

/* Takes ownership of px. */
static int tex_upload(GsTex handle, int level, uint32_t w, uint32_t h, int format, const void *px, uint32_t bytes) {
    Tex *t = (Tex *)handle;
    UINT pitch;
    (void)bytes;
    if (t == NULL || t->res == NULL) {
        free((void *)px);
        return 0;
    }
    if (format == GS_TEXFMT_RGBA8) {
        pitch = w * 4;
    } else {
        pitch = (w + 3) / 4 * (format == GS_TEXFMT_BC1 ? 8 : 16);
    }
    sCtx->UpdateSubresource(t->res, (UINT)level, NULL, px, pitch, 0);
    free((void *)px);
    return 1;
}

static void tex_destroy(GsTex handle) {
    Tex *t = (Tex *)handle;
    if (t == NULL) {
        return;
    }
    if (sDeadCount == (int)(sizeof(sDead) / sizeof(sDead[0]))) { /* (not seen in practice) */
        free_texture(t);
        free(t);
        return;
    }
    sDead[sDeadCount++] = t;
}

static void flush_dead(void) {
    int i;
    for (i = 0; i < sDeadCount; i++) {
        free_texture(sDead[i]);
        free(sDead[i]);
    }
    sDeadCount = 0;
}

static int tex_slots_left(void) { return 1 << 20; }
static GsTex white_tex(void) { return (GsTex)&sWhite; }

/* The view a draw samples: its texture, or the white one if it has none (or it is the target being drawn to: the
   runtime would silently unbind the target, and the other back ends read garbage there). */
static ID3D11ShaderResourceView *srv_of(GsTex h, int cur) {
    Tex *t = (Tex *)h;
    if (t == NULL || t->srv == NULL) {
        return sWhite.srv;
    }
    if (cur >= 0 && (t == &sTgCol[cur] || t == &sTgAux[cur])) {
        return sWhite.srv;
    }
    return t->srv;
}

/* ---- the attachments of the shared targets ------------------------------------------------------------ */
static void target_ensure(int i) {
    UINT w = (UINT)(GS_W * SCALE), h = (UINT)(GS_H * SCALE);
    D3D11_DEPTH_STENCIL_VIEW_DESC dv;
    if (!make_texture(&sTgCol[i], w, h, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                      DXGI_FORMAT_R8G8B8A8_UNORM, NULL, 0) ||
        !make_texture(&sTgAux[i], w, h, 1, DXGI_FORMAT_R8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                      DXGI_FORMAT_R8_UNORM, NULL, 0) ||
        !make_texture(&sTgDep[i], w, h, 1, DXGI_FORMAT_R32G8X24_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
                      DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, NULL, 0)) {
        fprintf(stderr, "bt3: d3d11: target %d (%u x %u) could not be created (out of video memory?)\n", i, w, h);
        target_drop(i);
        return;
    }
    sDev->CreateRenderTargetView(sTgCol[i].res, NULL, &sTgColRtv[i]);
    sDev->CreateRenderTargetView(sTgAux[i].res, NULL, &sTgAuxRtv[i]);
    memset(&dv, 0, sizeof(dv));
    dv.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    sDev->CreateDepthStencilView(sTgDep[i].res, &dv, &sTgDsv[i]);
}

static void target_drop(int i) {
    if (sCurBound == i) {
        sCtx->OMSetRenderTargets(0, NULL, NULL);
        sCurBound = -1;
    }
    rel(sTgColRtv[i]);
    rel(sTgAuxRtv[i]);
    rel(sTgDsv[i]);
    free_texture(&sTgCol[i]);
    free_texture(&sTgAux[i]);
    free_texture(&sTgDep[i]);
}

static GsTex target_color(int i) { return (GsTex)&sTgCol[i]; }
static GsTex target_aux(int i) { return (GsTex)&sTgAux[i]; }
static GsTex target_depth(int i) { return (GsTex)&sTgDep[i]; }

static void copies_create(void) {
    make_texture(&sDateCopy, (UINT)(GS_W * SCALE), (UINT)(GS_H * SCALE), 1, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE,
                 DXGI_FORMAT_R8_UNORM, NULL, 0);
    make_texture(&sAuxCopy, (UINT)(GS_W * SCALE), (UINT)(GS_H * SCALE), 1, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE,
                 DXGI_FORMAT_R8_UNORM, NULL, 0);
}

static void scale_changed(void) {
    int i;
    ID3D11ShaderResourceView *none[3] = {NULL, NULL, NULL};
    sCtx->PSSetShaderResources(0, 3, none);
    sCtx->OMSetRenderTargets(0, NULL, NULL);
    sCtx->Flush();
    for (i = 0; i < gsTargetCount; i++) {
        target_drop(i);
    }
    free_texture(&sDateCopy);
    free_texture(&sAuxCopy);
    rel(sStaging);
    sStagingW = sStagingH = 0;
    copies_create();
    sCurBound = -1;
}

/* ---- pipelines (blend and depth-stencil state per key) ------------------------------------------------- */
static ID3D11BlendState *make_blend(int enabled, D3D11_BLEND src, D3D11_BLEND dst, D3D11_BLEND_OP op, uint32_t mask0, uint32_t mask1) {
    D3D11_BLEND_DESC b;
    ID3D11BlendState *bs = NULL;
    memset(&b, 0, sizeof(b));
    b.IndependentBlendEnable = TRUE;
    b.RenderTarget[0].BlendEnable = enabled ? TRUE : FALSE;
    b.RenderTarget[0].SrcBlend = src;
    b.RenderTarget[0].DestBlend = dst;
    b.RenderTarget[0].BlendOp = op;
    b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    b.RenderTarget[0].RenderTargetWriteMask = (UINT8)mask0;
    b.RenderTarget[1].BlendEnable = FALSE;
    b.RenderTarget[1].SrcBlend = D3D11_BLEND_ONE;
    b.RenderTarget[1].DestBlend = D3D11_BLEND_ZERO;
    b.RenderTarget[1].BlendOp = D3D11_BLEND_OP_ADD;
    b.RenderTarget[1].SrcBlendAlpha = D3D11_BLEND_ONE;
    b.RenderTarget[1].DestBlendAlpha = D3D11_BLEND_ZERO;
    b.RenderTarget[1].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    b.RenderTarget[1].RenderTargetWriteMask = (UINT8)mask1;
    sDev->CreateBlendState(&b, &bs);
    return bs;
}

/* The GS blend (pipeline key bits 0..9) as the blend factors, as gs_gl.c's apply_blend turns it. */
static ID3D11BlendState *blend_for(uint32_t bkey, uint32_t wmask) {
    int k[2] = {(int)((bkey >> 1) & 7), (int)((bkey >> 4) & 7)}, C = (int)((bkey >> 7) & 3), i;
    D3D11_BLEND fc = C == 0 ? D3D11_BLEND_SRC_ALPHA : C == 1 ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_BLEND_FACTOR;
    D3D11_BLEND fi = C == 0 ? D3D11_BLEND_INV_SRC_ALPHA : C == 1 ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_BLEND_FACTOR;
    D3D11_BLEND f[2];
    D3D11_BLEND_OP op;
    uint32_t aux = (wmask & GS_CC_A) ? 1u : 0u; /* the alpha byte's attachment follows the alpha channel's mask */
    if (!(bkey & 1)) {
        return make_blend(0, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, wmask, aux);
    }
    for (i = 0; i < 2; i++) {
        f[i] = k[i] == 0 ? D3D11_BLEND_ZERO : (k[i] == 1 || k[i] == 5) ? D3D11_BLEND_ONE : k[i] == 4 ? fi : fc;
    }
    if (bkey & (1u << 9)) {
        f[0] = D3D11_BLEND_ONE; /* (the shader scaled the source by its alpha: gs_draw.c, blend_key) */
    }
    op = k[0] == 3 ? D3D11_BLEND_OP_REV_SUBTRACT : k[1] == 3 ? D3D11_BLEND_OP_SUBTRACT : D3D11_BLEND_OP_ADD;
    return make_blend(1, f[0], f[1], op, wmask, aux);
}

static ID3D11DepthStencilState *depth_for(const Pipe *p) {
    D3D11_DEPTH_STENCIL_DESC d;
    ID3D11DepthStencilState *ds = NULL;
    memset(&d, 0, sizeof(d));
    if (!p->fx) {
        d.DepthEnable = TRUE;
        d.DepthWriteMask = p->zwrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        d.DepthFunc = p->ztst == 0 ? D3D11_COMPARISON_NEVER : p->ztst == 1 ? D3D11_COMPARISON_ALWAYS :
                      p->ztst == 2 ? D3D11_COMPARISON_GREATER_EQUAL : D3D11_COMPARISON_GREATER;
        if (p->shadow) { /* one bit per pixel blocks every later triangle of a flat-shadow run */
            d.StencilEnable = TRUE;
            d.StencilReadMask = 0xFF;
            d.StencilWriteMask = 0xFF;
            d.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
            d.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
            d.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
            d.FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR_SAT;
            d.BackFace = d.FrontFace;
        }
    }
    sDev->CreateDepthStencilState(&d, &ds);
    return ds;
}

static void pipe_decode(Pipe *p, uint32_t key) {
    p->key = key;
    p->bkey = key & 0x3FF;
    p->ztst = (int)((key >> 10) & 3);
    p->zwrite = (int)((key >> 12) & 1);
    p->topo = (int)((key >> 13) & 3);
    p->vu = (int)((key >> 20) & 3);
    p->nz = (int)((key >> 23) & 1);
    p->wb = (int)((key >> 25) & 1);
    p->shadow = (int)((key >> 24) & 1);
    p->fx = (int)((key >> 22) & 1);
    p->wmask = (key >> 16) & 15;
    p->bs = blend_for(p->bkey, p->wmask);
    p->ds = depth_for(p);
}

static int pipe_get(uint32_t key) {
    int i;
    for (i = 0; i < sPipeCount; i++) {
        if (sPipes[i].key == key) {
            return i;
        }
    }
    if (sPipeCount == 1024) {
        return 0;
    }
    pipe_decode(&sPipes[sPipeCount], key);
    return sPipeCount++;
}

static void set_blend(ID3D11BlendState *bs, float c) {
    float f[4] = {c, c, c, c};
    if (bs != sCurBs || c != sBlendc) {
        sCtx->OMSetBlendState(bs, f, 0xFFFFFFFFu);
        sCurBs = bs;
        sBlendc = c;
    }
}

static void set_params(const void *p, size_t n) { /* (a constant buffer is updated whole) */
    float buf[20];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, p, n < sizeof(buf) ? n : sizeof(buf));
    sCtx->UpdateSubresource(sCbParams, 0, NULL, buf, 0, 0);
}

static void set_scissor(const int *sc) {
    if (!sScissorKnown || memcmp(sc, sLastScissor, sizeof(sLastScissor)) != 0) {
        D3D11_RECT r;
        r.left = sc[0];
        r.top = sc[1];
        r.right = sc[0] + (sc[2] > 0 ? sc[2] : 0);
        r.bottom = sc[1] + (sc[3] > 0 ? sc[3] : 0);
        sCtx->RSSetScissorRects(1, &r);
        memcpy(sLastScissor, sc, sizeof(sLastScissor));
        sScissorKnown = 1;
    }
}

static void unbind_srvs(void) {
    ID3D11ShaderResourceView *none[3] = {NULL, NULL, NULL};
    sCtx->PSSetShaderResources(0, 3, none);
}

static void set_viewport(float x, float y, float w, float h) {
    D3D11_VIEWPORT v;
    v.TopLeftX = x;
    v.TopLeftY = y;
    v.Width = w;
    v.Height = h;
    v.MinDepth = 0.0f;
    v.MaxDepth = 1.0f;
    sCtx->RSSetViewports(1, &v);
}

/* Target i as the render target: colour and the alpha byte, with its depth unless a pass reads that. */
static void bind_target(int i, int clear, int withDepth) {
    ID3D11RenderTargetView *rt[2];
    if (sCurBound == i && sCurDepth == withDepth && !clear) {
        return;
    }
    rt[0] = sTgColRtv[i];
    rt[1] = sTgAuxRtv[i];
    unbind_srvs();
    sCtx->OMSetRenderTargets(2, rt, withDepth ? sTgDsv[i] : NULL);
    set_viewport(0.0f, 0.0f, (float)(GS_W * SCALE), (float)(GS_H * SCALE));
    if (clear && rt[0] != NULL) {
        /* as the other back ends clear: colour and alpha 0 (alpha 1 only with the magenta aid), the alpha byte 0 */
        float col[4] = {0.0f, 0.0f, 0.0f, 0.0f}, aux[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (getenv("BT3_GPU_MAGENTA") != NULL) { col[0] = 1.0f; col[2] = 1.0f; col[3] = 1.0f; }
        sCtx->ClearRenderTargetView(rt[0], col);
        sCtx->ClearRenderTargetView(rt[1], aux);
        if (withDepth && sTgDsv[i] != NULL) {
            sCtx->ClearDepthStencilView(sTgDsv[i], D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);
        }
    }
    sCurBound = i;
    sCurDepth = withDepth;
    sCurPipe = -1;
    sCurBs = NULL;
    sCurIa = -1;
}

static void set_pipe(int pipe) {
    Pipe *p;
    if (pipe == sCurPipe) {
        return;
    }
    sCurPipe = pipe;
    p = &sPipes[pipe];
    if (p->fx) {
        sCtx->VSSetShader(sVsFx, NULL, 0);
        sCtx->PSSetShader(sPsDclut, NULL, 0);
    } else {
        sCtx->VSSetShader(p->wb ? sVsGs : p->vu == 0 ? sVsGs : sVsVu[p->vu - 1], NULL, 0);
        sCtx->PSSetShader(p->wb ? sPsWb : p->nz ? sPsNz : sPsGs, NULL, 0);
    }
    sCtx->OMSetDepthStencilState(p->ds, 0);
    sCurBs = NULL; /* (the blend state is set by the caller together with the blend colour) */
}

static void set_native(int effect) {
    sCurPipe = -1;
    sCtx->OMSetDepthStencilState(sDsOff, 0);
    sCtx->VSSetShader(sVsFx, NULL, 0);
    sCtx->PSSetShader(effect == 2 ? sPsKey : sPsOutline, NULL, 0);
    sCtx->IASetInputLayout(NULL);
    sCurIa = -1;
    sCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    set_blend(effect == 2 ? sBsKey : sBsOutline, 0.0f);
}

static void set_ia(int vu) {
    UINT stride, off = 0;
    if (sCurIa == vu) {
        return;
    }
    sCurIa = vu;
    if (vu) {
        stride = 48;
        sCtx->IASetInputLayout(sLayoutVu);
        sCtx->IASetVertexBuffers(0, 1, &sVboVu, &stride, &off);
        sCtx->IASetIndexBuffer(sIboVu, DXGI_FORMAT_R32_UINT, 0);
    } else {
        stride = sizeof(Vtx);
        sCtx->IASetInputLayout(sLayoutGs);
        sCtx->IASetVertexBuffers(0, 1, &sVboGs, &stride, &off);
    }
}

static void set_textures(ID3D11ShaderResourceView *a, ID3D11ShaderResourceView *b, ID3D11ShaderResourceView *c, int s0) {
    ID3D11ShaderResourceView *v[3] = {a, b, c};
    ID3D11SamplerState *smp = sSamplers[s0];
    sCtx->PSSetShaderResources(0, 3, v);
    sCtx->PSSetSamplers(0, 1, &smp);
}

/* ---- init --------------------------------------------------------------------------------------------- */
static int create_swapchain(void) {
    IDXGIDevice *dd = NULL;
    IDXGIAdapter *ad = NULL;
    IDXGIFactory *fac = NULL;
    DXGI_SWAP_CHAIN_DESC sc;
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(sWindow), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    int w, h, tries;
    HRESULT hr = E_FAIL;

    if (hwnd == NULL) {
        fprintf(stderr, "bt3: d3d11: the window has no HWND\n");
        return 0;
    }
    if (FAILED(sDev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dd)) || FAILED(dd->GetAdapter(&ad)) ||
        FAILED(ad->GetParent(__uuidof(IDXGIFactory), (void **)&fac))) {
        fprintf(stderr, "bt3: d3d11: no DXGI factory\n");
        rel(ad);
        rel(dd);
        return 0;
    }
    SDL_GetWindowSizeInPixels(sWindow, &w, &h);
    w = w > 0 ? w : 1;
    h = h > 0 ? h : 1;
    for (tries = 0; tries < 2 && FAILED(hr); tries++) {
        memset(&sc, 0, sizeof(sc));
        sc.BufferDesc.Width = (UINT)w;
        sc.BufferDesc.Height = (UINT)h;
        sc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sc.SampleDesc.Count = 1;
        sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc.BufferCount = tries == 0 ? 2 : 1;
        sc.OutputWindow = hwnd;
        sc.Windowed = TRUE;
        sc.SwapEffect = tries == 0 ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD;
        hr = fac->CreateSwapChain(sDev, &sc, &sSwap);
        sFlip = tries == 0;
        if (FAILED(hr)) {
            sSwap = NULL;
        }
    }
    if (SUCCEEDED(hr)) {
        fac->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES); /* (F11 is SDL's) */
    }
    rel(fac);
    rel(ad);
    rel(dd);
    if (FAILED(hr)) {
        fprintf(stderr, "bt3: d3d11: CreateSwapChain failed (0x%08lx)\n", (unsigned long)hr);
        return 0;
    }
    sBackW = w;
    sBackH = h;
    return 1;
}

static int make_back_rtv(void) {
    ID3D11Texture2D *bb = NULL;
    rel(sBackRtv);
    if (FAILED(sSwap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb))) {
        return 0;
    }
    sDev->CreateRenderTargetView(bb, NULL, &sBackRtv);
    bb->Release();
    return sBackRtv != NULL;
}

static int d3d_init(void) {
    static const uint32_t white = 0xFFFFFFFFu;
    static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    PFN_D3D11_CREATE_DEVICE create;
    PFN_CreateDXGIFactory1_ createFactory;
    IDXGIFactory1 *fac = NULL;
    IDXGIAdapter1 *pick = NULL;
    D3D_FEATURE_LEVEL got;
    SIZE_T best = 0;
    ID3DBlob *gsBlob = NULL, *vuBlob = NULL;
    D3D11_INPUT_ELEMENT_DESC egs[3], evu[3];
    D3D11_BUFFER_DESC bd;
    D3D11_RASTERIZER_DESC rd;
    D3D11_SAMPLER_DESC sd;
    D3D11_DEPTH_STENCIL_DESC dd;
    HRESULT hr;
    int i, want = -1;
    D3D_SHADER_MACRO defNz[] = {{"NO_FRAG_DEPTH", "1"}, {NULL, NULL}};
    D3D_SHADER_MACRO defWb[] = {{"NO_FRAG_DEPTH", "1"}, {"GS_BLOCK", "1"}, {NULL, NULL}};

    sDllD3D = LoadLibraryA("d3d11.dll");
    sDllDxgi = LoadLibraryA("dxgi.dll");
    sDllCompiler = LoadLibraryA("d3dcompiler_47.dll");
    if (sDllD3D == NULL || sDllDxgi == NULL || sDllCompiler == NULL) {
        fprintf(stderr, "bt3: d3d11: %s is not available\n", sDllD3D == NULL ? "d3d11.dll" : sDllDxgi == NULL ? "dxgi.dll" : "d3dcompiler_47.dll");
        return 0;
    }
    create = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(sDllD3D, "D3D11CreateDevice");
    createFactory = (PFN_CreateDXGIFactory1_)GetProcAddress(sDllDxgi, "CreateDXGIFactory1");
    sD3DCompile = (PFN_D3DCompile_)GetProcAddress(sDllCompiler, "D3DCompile");
    if (create == NULL || createFactory == NULL || sD3DCompile == NULL) {
        fprintf(stderr, "bt3: d3d11: missing entry points\n");
        return 0;
    }

    sWindow = GsDraw_WindowCreate(0);
    if (sWindow == NULL) {
        return 0;
    }

    /* The adapter: the one with the most video memory of its own (on a laptop with two graphics chips, the fast
       one), unless BT3_D3D11_ADAPTER=<n> (the numbers of the list printed below, from 0) names another. */
    if (SUCCEEDED(createFactory(__uuidof(IDXGIFactory1), (void **)&fac))) {
        IDXGIAdapter1 *ad;
        if (getenv("BT3_D3D11_ADAPTER") != NULL) {
            want = atoi(getenv("BT3_D3D11_ADAPTER"));
        }
        for (i = 0; fac->EnumAdapters1((UINT)i, &ad) == S_OK; i++) {
            DXGI_ADAPTER_DESC1 desc;
            char name[128];
            ad->GetDesc1(&desc);
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), NULL, NULL);
            fprintf(stderr, "bt3: d3d11: adapter %d: %s, %u MB%s\n", i, name, (unsigned)(desc.DedicatedVideoMemory >> 20),
                    (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? " (software)" : "");
            if (want >= 0 ? i == want : (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (pick == NULL || desc.DedicatedVideoMemory > best))) {
                if (pick != NULL) {
                    pick->Release();
                }
                pick = ad;
                best = desc.DedicatedVideoMemory;
                snprintf(sAdapterName, sizeof(sAdapterName), "%s", name);
                continue;
            }
            ad->Release();
        }
        fac->Release();
    }
    hr = create(pick, pick != NULL ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL,
                getenv("BT3_GPU_DEBUG") != NULL ? D3D11_CREATE_DEVICE_DEBUG : 0, levels, 3, D3D11_SDK_VERSION, &sDev, &got, &sCtx);
    if (FAILED(hr) && getenv("BT3_GPU_DEBUG") != NULL) { /* (no debug layer installed) */
        hr = create(pick, pick != NULL ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, 3, D3D11_SDK_VERSION, &sDev, &got, &sCtx);
    }
    if (pick != NULL) {
        pick->Release();
    }
    if (FAILED(hr)) {
        fprintf(stderr, "bt3: d3d11: no device (0x%08lx)\n", (unsigned long)hr);
        return 0;
    }
    fprintf(stderr, "bt3: D3D11 renderer: %s (feature level %d_%d)\n", sAdapterName[0] ? sAdapterName : "default adapter",
            got >> 12, (got >> 8) & 15);
    if (!create_swapchain() || !make_back_rtv()) {
        return 0;
    }
    sVsync = Port_Setting("vsync", 0) != 0;

    /* shaders: gs / vu0 / vu4 / vu6 vertex stages, gs.frag in its three forms, the full-screen passes */
    sVsGs = make_vs(kHlslGsVs, "gs.vert", &gsBlob);
    sVsVu[0] = make_vs(kHlslVu0Vs, "vu0.vert", &vuBlob);
    sVsVu[1] = make_vs(kHlslVu4Vs, "vu4.vert", NULL);
    sVsVu[2] = make_vs(kHlslVu6Vs, "vu6.vert", NULL);
    sVsFx = make_vs(kHlslFxVs, "fx.vert", NULL);
    sVsPresent = make_vs(kHlslPresentVs, "present.vert", NULL);
    sPsGs = make_ps(kHlslGsPs, "gs.frag", NULL);
    sPsNz = make_ps(kHlslGsPs, "gsnz.frag", defNz);
    sPsWb = make_ps(kHlslGsPs, "gswb.frag", defWb);
    sPsOutline = make_ps(kHlslOutlinePs, "outline.frag", NULL);
    sPsKey = make_ps(kHlslAlphakeyPs, "alphakey.frag", NULL);
    sPsDclut = make_ps(kHlslDclutPs, "dclut.frag", NULL);
    sPsPresent = make_ps(kHlslPresentPs, "present.frag", NULL);
    if (gsBlob == NULL || vuBlob == NULL || !sVsGs || !sVsVu[0] || !sVsVu[1] || !sVsVu[2] || !sVsFx || !sVsPresent || !sPsGs ||
        !sPsNz || !sPsWb || !sPsOutline || !sPsKey || !sPsDclut || !sPsPresent) {
        rel(gsBlob);
        rel(vuBlob);
        return 0;
    }

    memset(egs, 0, sizeof(egs));
    egs[0].SemanticName = "POSITION"; egs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT; egs[0].AlignedByteOffset = 0;
    egs[1].SemanticName = "COLOR"; egs[1].Format = DXGI_FORMAT_R8G8B8A8_UNORM; egs[1].AlignedByteOffset = 12;
    egs[2].SemanticName = "TEXCOORD"; egs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT; egs[2].AlignedByteOffset = 16;
    for (i = 0; i < 3; i++) {
        egs[i].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    }
    memset(evu, 0, sizeof(evu));
    for (i = 0; i < 3; i++) {
        evu[i].SemanticName = "TEXCOORD";
        evu[i].SemanticIndex = (UINT)i;
        evu[i].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        evu[i].AlignedByteOffset = (UINT)(i * 16);
        evu[i].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    }
    hr = sDev->CreateInputLayout(egs, 3, gsBlob->GetBufferPointer(), gsBlob->GetBufferSize(), &sLayoutGs);
    if (SUCCEEDED(hr)) {
        hr = sDev->CreateInputLayout(evu, 3, vuBlob->GetBufferPointer(), vuBlob->GetBufferSize(), &sLayoutVu);
    }
    rel(gsBlob);
    rel(vuBlob);
    if (FAILED(hr)) {
        fprintf(stderr, "bt3: d3d11: CreateInputLayout failed (0x%08lx)\n", (unsigned long)hr);
        return 0;
    }

    /* the buffers: dynamic vertex / index data, two constant buffers */
    memset(&bd, 0, sizeof(bd));
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = (UINT)(MAX_VERTS * sizeof(Vtx));
    hr = sDev->CreateBuffer(&bd, NULL, &sVboGs);
    bd.ByteWidth = (UINT)(MAX_VU_VERTS * 48);
    if (SUCCEEDED(hr)) { hr = sDev->CreateBuffer(&bd, NULL, &sVboVu); }
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.ByteWidth = (UINT)(MAX_VU_IDX * sizeof(uint32_t));
    if (SUCCEEDED(hr)) { hr = sDev->CreateBuffer(&bd, NULL, &sIboVu); }
    memset(&bd, 0, sizeof(bd));
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.ByteWidth = sizeof(Vu0Uniform);
    if (SUCCEEDED(hr)) { hr = sDev->CreateBuffer(&bd, NULL, &sCbU); }
    bd.ByteWidth = 80;
    if (SUCCEEDED(hr)) { hr = sDev->CreateBuffer(&bd, NULL, &sCbParams); }
    if (FAILED(hr)) {
        fprintf(stderr, "bt3: d3d11: CreateBuffer failed (0x%08lx)\n", (unsigned long)hr);
        return 0;
    }

    /* sampler states: bit 0 linear, bit 1 / 2 clamp S / T, bit 3 a texture pack's replacement (its mip chain is used) */
    for (i = 0; i < 16; i++) {
        int lin = (i & 1) != 0, mip = (i & 8) != 0;
        memset(&sd, 0, sizeof(sd));
        sd.Filter = (D3D11_FILTER)(((mip || lin) ? 0x10 : 0) | (lin ? 0x4 : 0) | (mip ? 0x1 : 0));
        sd.AddressU = (i & 2) ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressV = (i & 4) ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MinLOD = 0.0f;
        sd.MaxLOD = mip ? D3D11_FLOAT32_MAX : 0.0f;
        sd.MaxAnisotropy = 1;
        sDev->CreateSamplerState(&sd, &sSamplers[i]);
    }

    /* rasteriser: no culling, the scissor on (the GS's scissor), depth clip off (depth is clamped, as in the others) */
    memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = FALSE;
    rd.ScissorEnable = TRUE;
    sDev->CreateRasterizerState(&rd, &sRast);
    rd.ScissorEnable = FALSE;
    sDev->CreateRasterizerState(&rd, &sRastNoScissor);
    memset(&dd, 0, sizeof(dd));
    sDev->CreateDepthStencilState(&dd, &sDsOff);
    sBsOff = make_blend(0, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, 15, 0);
    sBsKey = make_blend(1, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, 7, 0);
    sBsOutline = make_blend(1, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_REV_SUBTRACT, 7, 0);
    if (!sRast || !sRastNoScissor || !sDsOff || !sBsOff || !sBsKey || !sBsOutline) {
        fprintf(stderr, "bt3: d3d11: a state object could not be created\n");
        return 0;
    }

    if (!make_texture(&sWhite, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, DXGI_FORMAT_R8G8B8A8_UNORM, &white, 4)) {
        return 0;
    }
    copies_create();
    sCtx->VSSetConstantBuffers(0, 1, &sCbU);
    sCtx->PSSetConstantBuffers(1, 1, &sCbParams);
    sCtx->RSSetState(sRast);

    /* the overlay (F1's settings, the online window): ImGui's Direct3D 11 backend */
    if (!Ui_InitD3D11(sWindow, sDev, sCtx, sAdapterName[0] ? sAdapterName : "Direct3D 11")) {
        fprintf(stderr, "bt3: d3d11: the settings window is not available\n");
    }
    return 1;
}

/* A strip of pre-rendered names (the added stages / songs) as a texture for the overlay (ui.cpp): the view,
   which is what ImGui's Direct3D 11 backend takes as a texture id. */
void *GsD3d11_StripTexture(const void *rgba, int w, int h) {
    Tex t;
    if (sDev == NULL || !make_texture(&t, (UINT)w, (UINT)h, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE,
                                      DXGI_FORMAT_R8G8B8A8_UNORM, rgba, (UINT)w * 4)) {
        return NULL;
    }
    t.res->Release(); /* (the view keeps it) */
    return t.srv;
}

void GsD3d11_StripTextureFree(void *srv) {
    if (srv != NULL) {
        ((ID3D11ShaderResourceView *)srv)->Release();
    }
}

void GsD3d11_Vsync(int on) { /* the settings window's "V-sync" */
    sVsync = on != 0;
}

/* ---- reading a target back ---------------------------------------------------------------------------- */
/* The top-left w x h pixels of target i's colour as RGBA, rows top first. */
static int read_color(int i, int w, int h, uint8_t *dst) {
    D3D11_MAPPED_SUBRESOURCE m;
    D3D11_BOX box;
    int y;
    if (sTgCol[i].res == NULL) {
        return 0;
    }
    if (sStaging == NULL || sStagingW != w || sStagingH != h) {
        D3D11_TEXTURE2D_DESC d;
        rel(sStaging);
        memset(&d, 0, sizeof(d));
        d.Width = (UINT)w;
        d.Height = (UINT)h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(sDev->CreateTexture2D(&d, NULL, &sStaging))) {
            sStaging = NULL;
            return 0;
        }
        sStagingW = w;
        sStagingH = h;
    }
    box.left = 0; box.top = 0; box.front = 0; box.right = (UINT)w; box.bottom = (UINT)h; box.back = 1;
    sCtx->CopySubresourceRegion(sStaging, 0, 0, 0, 0, sTgCol[i].res, 0, &box);
    if (FAILED(sCtx->Map(sStaging, 0, D3D11_MAP_READ, 0, &m))) {
        return 0;
    }
    for (y = 0; y < h; y++) {
        memcpy(dst + (size_t)y * w * 4, (const uint8_t *)m.pData + (size_t)y * m.RowPitch, (size_t)w * 4);
    }
    sCtx->Unmap(sStaging, 0);
    return 1;
}

/* Target i's picture, reduced to the GS's 512 x 448 (as the other back ends'). */
static int d3d_target_read(int i, uint8_t *rgba) {
    int w = 512 * SCALE, h = 448 * SCALE, x, y;
    uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4);
    if (px == NULL) {
        return 0;
    }
    if (!read_color(i, w, h, px)) {
        free(px);
        return 0;
    }
    for (y = 0; y < 448; y++) {
        for (x = 0; x < 512; x++) {
            memcpy(rgba + (y * 512 + x) * 4, px + ((size_t)(y * SCALE + SCALE / 2) * w + x * SCALE + SCALE / 2) * 4, 4);
        }
    }
    free(px);
    return 1;
}

static void d3d_target_copy(int src, int dst) {
    if (sTgCol[src].res == NULL || sTgCol[dst].res == NULL) {
        return;
    }
    sCtx->CopyResource(sTgCol[dst].res, sTgCol[src].res);
}

/* ---- the frame ---------------------------------------------------------------------------------------- */
static void upload_dynamic(ID3D11Buffer *b, const void *data, size_t bytes) {
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(sCtx->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, data, bytes);
        sCtx->Unmap(b, 0);
    }
}

static void resize_back(void) {
    int w, h;
    SDL_GetWindowSizeInPixels(sWindow, &w, &h);
    if (w <= 0 || h <= 0 || (w == sBackW && h == sBackH)) {
        return;
    }
    sCtx->OMSetRenderTargets(0, NULL, NULL);
    sCurBound = -1;
    rel(sBackRtv);
    if (SUCCEEDED(sSwap->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0))) {
        sBackW = w;
        sBackH = h;
        make_back_rtv();
    } else {
        fprintf(stderr, "bt3: d3d11: ResizeBuffers failed\n");
        make_back_rtv();
    }
}

static void frame_end(void) {
    SDL_Event ev;
    int cur = -1, best = -1, i;
    uint32_t n;
    struct { int32_t mode[4]; float misc[4]; float rect[4]; float orig[4]; float extra[4]; } fu, lastFu;
    int haveFu = 0;

    GsGpu_TitlePoll();
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT) { exit(0); }
        PortA11y_Event(&ev);
        if (PortInput_Hotkey(&ev)) { Ui_Toggle(); continue; }
        if (Ui_Event(&ev)) { continue; }
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) { if (!ev.key.repeat && !Ui_QuitAsk()) { exit(0); } /* (asks first) */ }
        if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_F9) { /* in-between pictures on / off */
            static int was = 1;
            if (gsInterp > 0) { was = gsInterp; gsInterp = 0; } else { gsInterp = was; }
            Port_SettingSave("interp", gsInterp);
            Port_SettingsWrite();
            fprintf(stderr, "bt3: smooth motion %s\n", gsInterp == 0 ? "off" : gsInterp == 1 ? "60" : gsInterp == 2 ? "120" : "240");
        }
        if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_F11) { GsDraw_FullscreenToggle(); }
    }
    if (getenv("BT3_GS_VERBOSE") != NULL && gGsFrame % 30 == 0) {
        fprintf(stderr, "gpu: frame %u: %u draws, %u vertices, %d targets, %d textures, %d pipelines, %u primitives of PS2-only passes dropped, %u native effects\n",
                gGsFrame, gsDrawCount, gsVertCount + gsVuVertCount, gsTargetCount, gsTexCount, sPipeCount, gsSkipped, gsNative);
    }

    if (gsVertCount != 0) { upload_dynamic(sVboGs, gsVerts, (size_t)gsVertCount * sizeof(Vtx)); }
    if (gsVuVertCount != 0) { upload_dynamic(sVboVu, gsVuVerts, (size_t)gsVuVertCount * 48); }
    if (gsVuIdxCount != 0) { upload_dynamic(sIboVu, gsVuIdx, (size_t)gsVuIdxCount * sizeof(uint32_t)); }
    for (i = 0; i < 2; i++) { /* the frame buffers the game uploaded */
        uint32_t *px, x, y, fbp = i ? 0x70 : 0;
        if (!(gGsFbUploads & (1u << i))) { continue; }
        if (sFbTex[i].res == NULL &&
            !make_texture(&sFbTex[i], GS_W, GS_H, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, DXGI_FORMAT_R8G8B8A8_UNORM, NULL, 0)) {
            continue;
        }
        px = (uint32_t *)malloc((size_t)GS_W * GS_H * 4);
        for (y = 0; y < GS_H; y++) {
            for (x = 0; x < GS_W; x++) {
                px[y * GS_W + x] = Gs_VramRead(fbp * 32, 8, 0, x, y) | 0xFF000000u;
            }
        }
        sCtx->UpdateSubresource(sFbTex[i].res, 0, NULL, px, GS_W * 4, 0);
        free(px);
    }
    gGsFbUploads = 0;

    sCurPipe = -1;
    sCurBound = -1;
    sCurIa = -1;
    sCurBs = NULL;
    sBlendc = -1.0f;
    sScissorKnown = 0;
    sCtx->RSSetState(sRast);
    sCtx->VSSetConstantBuffers(0, 1, &sCbU);
    sCtx->PSSetConstantBuffers(1, 1, &sCbParams);
    sCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (n = 0; n < gsDrawCount; n++) {
        const GsDraw *d = &gsDraws[n];
        int shadowStart;
        if (d->native >= 100) { /* a frame buffer the game uploaded, stretched over the whole target */
            ID3D11RenderTargetView *rt[1];
            float params[4] = {1.0f, 1.0f, 0.0f, 0.0f};
            if (sFbTex[d->native - 100].res == NULL || sTgColRtv[d->target] == NULL) { continue; }
            unbind_srvs();
            rt[0] = sTgColRtv[d->target];
            sCtx->OMSetRenderTargets(1, rt, NULL);
            set_viewport(0.0f, 0.0f, (float)(GS_W * SCALE), (float)(GS_H * SCALE));
            sCtx->RSSetState(sRastNoScissor);
            sCtx->OMSetDepthStencilState(sDsOff, 0);
            sCtx->OMSetBlendState(sBsOff, NULL, 0xFFFFFFFFu);
            sCtx->IASetInputLayout(NULL);
            sCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            sCtx->VSSetShader(sVsPresent, NULL, 0);
            sCtx->PSSetShader(sPsPresent, NULL, 0);
            set_params(params, 16);
            set_textures(sFbTex[d->native - 100].srv, NULL, NULL, 7);
            sCtx->Draw(3, 0);
            sCtx->RSSetState(sRast);
            gsTargets[d->target].cleared = 1;
            cur = -1;
            sCurBound = -1; sCurPipe = -1; sCurIa = -1; sCurBs = NULL; sScissorKnown = 0; haveFu = 0;
            continue;
        }
        if (d->native == 5 || d->native == 4) { /* a copy of the frame's alpha ids (the outline and the DATE passes read it) */
            Tex *dst = d->native == 5 ? &sDateCopy : &sAuxCopy;
            D3D11_BOX box;
            int rx = 0, ry = 0, rw = GS_W * SCALE, rh = GS_H * SCALE;
            if (!gsTargets[d->target].cleared || sTgAux[d->target].res == NULL || dst->res == NULL) { continue; }
            if (d->native == 5 && !GsDraw_DateRegion(n, &rx, &ry, &rw, &rh)) { continue; }
            if (rx < 0) { rw += rx; rx = 0; }
            if (ry < 0) { rh += ry; ry = 0; }
            if (rx + rw > GS_W * SCALE) { rw = GS_W * SCALE - rx; }
            if (ry + rh > GS_H * SCALE) { rh = GS_H * SCALE - ry; }
            if (rw <= 0 || rh <= 0) { continue; }
            box.left = (UINT)rx; box.top = (UINT)ry; box.front = 0; box.right = (UINT)(rx + rw); box.bottom = (UINT)(ry + rh); box.back = 1;
            sCtx->CopySubresourceRegion(dst->res, 0, (UINT)rx, (UINT)ry, 0, sTgAux[d->target].res, 0, &box);
            continue;
        }
        if (d->native == 3) { /* a table pass (dclut.frag): reads the target's own depth, so it draws without it */
            float params[4] = {(float)d->src, d->misc[0], 0.0f, 0.0f};
            if (!gsTargets[d->target].cleared || sTgColRtv[d->target] == NULL) { continue; }
            bind_target(d->target, 0, 0);
            cur = d->target;
            set_pipe(d->pipeline);
            sCtx->IASetInputLayout(NULL);
            sCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            sCurIa = -1;
            set_textures(sTgDep[d->target].srv, srv_of((GsTex)&sAuxCopy, -1), srv_of(d->tex, cur), 6);
            set_params(params, 16);
            haveFu = 0;
            set_blend(sPipes[d->pipeline].bs, d->blendc);
            set_scissor(d->scissor);
            sCtx->Draw(3, 0);
            continue;
        }
        if (d->native) { /* the outline (1) and the alpha key (2) */
            float params[4];
            if (!gsTargets[d->target].cleared || sTgColRtv[d->target] == NULL) { continue; }
            bind_target(d->target, 0, 0);
            cur = d->target;
            set_native(d->native);
            set_textures(srv_of((GsTex)&sAuxCopy, -1), NULL, NULL, 6);
            params[0] = 1333.333f / (float)Port_PageAspectMilli() / (float)GS_W; /* one PS2 pixel across, as wide as at 4:3 (outline.frag) */
            params[1] = 1.0f / (float)GS_H;
            params[2] = 100.0f / 255.0f;
            params[3] = getenv("BT3_FX_DEBUG") != NULL ? 1.0f : 0.0f;
            set_params(params, 16);
            haveFu = 0;
            set_scissor(d->scissor);
            sCtx->Draw(3, 0);
            continue;
        }
        /* an ordinary primitive */
        if (sTgColRtv[d->target] == NULL) { continue; }
        shadowStart = sPipes[d->pipeline].shadow &&
            (n == 0 || gsDraws[n - 1].native || gsDraws[n - 1].target != d->target ||
             !sPipes[gsDraws[n - 1].pipeline].shadow);
        if (cur != d->target || sCurBound != d->target || !sCurDepth) {
            bind_target(d->target, !gsTargets[d->target].cleared, 1);
            gsTargets[d->target].cleared = 1;
            cur = d->target;
            haveFu = 0;
            sScissorKnown = 0;
        } else if (!gsTargets[d->target].cleared) {
            gsTargets[d->target].cleared = 1;
        }
        if (shadowStart && sTgDsv[d->target] != NULL) {
            sCtx->ClearDepthStencilView(sTgDsv[d->target], D3D11_CLEAR_STENCIL, 0.0f, 0); /* (a clear ignores the scissor) */
        }
        set_pipe(d->pipeline);
        set_ia(d->vu);
        if (d->vu) { sCtx->UpdateSubresource(sCbU, 0, NULL, &gsVuUni[d->uniform], 0, 0); }
        memset(&fu, 0, sizeof(fu));
        memcpy(fu.mode, d->mode, sizeof(fu.mode));
        memcpy(fu.misc, d->misc, sizeof(fu.misc));
        memcpy(fu.rect, d->rect, sizeof(fu.rect));
        memcpy(fu.orig, d->orig, sizeof(fu.orig));
        fu.extra[0] = d->vu ? (gsVuUni[d->uniform].misc[2] > 0.0f ? gsVuUni[d->uniform].misc[2] : 0.0f) : gsZMax;
        fu.extra[1] = (sPipes[d->pipeline].key & (1u << 9)) ? 1.0f : 0.0f; /* the source scaled by its alpha */
        if (!haveFu || memcmp(&fu, &lastFu, sizeof(fu)) != 0) {
            set_params(&fu, sizeof(fu));
            memcpy(&lastFu, &fu, sizeof(fu));
            haveFu = 1;
        }
        set_textures(srv_of(d->tex, cur), srv_of((GsTex)&sDateCopy, cur), NULL, d->sampler);
        set_blend(sPipes[d->pipeline].bs, d->blendc);
        set_scissor(d->scissor);
        if (d->vu) {
            sCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            sCtx->DrawIndexed(d->count, d->first, 0);
        } else {
            int topo = sPipes[d->pipeline].topo;
            sCtx->IASetPrimitiveTopology(topo == 0 ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST : topo == 1 ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST :
                                         D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
            sCtx->Draw(d->count, d->first);
        }
    }

    /* the shown buffer: the one sceGsSwapDBuff set up for this frame, else the first target */
    best = gGsMainFbp >= 0 ? GsDraw_TargetGet((uint32_t)gGsMainFbp, 0) : -1;
    for (i = 0; best < 0 && i < gsTargetCount; i++) { best = i; }
    unbind_srvs();
    resize_back();
    if (sBackRtv != NULL && sBackW > 0 && sBackH > 0) {
        float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        int sw = sBackW, sh = sBackH;
        sCtx->OMSetRenderTargets(1, &sBackRtv, NULL);
        sCurBound = -1;
        set_viewport(0.0f, 0.0f, (float)sw, (float)sh);
        sCtx->ClearRenderTargetView(sBackRtv, black);
        if (best >= 0 && gsTargets[best].cleared && sTgCol[best].srv != NULL) {
            float want = (float)Port_PageAspectMilli() / 1000.0f; /* 4:3 while the menus run, if asked for */
            int w = sw, h = sh, dx, dy;
            float params[4] = {512.0f / (float)GS_W, 448.0f / (float)GS_H, 0.0f, 0.0f};
            if ((float)sw > (float)sh * want) { w = (int)((float)sh * want + 0.5f); } else { h = (int)((float)sw / want + 0.5f); }
            dx = (sw - w) / 2;
            dy = (sh - h) / 2;
            { /* where the picture is in the window, for the overlay's stage and song names (ui.cpp), counted from the top */
                gUiPresentX = dx;
                gUiPresentY = dy;
                gUiPresentW = w;
                gUiPresentH = h;
            }
            set_viewport((float)dx, (float)dy, (float)w, (float)h);
            sCtx->RSSetState(sRastNoScissor);
            sCtx->OMSetDepthStencilState(sDsOff, 0);
            sCtx->OMSetBlendState(sBsOff, NULL, 0xFFFFFFFFu);
            sCtx->IASetInputLayout(NULL);
            sCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            sCurIa = -1;
            sCtx->VSSetShader(sVsPresent, NULL, 0);
            sCtx->PSSetShader(sPsPresent, NULL, 0);
            set_params(params, 16);
            set_textures(sTgCol[best].srv, NULL, NULL, 7); /* linear, clamped */
            sCtx->Draw(3, 0);
            unbind_srvs();
            sCtx->RSSetState(sRast);
        }
        set_viewport(0.0f, 0.0f, (float)sw, (float)sh);
        sCtx->OMSetRenderTargets(1, &sBackRtv, NULL);
        Ui_DrawGL(); /* the overlay, over the presented picture */
    }
    /* BT3_SHOT=<n>: read the shown buffer back to port/build/shots/gpu_NNNNN.ppm */
    {
        static int every = -1;
        if (every < 0) { every = getenv("BT3_SHOT") != NULL ? atoi(getenv("BT3_SHOT")) : 0; }
        if (every > 0 && gGsFrame % (unsigned)every == 0 && best >= 0 && gsTargets[best].cleared &&
            (getenv("BT3_SHOT_FROM") == NULL || (int)gGsFrame >= atoi(getenv("BT3_SHOT_FROM"))) &&
            (getenv("BT3_SHOT_TO") == NULL || (int)gGsFrame <= atoi(getenv("BT3_SHOT_TO")))) {
            int w = 512 * SCALE, h = 448 * SCALE, y;
            uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4);
            if (px != NULL && read_color(best, w, h, px)) {
                char name[64];
                FILE *fp;
                snprintf(name, sizeof(name), "port/build/shots/gpu_%05u.ppm", gGsFrame);
                fp = fopen(name, "wb");
                if (fp != NULL) {
                    fprintf(fp, "P6\n%d %d\n255\n", w, h);
                    for (y = 0; y < h * w; y++) { fwrite(&px[y * 4], 1, 3, fp); }
                    fclose(fp);
                }
            }
            free(px);
        }
    }
    if (sSwap != NULL && sBackW > 0 && sBackH > 0) {
        HRESULT hr = sSwap->Present(sVsync ? 1 : 0, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            static int said;
            if (!said) {
                said = 1;
                fprintf(stderr, "bt3: d3d11: the device was lost (0x%08lx; reason 0x%08lx)\n", (unsigned long)hr,
                        (unsigned long)sDev->GetDeviceRemovedReason());
            }
        }
    }
    flush_dead();

    for (i = 0; i < gsTargetCount; i++) { gsTargets[i].draws = 0; }
    gsVertCount = 0;
    gsVuVertCount = 0;
    gsVuIdxCount = 0;
    gsVuUniCount = 0;
    gsDrawCount = 0;
    gsAnchor = 0;
    gsSkipped = 0;
    gsNative = 0;
}

GsBackend sD3d11Backend = {
    "d3d11",
    d3d_init,
    frame_end,
    scale_changed,
    white_tex,
    tex_create,
    tex_upload,
    tex_destroy,
    tex_slots_left,
    target_ensure,
    target_drop,
    target_color,
    target_aux,
    target_depth,
    pipe_get,
    d3d_target_read,
    d3d_target_copy,
};

#else /* not Windows: the back end exists so that gs_draw.c can name it, and never starts */

static int d3d_init(void) { return 0; }
static void d3d_frame_end(void) {}
static void d3d_scale_changed(void) {}
static GsTex d3d_white(void) { return 0; }
static GsTex d3d_tex_create(uint32_t, uint32_t, int, int) { return 0; }
static int d3d_tex_upload(GsTex, int, uint32_t, uint32_t, int, const void *, uint32_t) { return 0; }
static void d3d_tex_destroy(GsTex) {}
static int d3d_slots(void) { return 0; }
static void d3d_target(int) {}
static GsTex d3d_target_tex(int) { return 0; }
static int d3d_pipe(uint32_t) { return 0; }
static int d3d_read(int, uint8_t *) { return 0; }
static void d3d_copy(int, int) {}

void *GsD3d11_StripTexture(const void *, int, int) { return NULL; }
void GsD3d11_StripTextureFree(void *) {}
void GsD3d11_Vsync(int) {}

GsBackend sD3d11Backend = {
    "d3d11", d3d_init, d3d_frame_end, d3d_scale_changed, d3d_white, d3d_tex_create, d3d_tex_upload, d3d_tex_destroy, d3d_slots,
    d3d_target, d3d_target, d3d_target_tex, d3d_target_tex, d3d_target_tex, d3d_pipe, d3d_read, d3d_copy,
};

#endif
