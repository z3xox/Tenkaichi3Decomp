#version 450
// Native version of the game's "alpha key" pass (GfxAlphaKey_Draw, src/sys/gfxm_b.c): a model that must be seen
// through (a fighter between the camera and the action) does not draw its colours, only an id 0xF4..0xFE into
// the frame's alpha. The PS2 pass copies that byte into the depth buffer's spare byte and draws it over the
// screen through a table: eleven fixed colours with alpha 0x30 (of 0x80), everything else transparent.
// Here: the same table read from the exact copy of the alpha byte; the pipeline blends by the output alpha.
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
// SEPARATE_SAMPLERS: the WebGPU build (port/tools/wasm_shaders.py: naga -D SEPARATE_SAMPLERS). WGSL has no
// combined texture + sampler: each becomes a texture at binding 2k and its sampler at 2k + 1 of the same set
// (k = its binding below). Vulkan (glslc) and OpenGL compile the #else part, as before.
#ifdef SEPARATE_SAMPLERS
layout(set = 2, binding = 0) uniform texture2D aux_texture; // .r = the GS alpha byte / 255
layout(set = 2, binding = 1) uniform sampler aux_sampler;
#define aux sampler2D(aux_texture, aux_sampler)
#else
layout(set = 2, binding = 0) uniform sampler2D aux; // .r = the GS alpha byte / 255
#endif
const vec3 kTable[11] = vec3[11]( // GfxAlphaKey_BuildClut
    vec3(0x3F, 0x3F, 0xFF), vec3(0x80, 0x00, 0xFF), vec3(0xFF, 0xFF, 0x20), vec3(0xFF, 0x00, 0x00),
    vec3(0x00, 0xFF, 0x00), vec3(0x54, 0xFD, 0xFF), vec3(0xFF, 0x00, 0xFF), vec3(0x40, 0x00, 0xFF),
    vec3(0xFF, 0xFF, 0x20), vec3(0xFF, 0xFF, 0x2A), vec3(0xFF, 0xFF, 0x34));
void main() {
    int id = int(texture(aux, vUv).r * 255.0 + 0.5);
    if (id < 0xF4 || id == 0xFF) discard;
    outColor = vec4(kTable[id - 0xF4] / 255.0, float(0x30) / 128.0);
}
