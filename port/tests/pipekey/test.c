/* Test of the pipeline key (port/src/gs/gs_pipekey.c) against the code it replaced.
 *
 * REFERENCE below is a verbatim copy of the encoding as it was in gs_draw.c (blend_key, pipeline_get; upstream 0.1.31's,
 * with bit 23, the fragment shader without its depth output) and of the blend factors the OpenGL back end chose from it
 * (gs_gl.c apply_blend), before the shared decoder existed. Every combination of the inputs is checked, with the
 * variant switched on and off (BT3_NZ):
 *   1. the new key equals the old key (the values are kept between runs, and the Vulkan back end decodes them itself);
 *   2. decoding a key and encoding it again gives the same key;
 *   3. the shared blend description (GsBlend_Describe) picks what the old OpenGL code picked.
 * Build and run: python port/tests/pipekey/run.py */
#include <stdint.h>
#include <stdio.h>
#include "gs_draw.h"

/* ---- REFERENCE (do not change: it is what the new code must reproduce) ---- */
static uint32_t ref_blend_key(uint64_t alpha, int abe) {
    int A = alpha & 3, B = (alpha >> 2) & 3, C = (alpha >> 4) & 3, D = (alpha >> 6) & 3, k[2], i;
    if (!abe) {
        return 0;
    }
    for (i = 0; i < 2; i++) {
        int c = (A == i) - (B == i), one = D == i;
        k[i] = c == 0 ? one : c > 0 ? (one ? 5 : 2) : (one ? 4 : 3);
    }
    return 1u | (uint32_t)k[0] << 1 | (uint32_t)k[1] << 4 | (uint32_t)C << 7;
}
static uint32_t ref_key(uint64_t alpha, int abe, uint64_t test, uint64_t zb, uint32_t m, int topo, int vu, int nz) {
    int zte = (test >> 16) & 1, ztst = (test >> 17) & 3, zwrite = !((zb >> 32) & 1);
    uint32_t bkey = ref_blend_key(alpha, abe), wmask, key;
    if (!zte) {
        ztst = 1;
    }
    wmask = ((m & 0xFF) != 0xFF ? GS_CC_R : 0) | ((m & 0xFF00) != 0xFF00 ? GS_CC_G : 0) |
            ((m & 0xFF0000) != 0xFF0000 ? GS_CC_B : 0) | ((m & 0xFF000000u) != 0xFF000000u ? GS_CC_A : 0);
    key = bkey | (uint32_t)ztst << 10 | (uint32_t)zwrite << 12 | (uint32_t)topo << 13 | wmask << 16 | (uint32_t)vu << 20;
    if (nz && !zwrite && ztst != 3 && vu != 4) {
        key |= 1u << 23;
    }
    if (vu == 4) {
        key = bkey | wmask << 16 | 1u << 22;
    }
    return key;
}
/* gs_gl.c apply_blend: per side 'Z' zero, '1' one, 'c' C, 'i' 1 - C; op 'A' add, 'S' subtract, 'R' reverse subtract;
   C source 0 src alpha, 1 dst alpha, 2 constant */
static void ref_gl_blend(uint32_t bkey, char f[2], char *op, int *csrc) {
    int k[2] = {(int)((bkey >> 1) & 7), (int)((bkey >> 4) & 7)}, C = (int)((bkey >> 7) & 3), i;
    for (i = 0; i < 2; i++) {
        f[i] = k[i] == 0 ? 'Z' : k[i] == 1 || k[i] == 5 ? '1' : k[i] == 4 ? 'i' : 'c';
    }
    *op = k[0] == 3 ? 'R' : k[1] == 3 ? 'S' : 'A';
    *csrc = C == 0 ? 0 : C == 1 ? 1 : 2;
}
/* ---- end of REFERENCE ---- */

static char factor_char(GsBlendFactor f) {
    return f == GS_FACTOR_ZERO ? 'Z' : f == GS_FACTOR_ONE ? '1' : f == GS_FACTOR_C ? 'c' : 'i';
}

int main(void) {
    static const uint32_t channel[3] = {0x00, 0xFF, 0x0F}; /* written, masked, partly masked (counts as written) */
    unsigned long checked = 0, bad[3] = {0, 0, 0};
    unsigned alpha, abe, test, zmsk, fb, topo, vu, nz;

    for (alpha = 0; alpha < 256; alpha++)
    for (abe = 0; abe < 2; abe++)
    for (test = 0; test < 8; test++)          /* TEST.ZTE and TEST.ZTST */
    for (zmsk = 0; zmsk < 2; zmsk++)
    for (fb = 0; fb < 81; fb++)               /* each of the four FBMSK bytes one of channel[] */
    for (topo = 0; topo < 3; topo++)
    for (vu = 0; vu < 5; vu++)                /* 0..3 vertex sources, 4: the old code's "table pass" */
    for (nz = 0; nz < 2; nz++) {              /* BT3_NZ: the variant without depth output allowed or not */
        uint64_t t = (uint64_t)test << 16, z = (uint64_t)zmsk << 32;
        uint32_t m = channel[fb % 3] | channel[fb / 3 % 3] << 8 | channel[fb / 9 % 3] << 16 | channel[fb / 27 % 3] << 24;
        uint32_t want = ref_key(alpha, (int)abe, t, z, m, (int)topo, (int)vu, (int)nz), key, again;
        GsPipeState s = GsPipeState_FromGs(alpha, (int)abe, t, z, m, (GsTopology)topo, vu == 4 ? GS_VERTS_GS : (GsVertexSource)vu, vu == 4);
        GsPipeState d;

        s.noFragDepth = nz && GsPipeState_FragDepthRedundant(&s); /* (as gs_draw.c pipeline_get) */

        key = GsPipeKey_Encode(&s);
        GsPipeKey_Decode(key, &d);
        again = GsPipeKey_Encode(&d);
        checked++;
        if (key != want && bad[0]++ < 5) {
            printf("  key differs: alpha %02x abe %u test %u zmsk %u fbmsk %08x topo %u vu %u: new %08x, old %08x\n", alpha, abe, test, zmsk, m, topo, vu, key, want);
        }
        if (again != key && bad[1]++ < 5) {
            printf("  decode/encode changes %08x into %08x\n", key, again);
        }
        if (s.blend.enabled) {
            char f[2], op;
            int csrc;
            GsBlendDesc bd = GsBlend_Describe(d.blend);
            char nop = bd.op == GS_OP_ADD ? 'A' : bd.op == GS_OP_SUBTRACT ? 'S' : 'R';
            int nc = bd.c == GS_BLEND_C_SRC_ALPHA ? 0 : bd.c == GS_BLEND_C_DST_ALPHA ? 1 : 2;
            ref_gl_blend(want & 0x1FF, f, &op, &csrc);
            if ((factor_char(bd.srcFactor) != f[0] || factor_char(bd.dstFactor) != f[1] || nop != op || nc != csrc) && bad[2]++ < 5) {
                printf("  blend differs for key %08x: new %c %c %c C%d, old %c %c %c C%d\n", key, factor_char(bd.srcFactor),
                       factor_char(bd.dstFactor), nop, nc, f[0], f[1], op, csrc);
            }
        }
    }
    printf("%lu combinations: %lu keys differ from the old encoding, %lu change on decode + encode, %lu blends differ\n",
           checked, bad[0], bad[1], bad[2]);
    printf(bad[0] || bad[1] || bad[2] ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return bad[0] || bad[1] || bad[2];
}
