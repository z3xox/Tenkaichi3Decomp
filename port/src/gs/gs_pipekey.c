/*
 * The pipeline key (gs_draw.h): GS register state -> GsPipeState -> key value, and back, plus the blend every back end
 * derives from it. Pure functions, no state: gs_draw.c builds keys while recording, each back end decodes them when it
 * makes a pipeline. Tested on its own: port/tests/pipekey.
 */
#include <string.h>
#include "gs_draw.h"

/* The key's fields (gs_draw.h describes the layout; the values must not change: they are kept between runs). */
#define KEY_BLEND_ENABLE 1u
#define KEY_BLEND_SRC_SHIFT 1
#define KEY_BLEND_DST_SHIFT 4
#define KEY_BLEND_C_SHIFT 7
#define KEY_DEPTH_TEST_SHIFT 10
#define KEY_DEPTH_WRITE_SHIFT 12
#define KEY_TOPOLOGY_SHIFT 13
#define KEY_WRITE_MASK_SHIFT 16
#define KEY_VERTICES_SHIFT 20
#define KEY_TABLE_PASS_SHIFT 22
#define KEY_NO_FRAG_DEPTH_SHIFT 23
#define KEY_BLEND_K_MASK 7u /* GsBlendK */
#define KEY_TWO_BITS 3u     /* GsBlendC, GsDepthTest, GsTopology, GsVertexSource */
#define KEY_WRITE_MASK_ALL (GS_CC_R | GS_CC_G | GS_CC_B | GS_CC_A)

/* The GS registers' fields read here (GS manual: ALPHA, PRIM, TEST, ZBUF, FRAME). */
#define ALPHA_A(alpha) ((int)((alpha) & 3))
#define ALPHA_B(alpha) ((int)(((alpha) >> 2) & 3))
#define ALPHA_C(alpha) ((int)(((alpha) >> 4) & 3))
#define ALPHA_D(alpha) ((int)(((alpha) >> 6) & 3))
#define TEST_ZTE(test) ((int)(((test) >> 16) & 1))
#define TEST_ZTST(test) ((int)(((test) >> 17) & 3))
#define ZBUF_ZMSK(zbuf) ((int)(((zbuf) >> 32) & 1))
#define FBMSK_CHANNEL(fbmsk, i) (((fbmsk) >> (8 * (i))) & 0xFFu) /* one byte of FRAME.FBMSK per channel, R G B A */
#define FBMSK_CHANNEL_MASKED 0xFFu

/* ((A - B) * C >> 7) + D (ALPHA, with PRIM.ABE) as ks * Cs + kd * Cd with ks, kd from {0, 1, C, -C, 1 - C, 1 + C}. Each
   of A, B, D is source (0), destination (1) or zero (2), so for each side A == side adds C, B == side subtracts it and
   D == side adds one. */
static GsBlend blend_from_alpha(uint64_t alpha, int abe) {
    GsBlendK k[2];
    GsBlend b;
    int side;

    memset(&b, 0, sizeof(b));
    if (!abe) {
        return b;
    }
    for (side = 0; side < 2; side++) {
        int c = (ALPHA_A(alpha) == side) - (ALPHA_B(alpha) == side), one = ALPHA_D(alpha) == side;
        k[side] = c == 0 ? (one ? GS_BLEND_K_ONE : GS_BLEND_K_ZERO)
                : c > 0  ? (one ? GS_BLEND_K_ONE_PLUS_C : GS_BLEND_K_C)
                         : (one ? GS_BLEND_K_ONE_MINUS_C : GS_BLEND_K_MINUS_C);
    }
    b.enabled = 1;
    b.src = k[0];
    b.dst = k[1];
    b.c = (GsBlendC)ALPHA_C(alpha);
    return b;
}

/* FRAME.FBMSK in the whole-channel forms ordinary drawing uses (alpha only, everything but alpha, ...): a channel whose
   eight mask bits are all set is not written. Partial masks are not representable. */
static uint32_t write_mask(uint32_t fbmsk) {
    static const uint32_t channel[4] = {GS_CC_R, GS_CC_G, GS_CC_B, GS_CC_A};
    uint32_t mask = 0;
    int i;
    for (i = 0; i < 4; i++) {
        if (FBMSK_CHANNEL(fbmsk, i) != FBMSK_CHANNEL_MASKED) {
            mask |= channel[i];
        }
    }
    return mask;
}

