#version 450
// A texture drawn over the render target (fx.vert's full-screen triangle), sampling the part of it given by p.rect
// (u0, v0, u1, v1). The WebGPU back end's present and its scaled copy of a picture uploaded into a display buffer:
// WebGPU has no blit (the Vulkan back end uses SDL's, the OpenGL one glBlitFramebuffer and its own present program).
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
// SEPARATE_SAMPLERS: the WebGPU build (port/tools/wasm_shaders.py: naga -D SEPARATE_SAMPLERS). WGSL has no
// combined texture + sampler: each becomes a texture at binding 2k and its sampler at 2k + 1 of the same set
// (k = its binding below). Vulkan (glslc) and OpenGL compile the #else part, as before.
#ifdef SEPARATE_SAMPLERS
layout(set = 2, binding = 0) uniform texture2D src_texture;
layout(set = 2, binding = 1) uniform sampler src_sampler;
#define src sampler2D(src_texture, src_sampler)
#else
layout(set = 2, binding = 0) uniform sampler2D src;
#endif
layout(set = 3, binding = 0) uniform Params {
    vec4 rect;
} p;
void main() {
    outColor = texture(src, mix(p.rect.xy, p.rect.zw, vUv));
}