GsPipeState GsPipeState_FromGs(uint64_t alpha, int abe, uint64_t test, uint64_t zbuf, uint32_t fbmsk, GsTopology topology,
                               GsVertexSource vertices, int tablePass) {
    GsPipeState s;
    memset(&s, 0, sizeof(s));
    s.blend = blend_from_alpha(alpha, abe);
    s.writeMask = write_mask(fbmsk);
    if (tablePass) { /* only blending and the write mask matter for it */
        s.tablePass = 1;
        return s;
    }
    s.depthTest = TEST_ZTE(test) ? (GsDepthTest)TEST_ZTST(test) : GS_DEPTH_ALWAYS;
    s.depthWrite = !ZBUF_ZMSK(zbuf);
    s.topology = topology;
    s.vertices = vertices;
    return s;
}

/* What is in the depth buffer was written through the full shader, so it is whole numbers; and z >= N is the same as
   floor(z) >= N for a whole N. So a draw that does not write depth and tests with "greater or equal" (or not at all)
   gets the same picture without the shader's depth. ("Greater" is not the same: N + 0.5 > N but floor(N + 0.5) > N is
   not. Those keep the full shader, and so does everything that writes depth.) The table pass has no depth at all. */
int GsPipeState_FragDepthRedundant(const GsPipeState *s) {
    return !s->tablePass && !s->depthWrite && s->depthTest != GS_DEPTH_GREATER;
}

uint32_t GsPipeKey_Encode(const GsPipeState *s) {
    uint32_t key = s->writeMask << KEY_WRITE_MASK_SHIFT;

    if (s->blend.enabled) {
        key |= KEY_BLEND_ENABLE | (uint32_t)s->blend.src << KEY_BLEND_SRC_SHIFT | (uint32_t)s->blend.dst << KEY_BLEND_DST_SHIFT |
               (uint32_t)s->blend.c << KEY_BLEND_C_SHIFT;
    }
    if (s->tablePass) {
        return key | 1u << KEY_TABLE_PASS_SHIFT;
    }
    return key | (uint32_t)s->depthTest << KEY_DEPTH_TEST_SHIFT | (uint32_t)s->depthWrite << KEY_DEPTH_WRITE_SHIFT |
           (uint32_t)s->topology << KEY_TOPOLOGY_SHIFT | (uint32_t)s->vertices << KEY_VERTICES_SHIFT |
           (uint32_t)(s->noFragDepth != 0) << KEY_NO_FRAG_DEPTH_SHIFT;
}

void GsPipeKey_Decode(uint32_t key, GsPipeState *s) {
    s->blend.enabled = (key & KEY_BLEND_ENABLE) != 0;
    s->blend.src = (GsBlendK)((key >> KEY_BLEND_SRC_SHIFT) & KEY_BLEND_K_MASK);
    s->blend.dst = (GsBlendK)((key >> KEY_BLEND_DST_SHIFT) & KEY_BLEND_K_MASK);
    s->blend.c = (GsBlendC)((key >> KEY_BLEND_C_SHIFT) & KEY_TWO_BITS);
    s->depthTest = (GsDepthTest)((key >> KEY_DEPTH_TEST_SHIFT) & KEY_TWO_BITS);
    s->depthWrite = (int)((key >> KEY_DEPTH_WRITE_SHIFT) & 1u);
    s->topology = (GsTopology)((key >> KEY_TOPOLOGY_SHIFT) & KEY_TWO_BITS);
    s->writeMask = (key >> KEY_WRITE_MASK_SHIFT) & KEY_WRITE_MASK_ALL;
    s->vertices = (GsVertexSource)((key >> KEY_VERTICES_SHIFT) & KEY_TWO_BITS);
    s->tablePass = (int)((key >> KEY_TABLE_PASS_SHIFT) & 1u);
    s->noFragDepth = (int)((key >> KEY_NO_FRAG_DEPTH_SHIFT) & 1u);
}

static GsBlendFactor blend_factor(GsBlendK k) {
    switch (k) {
    case GS_BLEND_K_ZERO: return GS_FACTOR_ZERO;
    case GS_BLEND_K_ONE:
    case GS_BLEND_K_ONE_PLUS_C: return GS_FACTOR_ONE; /* (1 + C: a blend factor cannot go above one) */
    case GS_BLEND_K_ONE_MINUS_C: return GS_FACTOR_ONE_MINUS_C;
    case GS_BLEND_K_C:
    case GS_BLEND_K_MINUS_C: break; /* -C: C with the subtracting operation */
    }
    return GS_FACTOR_C;
}

GsBlendDesc GsBlend_Describe(GsBlend b) {
    GsBlendDesc d;
    d.srcFactor = blend_factor(b.src);
    d.dstFactor = blend_factor(b.dst);
    d.op = b.src == GS_BLEND_K_MINUS_C ? GS_OP_REVERSE_SUBTRACT : b.dst == GS_BLEND_K_MINUS_C ? GS_OP_SUBTRACT : GS_OP_ADD;
    d.c = b.c;
    return d;
}
