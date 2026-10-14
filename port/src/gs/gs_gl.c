/*
 * Graphics Synthesizer, OpenGL back end (GL 3.3 core: SDL3 window + a core context).
 *
 * gs_draw.c records the frame (every primitive as a GsDraw, the vertices, the decoded textures) and installs
 * this file as the sGlBackend. It owns the GL objects (programs translated from the Vulkan GLSL at run time,
 * vertex buffers, textures, sampler objects, one framebuffer per GS frame-buffer address) and replays the list.
 *
 * Written from the ground up against the canonical OpenGL 3.3 core patterns (learnopengl.com):
 *   - one VAO per vertex layout, configured once with the VBO bound;
 *   - a framebuffer per target: colour (GL_RGBA8) + aux (GL_R8) + depth (GL_DEPTH_COMPONENT32F), complete, and
 *     used through GL_FRAMEBUFFER;
 *   - depth/colour state set explicitly before every draw, exactly like a pipeline object would be.
 * The GL floor is 3.3 core.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "gs_internal.h"
#include "gs_draw.h"
#include "shaders.h"
#include "ui.h"

extern int Port_AspectMilli(void);

/* ---- GL 3.3 core entry points (loaded through SDL_GL_GetProcAddress) ------------------------------ */
typedef unsigned int GLenum; typedef unsigned int GLuint; typedef int GLint; typedef int GLsizei;
typedef unsigned char GLboolean; typedef char GLchar; typedef float GLfloat; typedef unsigned int GLbitfield;
typedef void GLvoid; typedef ptrdiff_t GLsizeiptr; typedef ptrdiff_t GLintptr; typedef unsigned char GLubyte;

#define GL_FUNCS(X) \
    X(glGetError, GLenum, (void), ()) \
    X(glEnable, void, (GLenum), (GLenum)) \
    X(glDisable, void, (GLenum), (GLenum)) \
    X(glEnablei, void, (GLenum, GLuint), (GLenum, GLuint)) \
    X(glDisablei, void, (GLenum, GLuint), (GLenum, GLuint)) \
    X(glViewport, void, (GLint, GLint, GLsizei, GLsizei), (GLint, GLint, GLsizei, GLsizei)) \
    X(glScissor, void, (GLint, GLint, GLsizei, GLsizei), (GLint, GLint, GLsizei, GLsizei)) \
    X(glClearColor, void, (GLfloat, GLfloat, GLfloat, GLfloat), (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(glClear, void, (GLbitfield), (GLbitfield)) \
    X(glClearBufferfv, void, (GLenum, GLint, const GLfloat *), (GLenum, GLint, const GLfloat *)) \
    X(glGetString, const GLubyte *, (GLenum), (GLenum)) \
    X(glGetIntegerv, void, (GLenum, GLint *), (GLenum, GLint *)) \
    X(glFinish, void, (void), ()) \
    X(glPixelStorei, void, (GLenum, GLint), (GLenum, GLint)) \
    X(glBlendFuncSeparate, void, (GLenum, GLenum, GLenum, GLenum), (GLenum, GLenum, GLenum, GLenum)) \
    X(glBlendEquationSeparate, void, (GLenum, GLenum), (GLenum, GLenum)) \
    X(glBlendColor, void, (GLfloat, GLfloat, GLfloat, GLfloat), (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(glColorMaski, void, (GLuint, GLboolean, GLboolean, GLboolean, GLboolean), (GLuint, GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(glColorMask, void, (GLboolean, GLboolean, GLboolean, GLboolean), (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(glDepthFunc, void, (GLenum), (GLenum)) \
    X(glDepthMask, void, (GLboolean), (GLboolean)) \
    X(glCreateShader, GLuint, (GLenum), (GLenum)) \
    X(glShaderSource, void, (GLuint, GLsizei, const GLchar *const *, const GLint *), (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(glCompileShader, void, (GLuint), (GLuint)) \
    X(glGetShaderiv, void, (GLuint, GLenum, GLint *), (GLuint, GLenum, GLint *)) \
    X(glGetShaderInfoLog, void, (GLuint, GLsizei, GLsizei *, GLchar *), (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(glDeleteShader, void, (GLuint), (GLuint)) \
    X(glCreateProgram, GLuint, (void), ()) \
    X(glAttachShader, void, (GLuint, GLuint), (GLuint, GLuint)) \
    X(glLinkProgram, void, (GLuint), (GLuint)) \
    X(glGetProgramiv, void, (GLuint, GLenum, GLint *), (GLuint, GLenum, GLint *)) \
    X(glGetProgramInfoLog, void, (GLuint, GLsizei, GLsizei *, GLchar *), (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(glUseProgram, void, (GLuint), (GLuint)) \
    X(glDeleteProgram, void, (GLuint), (GLuint)) \
    X(glGetUniformLocation, GLint, (GLuint, const GLchar *), (GLuint, const GLchar *)) \
    X(glUniform1i, void, (GLint, GLint), (GLint, GLint)) \
    X(glUniform2f, void, (GLint, GLfloat, GLfloat), (GLint, GLfloat, GLfloat)) \
    X(glGetUniformBlockIndex, GLuint, (GLuint, const GLchar *), (GLuint, const GLchar *)) \
    X(glUniformBlockBinding, void, (GLuint, GLuint, GLuint), (GLuint, GLuint, GLuint)) \
    X(glBindBufferBase, void, (GLenum, GLuint, GLuint), (GLenum, GLuint, GLuint)) \
    X(glGenBuffers, void, (GLsizei, GLuint *), (GLsizei, GLuint *)) \
    X(glBindBuffer, void, (GLenum, GLuint), (GLenum, GLuint)) \
    X(glBufferData, void, (GLenum, GLsizeiptr, const void *, GLenum), (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(glBufferSubData, void, (GLenum, GLintptr, GLsizeiptr, const void *), (GLenum, GLintptr, GLsizeiptr, const void *)) \
    X(glGenVertexArrays, void, (GLsizei, GLuint *), (GLsizei, GLuint *)) \
    X(glBindVertexArray, void, (GLuint), (GLuint)) \
    X(glEnableVertexAttribArray, void, (GLuint), (GLuint)) \
    X(glVertexAttribPointer, void, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *), (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(glDrawArrays, void, (GLenum, GLint, GLsizei), (GLenum, GLint, GLsizei)) \
    X(glDrawElements, void, (GLenum, GLsizei, GLenum, const void *), (GLenum, GLsizei, GLenum, const void *)) \
    X(glGenSamplers, void, (GLsizei, GLuint *), (GLsizei, GLuint *)) \
    X(glBindSampler, void, (GLuint, GLuint), (GLuint, GLuint)) \
    X(glSamplerParameteri, void, (GLuint, GLenum, GLint), (GLuint, GLenum, GLint)) \
    X(glGenTextures, void, (GLsizei, GLuint *), (GLsizei, GLuint *)) \
    X(glBindTexture, void, (GLenum, GLuint), (GLenum, GLuint)) \
    X(glActiveTexture, void, (GLenum), (GLenum)) \
    X(glTexImage2D, void, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *), (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(glTexSubImage2D, void, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *), (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(glCompressedTexImage2D, void, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *), (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *)) \
    X(glTexParameteri, void, (GLenum, GLenum, GLint), (GLenum, GLenum, GLint)) \
    X(glDeleteTextures, void, (GLsizei, const GLuint *), (GLsizei, const GLuint *)) \
    X(glGenFramebuffers, void, (GLsizei, GLuint *), (GLsizei, GLuint *)) \
    X(glBindFramebuffer, void, (GLenum, GLuint), (GLenum, GLuint)) \
    X(glFramebufferTexture2D, void, (GLenum, GLenum, GLenum, GLuint, GLint), (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(glCheckFramebufferStatus, GLenum, (GLenum), (GLenum)) \
    X(glDeleteFramebuffers, void, (GLsizei, const GLuint *), (GLsizei, const GLuint *)) \
    X(glDrawBuffers, void, (GLsizei, const GLenum *), (GLsizei, const GLenum *)) \
    X(glReadBuffer, void, (GLenum), (GLenum)) \
    X(glBlitFramebuffer, void, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum), (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(glReadPixels, void, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *), (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))

#define GL_DECL(name, ret, args, callargs) static ret (*name) args;
GL_FUNCS(GL_DECL)
#undef GL_DECL

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_RED 0x1903
#define GL_COLOR 0x1800
#define GL_DEPTH 0x1801
#define GL_DEPTH_COMPONENT 0x1902
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_R8 0x8229
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_REPEAT 0x2901
#define GL_ARRAY_BUFFER 0x8892
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
#endif
#define GL_STREAM_DRAW 0x88E0
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_BLEND 0x0BE2
#define GL_DEPTH_TEST 0x0B71
#define GL_SCISSOR_TEST 0x0C11
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_CLAMP 0x864F
#define GL_LOWER_LEFT 0x8CA1
#define GL_ZERO_TO_ONE 0x935F
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_CONSTANT_COLOR 0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#define GL_FUNC_ADD 0x8006
#define GL_FUNC_SUBTRACT 0x800A
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_NEVER 0x0200
#define GL_GREATER 0x0204
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207
#define GL_FRAMEBUFFER 0x8D40
#define GL_BACK 0x0405
#define GL_FRONT 0x0404
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_ATTACHMENT1 0x8CE1
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_INVALID_INDEX 0xFFFFFFFFu

static void (*glClipControl)(GLenum, GLenum); /* optional: GL 4.5 / ARB_clip_control */
static int sHasClipControl;

#ifdef __EMSCRIPTEN__
/* port/src/web/web_gl.c: functions WebGL 2 has only as extensions (indexed blend / mask, clip control) */
extern void *Port_WebGLProc(const char *name);
extern int Port_WebGLExtension(const char *name);
#define WEB_PROC(name, ret, args) if (!name) { name = (ret (*) args)Port_WebGLProc(#name); }
/* without OES_draw_buffers_indexed the back end takes its fallback (sHasIndexed 0) */
#define WEB_OPTIONAL(name) (!strcmp(#name, "glEnablei") || !strcmp(#name, "glDisablei") || !strcmp(#name, "glColorMaski"))
#else
#define WEB_PROC(name, ret, args)
#define WEB_OPTIONAL(name) 0
#endif

static int load_gl(void) {
#define LOAD(name, ret, args, callargs) \
    name = (ret (*) args)SDL_GL_GetProcAddress(#name); \
    WEB_PROC(name, ret, args) \
    if (!name && !WEB_OPTIONAL(name)) { fprintf(stderr, "bt3: gl: missing GL entry point %s\n", #name); return 0; }
    GL_FUNCS(LOAD)
#undef LOAD
    return 1;
}

/* ---- shader translation: Vulkan GLSL 450 -> GL 3.3 core ------------------------------------------ */
#ifdef __EMSCRIPTEN__
/* WebGL 2: GLSL ES 3.00, which has no default precision for floats in fragment shaders */
#define GLSL_HEADER "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
#else
#define GLSL_HEADER "#version 330 core"
#endif
/* The rules port/tools/glprobe.c proved on hardware: #version, strip layout(set/binding) on uniforms and
   samplers, gl_VertexIndex -> gl_VertexID, and layout(location) kept only where GL 3.3 core allows it
   (vertex inputs and fragment outputs). */
static char *translate(const char *src, int is_vertex) {
    size_t cap = strlen(src) + 128, len = 0;
    char *out = malloc(cap);
#define PUSHN(s, n_) do { size_t _n = (n_); \
    while (len + _n + 1 > cap) cap *= 2; out = realloc(out, cap); memcpy(out + len, (s), _n); len += _n; } while (0)
#define PUSH(str) PUSHN((str), strlen(str))
    const char *p = src;
    while (*p) {
        if (!strncmp(p, "#version 450", 12)) { PUSH(GLSL_HEADER); p += 12; continue; }
        if (!strncmp(p, "gl_VertexIndex", 14)) { PUSH("gl_VertexID"); p += 14; continue; }
        if (!strncmp(p, "layout(depth_less)", 18)) { /* (GL 4.2's; without it gl_FragDepth is simply written) */
            while (*p && *p != '\n') { p++; }
            continue;
        }
        if (!strncmp(p, "layout(set", 10)) {
            const char *e = strchr(p, ')');
            if (e) {
                p = e + 1;
                if (*p == ' ') p++;
#ifdef __EMSCRIPTEN__
                /* a uniform BLOCK: std140, the layout the back end fills (without a qualifier GLSL ES's is "shared") */
                if (!strncmp(p, "uniform ", 8) && strncmp(p + 8, "sampler", 7) != 0) { PUSH("layout(std140) "); }
#endif
                continue;
            }
        }
        if (!strncmp(p, "layout(location", 15)) {
            const char *e = strchr(p, ')');
            if (e) {
                const char *q = e + 1;
                while (*q == ' ' || *q == '\t') q++;
                int in_out = !strncmp(q, "in ", 3) || !strncmp(q, "out ", 4);
                int bad = in_out && (is_vertex ? !strncmp(q, "out ", 4) : !strncmp(q, "in ", 3));
                if (bad) { p = q; continue; }
                PUSHN(p, (size_t)(e + 1 - p));
                p = e + 1;
                continue;
            }
        }
        out[len++] = *p++;
    }
    out[len] = 0;
    /* SDL GPU (Vulkan) maps clip-space +Y to the TOP of the render target; OpenGL maps it to the BOTTOM, so
       the same shader would store the picture in the other half of the target (black window). Voltear
       gl_Position.y makes OpenGL store the picture exactly where the Vulkan back end does, so the present,
       the screenshots, gl_FragCoord and every texture taken from a target all agree. */
    if (is_vertex) {
        char *m = strstr(out, "void main");
        if (m != NULL) {
            size_t rest = strlen(m + 5) + 1;
            memmove(m + 8, m + 5, rest);
            memcpy(m + 5, "gs_", 3);
            len += 3;
        }
        /* Depth: the shaders put z in Vulkan's clip range (0..w, stored as z / w). OpenGL's is -w..w, stored as
           0.5 + z / 2w, unless glClipControl(GL_ZERO_TO_ONE) is there (GL 4.5 / ARB_clip_control); without it z
           is moved to OpenGL's range here so that the stored depth is again z / w (the fog pass reads it). */
        PUSH(sHasClipControl ? "\nvoid main() { gs_main(); gl_Position.y = -gl_Position.y; }\n"
                             : "\nvoid main() { gs_main(); gl_Position.y = -gl_Position.y; gl_Position.z = 2.0 * gl_Position.z - gl_Position.w; }\n");
        out[len] = 0;
    }
#undef PUSH
#undef PUSHN
    return out;
}

static GLuint compile(GLenum stage, const char *src, const char *tag) {
    GLuint sh = glCreateShader(stage);
    GLint ok = 0;
#ifdef __EMSCRIPTEN__
    char *es = NULL; /* the hand-written shaders (present) carry "#version 330 core": the same header as the rest */
    if (!strncmp(src, "#version 330 core", 17)) {
        es = malloc(strlen(src) + sizeof(GLSL_HEADER));
        strcpy(es, GLSL_HEADER);
        strcat(es, src + 17);
        src = es;
    }
#endif
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
#ifdef __EMSCRIPTEN__
    if (!ok) {
        fprintf(stderr, "bt3: gl: %s source:\n%s\n", tag, src);
    }
    free(es);
    src = NULL;
#endif
    if (!ok) {
        GLint n = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &n);
        char *log = malloc((size_t)n + 1);
        glGetShaderInfoLog(sh, n, NULL, log);
        fprintf(stderr, "bt3: gl: %s compile FAILED\n%s\n", tag, log);
        free(log);
        exit(1);
    }
    return sh;
}

static GLuint make_program(const unsigned char *vsrc, const unsigned char *fsrc, const char *tag, const char *const *samplers, int ns) {
    char *vt = translate((const char *)vsrc, 1), *ft = translate((const char *)fsrc, 0);
    GLuint v = compile(GL_VERTEX_SHADER, vt, tag), f = compile(GL_FRAGMENT_SHADER, ft, tag), p;
    GLint ok = 0, i;
    free(vt); free(ft);
    p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint n = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &n);
        char *log = malloc((size_t)n + 1);
        glGetProgramInfoLog(p, n, NULL, log);
        fprintf(stderr, "bt3: gl: %s link FAILED\n%s\n", tag, log);
        free(log);
        exit(1);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    glUseProgram(p);
    for (i = 0; i < ns; i++) {
        glUniform1i(glGetUniformLocation(p, samplers[i]), i);
    }
    {   /* U (vertex programs' constants) -> binding 0; Params (draws' state) -> binding 1; never the same */
        GLuint idx = glGetUniformBlockIndex(p, "U");
        if (idx != GL_INVALID_INDEX) { glUniformBlockBinding(p, idx, 0); }
        idx = glGetUniformBlockIndex(p, "Params");
        if (idx != GL_INVALID_INDEX) { glUniformBlockBinding(p, idx, 1); }
    }
    return p;
}

/* The present: one textured full-screen triangle sampling the shown buffer's colour, drawn with the viewport
   set to the letterboxed rectangle. (A textured quad, as the OpenGL framebuffer guide does it, instead of a
   scaled/flipped blit.) */
static GLuint make_present_program(void) {
    static const char *vs = "#version 330 core\nout vec2 vUv;\nvoid main() {\n"
        "    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
        "    vUv = vec2((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);\n"  /* y flipped: the scene's top row to the window's top */
        "    gl_Position = vec4(p, 0.0, 1.0);\n}\n";
    static const char *fs = "#version 330 core\nin vec2 vUv;\nout vec4 o;\nuniform sampler2D tex;\nuniform vec2 uvScale;\n"
        "void main() { o = texture(tex, vUv * uvScale); }\n";
    GLuint v = compile(GL_VERTEX_SHADER, vs, "present.vs"), f = compile(GL_FRAGMENT_SHADER, fs, "present.fs"), p;
    GLint ok = 0;
    p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "bt3: gl: present link FAILED\n");
        exit(1);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "tex"), 0);
    glUniform2f(glGetUniformLocation(p, "uvScale"), (float)GS_DISPLAY_W / (float)GS_W, (float)GS_DISPLAY_H / (float)GS_H);
    return p;
}

/* ---- state -------------------------------------------------------------------------------------- */
static SDL_Window *sWindow;
static SDL_GLContext sCtx;
/* [0]: gs.frag, [1]: gsnz.frag (gs.frag without its depth output, key bit 23); each after gs.vert / vu0 / vu4 / vu6 */
static GLuint sMainProg[2][4];
static GLuint sOutlineProg, sKeyProg, sDclutProg, sPresentProg;
static GLuint sVaoGs, sVaoVu, sVboGs, sVboVu, sIboVu;
static GLuint sSamplers[GS_SAMPLER_COUNT]; /* indexed by GS_SAMPLER_* flags */
static GLuint sWhite;
static GLuint sUboU, sUboParams;
static GLuint sTgCol[MAX_TARGETS], sTgAux[MAX_TARGETS], sTgDep[MAX_TARGETS], sTgFbo[MAX_TARGETS];
#ifdef __EMSCRIPTEN__
/* The same colour attachments without the depth one. The depth table pass (native 3) samples the target's depth
   texture while drawing into the target: a feedback loop WebGL refuses (INVALID_OPERATION, the draw is dropped:
   found with the page's ?gldebug=1, "unit 0 = depth attachment"), which desktop GL tolerates. That pass neither tests
   nor writes depth (set_pipe: fx pipelines), so drawing it here gives the same picture. */
static GLuint sTgFboNoDepth[MAX_TARGETS];
#endif
static GLuint sDateCopy, sAuxCopy;
static GLuint sScratchRead, sScratchDraw;
static GLuint sFbTex[2], sFbFbo[2];
static int sHasIndexed;
static const char *sGsSamplers[2] = {"tex", "dateTex"};
static const char *sOneSampler[1] = {"aux"};
static const char *sDclutSamplers[3] = {"depthTex", "auxTex", "clutTex"};

typedef struct Pipe { uint32_t key; GsPipeState s; } Pipe; /* s: the key decoded (gs_draw.h) */
static Pipe sPipes[1024];
static int sPipeCount;

static void target_ensure(int i);
static void target_drop(int i);
static void set_pipe(int pipe);
static void bind_target(int i, int clear);

/* ---- the attachments of the shared targets ------------------------------------------------------ */
static void target_ensure(int i) {
    GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glGenTextures(1, &sTgCol[i]);
    glBindTexture(GL_TEXTURE_2D, sTgCol[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GS_W * SCALE, GS_H * SCALE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sTgAux[i]);
    glBindTexture(GL_TEXTURE_2D, sTgAux[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, GS_W * SCALE, GS_H * SCALE, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sTgDep[i]);
    glBindTexture(GL_TEXTURE_2D, sTgDep[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, GS_W * SCALE, GS_H * SCALE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &sTgFbo[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, sTgFbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sTgCol[i], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, sTgAux[i], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sTgDep[i], 0);
    glDrawBuffers(2, bufs);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "bt3: gl: target %d framebuffer incomplete\n", i);
    }
#ifdef __EMSCRIPTEN__
    glGenFramebuffers(1, &sTgFboNoDepth[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, sTgFboNoDepth[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sTgCol[i], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, sTgAux[i], 0);
    glDrawBuffers(2, bufs);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "bt3: gl: target %d framebuffer without depth incomplete\n", i);
    }
#endif
}

static void target_drop(int i) {
    glDeleteTextures(1, &sTgCol[i]);
    glDeleteTextures(1, &sTgAux[i]);
    glDeleteTextures(1, &sTgDep[i]);
    glDeleteFramebuffers(1, &sTgFbo[i]);
    sTgCol[i] = sTgAux[i] = sTgDep[i] = sTgFbo[i] = 0;
#ifdef __EMSCRIPTEN__
    glDeleteFramebuffers(1, &sTgFboNoDepth[i]);
    sTgFboNoDepth[i] = 0;
#endif
}

static GsTex target_color(int i) { return (GsTex)sTgCol[i]; }
static GsTex target_aux(int i) { return (GsTex)sTgAux[i]; }
static GsTex target_depth(int i) { return (GsTex)sTgDep[i]; }

/* Makes a texture; its levels come with tex_upload. GL_TEXTURE_MAX_LEVEL bounds the chain so that a mipmapped
   replacement is complete even if the pack's chain stops short of 1x1. */
static GsTex tex_create(uint32_t w, uint32_t h, int format, int levels) {
    GLuint t;
    (void)w; (void)h; (void)format;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels > 0 ? levels - 1 : 0);
    return (GsTex)t;
}

/* Uploads a level at once (GL has no upload queue; the pixels are copied). Takes ownership of px. */
static int tex_upload(GsTex handle, int level, uint32_t w, uint32_t h, int format, const void *px, uint32_t bytes) {
    GLuint t = (GLuint)handle;
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (format == GS_TEXFMT_RGBA8) {
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    } else {
        GLenum ifmt = format == GS_TEXFMT_BC1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
                      format == GS_TEXFMT_BC2 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        glCompressedTexImage2D(GL_TEXTURE_2D, level, ifmt, (GLsizei)w, (GLsizei)h, 0, (GLsizei)bytes, px);
    }
    free((void *)px);
    return 1;
}

static void tex_destroy(GsTex tex) { GLuint t = (GLuint)tex; glDeleteTextures(1, &t); }
static int tex_slots_left(void) { return GS_TEX_SLOTS_UNLIMITED; } /* (uploads go to GL at once) */
static GsTex white_tex(void) { return (GsTex)sWhite; }

static void copies_create(void) {
    glGenTextures(1, &sDateCopy);
    glBindTexture(GL_TEXTURE_2D, sDateCopy);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, GS_W * SCALE, GS_H * SCALE, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sAuxCopy);
    glBindTexture(GL_TEXTURE_2D, sAuxCopy);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, GS_W * SCALE, GS_H * SCALE, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
}

static void scale_changed(void) {
    int i;
    glFinish();
    for (i = 0; i < gsTargetCount; i++) {
        target_drop(i);
    }
    glDeleteTextures(1, &sDateCopy);
    glDeleteTextures(1, &sAuxCopy);
    sDateCopy = sAuxCopy = 0;
    copies_create();
}

/* ---- pipelines ---------------------------------------------------------------------------------- */
/* GL has no pipeline objects: a "pipeline" here is the decoded key, applied as GL state by set_pipe. */
static int pipe_get(uint32_t key) {
    int i;
    for (i = 0; i < sPipeCount; i++) {
        if (sPipes[i].key == key) { return i; }
    }
    if (sPipeCount == 1024) { return 0; }
    sPipes[sPipeCount].key = key;
    GsPipeKey_Decode(key, &sPipes[sPipeCount].s);
    return sPipeCount++;
}

/* The shared state values (gs_draw.h) as GL's. */
static const GLenum kGlDepthFunc[] = {[GS_DEPTH_NEVER] = GL_NEVER, [GS_DEPTH_ALWAYS] = GL_ALWAYS, [GS_DEPTH_GEQUAL] = GL_GEQUAL,
                                      [GS_DEPTH_GREATER] = GL_GREATER};
static const GLenum kGlTopology[] = {[GS_TOPOLOGY_TRIANGLES] = GL_TRIANGLES, [GS_TOPOLOGY_LINES] = GL_LINES,
                                     [GS_TOPOLOGY_POINTS] = GL_POINTS};
static const GLenum kGlBlendOp[] = {[GS_OP_ADD] = GL_FUNC_ADD, [GS_OP_SUBTRACT] = GL_FUNC_SUBTRACT,
                                    [GS_OP_REVERSE_SUBTRACT] = GL_FUNC_REVERSE_SUBTRACT};

static GLenum gl_blend_factor(GsBlendFactor f, GsBlendC c) {
    GLenum fromC = c == GS_BLEND_C_SRC_ALPHA ? GL_SRC_ALPHA : c == GS_BLEND_C_DST_ALPHA ? GL_DST_ALPHA : GL_CONSTANT_COLOR;
    switch (f) {
    case GS_FACTOR_ZERO: return GL_ZERO;
    case GS_FACTOR_ONE: return GL_ONE;
    case GS_FACTOR_C: return fromC;
    case GS_FACTOR_ONE_MINUS_C: break;
    }
    return c == GS_BLEND_C_SRC_ALPHA ? GL_ONE_MINUS_SRC_ALPHA : c == GS_BLEND_C_DST_ALPHA ? GL_ONE_MINUS_DST_ALPHA
                                                                                         : GL_ONE_MINUS_CONSTANT_COLOR;
}

static GLuint sCurProgram;
static int sCurPipe = -1;
static GLuint sCurTex0, sCurTex1, sCurTex2;
static int sCurSamp0, sCurSamp1;
static int haveScissor = 0;
static int lastScissor[4];
static float lastBlendc = -1.0f;

static void apply_blend(GsBlend b) {
    int enabled = b.enabled;
    GsBlendDesc d;
    if (sHasIndexed) {
        if (enabled) { glEnablei(GL_BLEND, 0); glDisablei(GL_BLEND, 1); } else { glDisablei(GL_BLEND, 0); glDisablei(GL_BLEND, 1); }
    } else if (enabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    if (!enabled) { return; }
    d = GsBlend_Describe(b);
    glBlendFuncSeparate(gl_blend_factor(d.srcFactor, d.c), gl_blend_factor(d.dstFactor, d.c), GL_ONE, GL_ZERO);
    glBlendEquationSeparate(kGlBlendOp[d.op], GL_FUNC_ADD);
}

static void apply_masks(uint32_t wmask) {
    if (sHasIndexed) {
        glColorMaski(0, (wmask & GS_CC_R) != 0, (wmask & GS_CC_G) != 0, (wmask & GS_CC_B) != 0, (wmask & GS_CC_A) != 0);
        glColorMaski(1, (wmask & GS_CC_A) != 0, GL_FALSE, GL_FALSE, GL_FALSE);
    } else {
        glColorMask((wmask & GS_CC_R) != 0, (wmask & GS_CC_G) != 0, (wmask & GS_CC_B) != 0, (wmask & GS_CC_A) != 0);
    }
}

static void set_params(const void *p, size_t n) {
    glBindBuffer(GL_UNIFORM_BUFFER, sUboParams);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)n, p);
}

static void set_uniform_u(const Vu0Uniform *u) {
    glBindBuffer(GL_UNIFORM_BUFFER, sUboU);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Vu0Uniform), u);
}

static void set_pipe(int pipe) {
    Pipe *p;
    if (pipe == sCurPipe) { return; }
    sCurPipe = pipe;
    p = &sPipes[pipe];
    {
        GLuint prog = p->s.tablePass ? sDclutProg : sMainProg[p->s.noFragDepth][p->s.vertices];
        if (sCurProgram != prog) { sCurProgram = prog; glUseProgram(prog); }
    }
    apply_blend(p->s.blend);
    apply_masks(p->s.writeMask);
    if (p->s.tablePass) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    } else {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(p->s.depthWrite ? GL_TRUE : GL_FALSE);
        glDepthFunc(kGlDepthFunc[p->s.depthTest]);
    }
}

static void set_native(int effect) {
    sCurPipe = -1;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    if (sHasIndexed) { glEnablei(GL_BLEND, 0); glDisablei(GL_BLEND, 1); } else { glEnable(GL_BLEND); }
    if (effect == GS_NATIVE_ALPHA_KEY) {
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    } else {
        glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
        glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
    }
    if (sHasIndexed) {
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    } else {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    }
    if (sCurProgram != (effect == GS_NATIVE_ALPHA_KEY ? sKeyProg : sOutlineProg)) {
        sCurProgram = effect == GS_NATIVE_ALPHA_KEY ? sKeyProg : sOutlineProg;
        glUseProgram(sCurProgram);
    }
}

static void set_scissor(const int *sc) {
    if (!haveScissor || memcmp(sc, lastScissor, sizeof(lastScissor)) != 0) {
        glScissor(sc[0], sc[1], sc[2], sc[3]);
        memcpy(lastScissor, sc, sizeof(lastScissor));
        haveScissor = 1;
    }
}

static void set_textures(GLuint t0, int s0, GLuint t1, int s1, GLuint t2, int s2) {
    if (sCurTex0 != t0 || sCurSamp0 != s0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, t0);
        glBindSampler(0, sSamplers[s0]);
        sCurTex0 = t0;
        sCurSamp0 = s0;
    }
    if (sCurTex1 != t1 || sCurSamp1 != s1) {
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, t1);
        glBindSampler(1, sSamplers[s1]);
        sCurTex1 = t1;
        sCurSamp1 = s1;
    }
    if (t2 != 0 && sCurTex2 != t2) {
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, t2);
        glBindSampler(2, sSamplers[GS_SAMPLER_NEAREST_CLAMPED]);
        sCurTex2 = t2;
    }
}

static void bind_target(int i, int clear) {
    GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glBindFramebuffer(GL_FRAMEBUFFER, sTgFbo[i]);
    glDrawBuffers(2, bufs);
    glViewport(0, 0, GS_W * SCALE, GS_H * SCALE);
    if (clear) {
        /* as the Vulkan back end clears: colour and alpha 0 (alpha 1 only with the magenta aid), the alpha byte 0 */
        float col[4] = {0.0f, 0.0f, 0.0f, 0.0f}, aux[4] = {0.0f, 0.0f, 0.0f, 0.0f}, dep = 0.0f;
        if (getenv("BT3_GPU_MAGENTA") != NULL) { col[0] = 1.0f; col[2] = 1.0f; col[3] = 1.0f; }
        glDisable(GL_SCISSOR_TEST);
        /* a clear obeys the write masks of the draw before it: open them (the caller sets its pipeline again) */
        if (sHasIndexed) { glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); }
        else { glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); }
        glDepthMask(GL_TRUE);
        sCurPipe = -1;
        glClearBufferfv(GL_COLOR, 0, col);
        glClearBufferfv(GL_COLOR, 1, aux);
        glClearBufferfv(GL_DEPTH, 0, &dep);
        glEnable(GL_SCISSOR_TEST);
    }
}

void GsGl_Vsync(int on) { /* the settings window's "V-sync" */
    if (sCtx != NULL) {
        SDL_GL_SetSwapInterval(on != 0);
    }
}

#ifdef __EMSCRIPTEN__
/* ---- GPU time per frame (the browser page's Developer section) ------------------------------------------
   Where the browser has EXT_disjoint_timer_query_webgl2: a TIME_ELAPSED query around the frame's commands. A few
   queries go round and a result is read once it is available (the game never waits for one); a frame during which
   the GPU's clock was disturbed (GPU_DISJOINT) is dropped. The times go to the page's ring (web_ui.c). */
#define GL_TIME_ELAPSED_EXT 0x88BF
#define GL_GPU_DISJOINT_EXT 0x8FBB
#define GL_QUERY_RESULT 0x8866
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#define GL_TIMER_QUERIES 4
extern void Web_GpuSample(unsigned frame, double ms); /* port/src/web/web_ui.c */
static void (*glGenQueries)(GLsizei, GLuint *);
static void (*glBeginQuery)(GLenum, GLuint);
static void (*glEndQuery)(GLenum);
static void (*glGetQueryObjectuiv)(GLuint, GLenum, GLuint *);
static struct {
    GLuint id;
    int busy;
    unsigned frame;
} sTimer[GL_TIMER_QUERIES];
static int sTimerOn, sTimerActive = -1;
static unsigned sTimerSkipped; /* frames not timed: every query still waiting for its result */

static void timer_init(void) {
    int i;
    if ((getenv("BT3_GPU_TIMING") != NULL && atoi(getenv("BT3_GPU_TIMING")) == 0) || /* (=0: off, to measure its cost) */
        !Port_WebGLExtension("EXT_disjoint_timer_query_webgl2")) {
        return;
    }
    glGenQueries = (void (*)(GLsizei, GLuint *))SDL_GL_GetProcAddress("glGenQueries");
    glBeginQuery = (void (*)(GLenum, GLuint))SDL_GL_GetProcAddress("glBeginQuery");
    glEndQuery = (void (*)(GLenum))SDL_GL_GetProcAddress("glEndQuery");
    glGetQueryObjectuiv = (void (*)(GLuint, GLenum, GLuint *))SDL_GL_GetProcAddress("glGetQueryObjectuiv");
    if (glGenQueries == NULL || glBeginQuery == NULL || glEndQuery == NULL || glGetQueryObjectuiv == NULL) {
        fprintf(stderr, "bt3: gl: the timer-query extension is there but its functions are not: no GPU times\n");
        return;
    }
    for (i = 0; i < GL_TIMER_QUERIES; i++) {
        glGenQueries(1, &sTimer[i].id);
    }
    sTimerOn = 1;
}

/* Results that are ready, into the page's ring. */
static void timer_collect(void) {
    GLint disjoint = 0;
    int i;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint); /* (reading it also clears it) */
    for (i = 0; i < GL_TIMER_QUERIES; i++) {
        GLuint ready = 0, ns = 0;
        if (!sTimer[i].busy) {
            continue;
        }
        glGetQueryObjectuiv(sTimer[i].id, GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) {
            continue;
        }
        glGetQueryObjectuiv(sTimer[i].id, GL_QUERY_RESULT, &ns);
        if (!disjoint) {
            Web_GpuSample(sTimer[i].frame, (double)ns / 1e6);
        }
        sTimer[i].busy = 0;
    }
}

static void timer_begin(void) {
    int i;
    if (!sTimerOn) {
        return;
    }
    timer_collect();
    for (i = 0; i < GL_TIMER_QUERIES && sTimer[i].busy; i++) {
    }
    if (i == GL_TIMER_QUERIES) {
        sTimerSkipped++;
        return;
    }
    glBeginQuery(GL_TIME_ELAPSED_EXT, sTimer[i].id);
    sTimerActive = i;
}

static void timer_end(void) {
    if (sTimerActive < 0) {
        return;
    }
    glEndQuery(GL_TIME_ELAPSED_EXT);
    sTimer[sTimerActive].busy = 1;
    sTimer[sTimerActive].frame = gGsFrame;
    sTimerActive = -1;
}

/* 1 = frames are timed on the GPU; the frames left untimed (for the page's Developer section). */
int GsGl_GpuTiming(void) { return sTimerOn; }
unsigned GsGl_GpuTimingSkipped(void) { return sTimerSkipped; }
#define TIMER_INIT() timer_init()
#define TIMER_BEGIN() timer_begin()
#define TIMER_END() timer_end()
#else
#define TIMER_INIT()
#define TIMER_BEGIN()
#define TIMER_END()
#endif

/* ---- init --------------------------------------------------------------------------------------- */
static int gl_init(void) {
    static const uint32_t white = 0xFFFFFFFFu;
    int i;

    sWindow = GsDraw_WindowCreate(1);
    if (sWindow == NULL) { return 0; }
    if (getenv("BT3_GL_TRACE") != NULL) { fprintf(stderr, "gl: the window is there\n"); }
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); /* WebGL 2 */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    if (getenv("BT3_GPU_DEBUG") != NULL) { SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG); }
    if (getenv("BT3_GL_TRACE") != NULL) { fprintf(stderr, "gl: window made, creating the context\n"); }
    sCtx = SDL_GL_CreateContext(sWindow);
    if (getenv("BT3_GL_TRACE") != NULL) { fprintf(stderr, "gl: context %p (%s)\n", (void *)sCtx, sCtx == NULL ? SDL_GetError() : "ok"); }
    if (sCtx == NULL || !SDL_GL_MakeCurrent(sWindow, sCtx)) {
        fprintf(stderr, "bt3: gl: no GL 3.3 core context: %s\n", SDL_GetError());
        return 0;
    }
    SDL_GL_SetSwapInterval(Port_Setting("vsync", 0) != 0);
    if (getenv("BT3_GL_TRACE") != NULL) { fprintf(stderr, "gl: current, loading the functions\n"); }
    if (!load_gl()) { return 0; }
    fprintf(stderr, "bt3: GL renderer: %s (%s)\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    sHasIndexed = glEnablei != NULL && glColorMaski != NULL;
    /* Depth as the Vulkan back end has it: clip-space z in 0..w stored as z / w, and (SDL GPU's default, depth
       clip off) nothing cut at the near and far planes, the depth clamped to 0..1 instead. */
#ifdef __EMSCRIPTEN__
    if (getenv("BT3_GL_NOCLIPCONTROL") == NULL) {
        glClipControl = (void (*)(GLenum, GLenum))Port_WebGLProc("glClipControl"); /* EXT_clip_control */
    }
#else
    if (getenv("BT3_GL_NOCLIPCONTROL") == NULL && SDL_GL_ExtensionSupported("GL_ARB_clip_control")) {
        glClipControl = (void (*)(GLenum, GLenum))SDL_GL_GetProcAddress("glClipControl");
    }
#endif
    sHasClipControl = glClipControl != NULL;
    if (sHasClipControl) { glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE); }
#ifdef __EMSCRIPTEN__
    if (Port_WebGLExtension("EXT_depth_clamp")) {
        glEnable(GL_DEPTH_CLAMP);
    } else {
        fprintf(stderr, "bt3: gl: no EXT_depth_clamp: geometry beyond the far plane is cut instead of clamped\n");
    }
    fprintf(stderr, "bt3: gl: WebGL extensions: indexed blend/mask %s, clip control %s, depth clamp %s\n",
            glEnablei != NULL ? "yes" : "NO", sHasClipControl ? "yes" : "NO", Port_WebGLExtension("EXT_depth_clamp") ? "yes" : "NO");
#else
    glEnable(GL_DEPTH_CLAMP);
#endif
    if (!sHasIndexed) { fprintf(stderr, "bt3: gl: no per-attachment blend/mask (GL 3.0 indexed): the aux byte may be wrong\n"); }

    sMainProg[0][GS_VERTS_GS] = make_program(kGsVertGlsl, kGsFragGlsl, "gs", sGsSamplers, 2);
    sMainProg[0][GS_VERTS_PROGRAM0] = make_program(kVu0VertGlsl, kGsFragGlsl, "vu0", sGsSamplers, 2);
    sMainProg[0][GS_VERTS_PROGRAM4] = make_program(kVu4VertGlsl, kGsFragGlsl, "vu4", sGsSamplers, 2);
    sMainProg[0][GS_VERTS_PROGRAM6] = make_program(kVu6VertGlsl, kGsFragGlsl, "vu6", sGsSamplers, 2);
    sMainProg[1][GS_VERTS_GS] = make_program(kGsVertGlsl, kGsnzFragGlsl, "gs nz", sGsSamplers, 2);
    sMainProg[1][GS_VERTS_PROGRAM0] = make_program(kVu0VertGlsl, kGsnzFragGlsl, "vu0 nz", sGsSamplers, 2);
    sMainProg[1][GS_VERTS_PROGRAM4] = make_program(kVu4VertGlsl, kGsnzFragGlsl, "vu4 nz", sGsSamplers, 2);
    sMainProg[1][GS_VERTS_PROGRAM6] = make_program(kVu6VertGlsl, kGsnzFragGlsl, "vu6 nz", sGsSamplers, 2);
    sOutlineProg = make_program(kFxVertGlsl, kOutlineFragGlsl, "outline", sOneSampler, 1);
    sKeyProg = make_program(kFxVertGlsl, kAlphakeyFragGlsl, "alphakey", sOneSampler, 1);
    sDclutProg = make_program(kFxVertGlsl, kDclutFragGlsl, "dclut", sDclutSamplers, 3);
    sPresentProg = make_present_program();

    glGenBuffers(1, &sVboGs);
    glBindBuffer(GL_ARRAY_BUFFER, sVboGs);
    glBufferData(GL_ARRAY_BUFFER, MAX_VERTS * sizeof(Vtx), NULL, GL_STREAM_DRAW);
    glGenVertexArrays(1, &sVaoGs);
    glBindVertexArray(sVaoGs);
    glBindBuffer(GL_ARRAY_BUFFER, sVboGs);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), (void *)12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)16);

    glGenBuffers(1, &sVboVu);
    glBindBuffer(GL_ARRAY_BUFFER, sVboVu);
    glBufferData(GL_ARRAY_BUFFER, MAX_VU_VERTS * VU_VERT_BYTES, NULL, GL_STREAM_DRAW);
    glGenVertexArrays(1, &sVaoVu);
    glBindVertexArray(sVaoVu);
    glBindBuffer(GL_ARRAY_BUFFER, sVboVu);
    for (i = 0; i < 3; i++) {
        glEnableVertexAttribArray((GLuint)i);
        glVertexAttribPointer((GLuint)i, 4, GL_FLOAT, GL_FALSE, (GLsizei)VU_VERT_BYTES, (void *)(uintptr_t)(i * 4 * sizeof(float)));
    }
    glGenBuffers(1, &sIboVu); /* the triangles' indices (gsVuIdx); the binding is part of the vertex array's state */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sIboVu);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, MAX_VU_IDX * sizeof(uint32_t), NULL, GL_STREAM_DRAW);
    glBindVertexArray(0);

    glGenBuffers(1, &sUboU);
    glBindBuffer(GL_UNIFORM_BUFFER, sUboU);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Vu0Uniform), NULL, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, sUboU);
    glGenBuffers(1, &sUboParams);
    glBindBuffer(GL_UNIFORM_BUFFER, sUboParams);
    glBufferData(GL_UNIFORM_BUFFER, 80, NULL, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, sUboParams);

    for (i = 0; i < GS_SAMPLER_COUNT; i++) {
        GLint filter = (i & GS_SAMPLER_LINEAR) ? GL_LINEAR : GL_NEAREST;
        glGenSamplers(1, &sSamplers[i]);
        glSamplerParameteri(sSamplers[i], GL_TEXTURE_MAG_FILTER, filter);
        glSamplerParameteri(sSamplers[i], GL_TEXTURE_MIN_FILTER, (i & GS_SAMPLER_MIPMAPS) ? GL_LINEAR_MIPMAP_LINEAR : filter);
        glSamplerParameteri(sSamplers[i], GL_TEXTURE_WRAP_S, (i & GS_SAMPLER_CLAMP_U) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glSamplerParameteri(sSamplers[i], GL_TEXTURE_WRAP_T, (i & GS_SAMPLER_CLAMP_V) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    }
    glGenTextures(1, &sWhite);
    glBindTexture(GL_TEXTURE_2D, sWhite);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    copies_create();
    glGenFramebuffers(1, &sScratchRead);
    glGenFramebuffers(1, &sScratchDraw);

    glDisable(GL_CULL_FACE);
    TIMER_INIT();
    glEnable(GL_SCISSOR_TEST);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glViewport(0, 0, GS_W * SCALE, GS_H * SCALE);
    /* the overlay (F1's settings, the online window) here too: ImGui's OpenGL3 backend, with the context current */
    if (!Ui_Init(sWindow, NULL, (void *)sCtx)) {
        fprintf(stderr, "bt3: gl: the settings window is not available\n");
    }
    return 1;
}

/* A strip of pre-rendered names (the added stages / songs) as a texture for the overlay (ui.cpp). */
unsigned GsGl_StripTexture(const void *rgba, int w, int h) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return (unsigned)id;
}

void GsGl_StripTextureFree(unsigned id) {
    GLuint t = (GLuint)id;
    glDeleteTextures(1, &t);
}


/* ---- the frame ---------------------------------------------------------------------------------- */
static void frame_end(void) {
    int cur = -1, best = -1, i;
    uint32_t n;
    struct { int32_t mode[4]; float misc[4]; float rect[4]; float orig[4]; float extra[4]; } fu, lastFu;
    int haveFu = 0;

    GsDraw_PollEvents();
    if (getenv("BT3_GS_VERBOSE") != NULL && gGsFrame % 30 == 0) {
        fprintf(stderr, "gpu: frame %u: %u draws, %u vertices, %d targets, %d textures, %d pipelines, %u primitives of PS2-only passes dropped, %u native effects\n",
                gGsFrame, gsDrawCount, gsVertCount + gsVuVertCount, gsTargetCount, gsTexCount, sPipeCount, gsSkipped, gsNative);
    }
    if (getenv("BT3_GPU_CUT") != NULL && (int)gGsFrame == atoi(getenv("BT3_GPU_CUT")) && strchr(getenv("BT3_GPU_CUT"), ':') != NULL) {
        uint32_t c = (uint32_t)atoi(strchr(getenv("BT3_GPU_CUT"), ':') + 1);
        fprintf(stderr, "cut: frame %u has %u draws, keeping %u\n", gGsFrame, gsDrawCount, c < gsDrawCount ? c : gsDrawCount);
        if (c < gsDrawCount) { gsDrawCount = c; }
    }
    if (getenv("BT3_GPU_TAIL") != NULL && (int)gGsFrame == atoi(getenv("BT3_GPU_TAIL"))) {
        uint32_t k;
        for (k = gsDrawCount > 14 ? gsDrawCount - 14 : 0; k < gsDrawCount; k++) {
            const GsDraw *d = &gsDraws[k];
            fprintf(stderr, "tail: draw %u native %d target %d vu %d first %u count %u pipeline key %08x\n",
                    k, d->native, d->target, d->vu, d->first, d->count, d->native ? 0 : sPipes[d->pipeline].key);
        }
    }

    TIMER_BEGIN(); /* (the browser build: the frame's GPU time, from its uploads to its present) */
    if (gsVertCount != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, sVboGs);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(gsVertCount * sizeof(Vtx)), gsVerts);
    }
    if (gsVuVertCount != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, sVboVu);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(gsVuVertCount * VU_VERT_BYTES), gsVuVerts);
    }
    if (gsVuIdxCount != 0) {
        glBindVertexArray(sVaoVu);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)(gsVuIdxCount * sizeof(uint32_t)), gsVuIdx);
        glBindVertexArray(0);
    }
    for (i = 0; i < GS_DISPLAY_BUFFERS; i++) {
        uint32_t *px;
        if (!(gGsFbUploads & (1u << i))) { continue; }
        if (sFbTex[i] == 0) {
            glGenTextures(1, &sFbTex[i]);
            glBindTexture(GL_TEXTURE_2D, sFbTex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GS_W, GS_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glGenFramebuffers(1, &sFbFbo[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, sFbFbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sFbTex[i], 0);
        }
        px = malloc((size_t)GS_W * GS_H * 4);
        GsDraw_DisplayBufferPixels(i, px);
        glBindTexture(GL_TEXTURE_2D, sFbTex[i]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GS_W, GS_H, GL_RGBA, GL_UNSIGNED_BYTE, px);
        free(px);
    }
    gGsFbUploads = 0;

    glBindVertexArray(sVaoGs);
    sCurPipe = -1;
    sCurProgram = 0;
    sCurTex0 = sCurTex1 = sCurTex2 = 0;
    sCurSamp0 = sCurSamp1 = -1;
    haveScissor = 0;
    lastBlendc = -1.0f;
    /* The present draws its full-screen triangle with the scissor test off (see the end of this function); turn it
       back on for the frame's own draws, or every partial scissor (the split screen's halves, the HUD, the
       fighters' shadows) is ignored and the two views overlap across the whole screen. */
    glEnable(GL_SCISSOR_TEST);
    for (n = 0; n < gsDrawCount; n++) {
        const GsDraw *d = &gsDraws[n];
        if (d->native >= GS_NATIVE_FB_UPLOAD) {
            GLenum one[1] = {GL_COLOR_ATTACHMENT0};
            if (sFbTex[d->native - GS_NATIVE_FB_UPLOAD] == 0) { continue; }
            glDisable(GL_SCISSOR_TEST); /* glBlitFramebuffer honours the scissor test: the copy must be whole */
            glBindFramebuffer(GL_READ_FRAMEBUFFER, sFbFbo[d->native - GS_NATIVE_FB_UPLOAD]);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sTgFbo[d->target]);
            glDrawBuffers(1, one);
            glBlitFramebuffer(0, 0, GS_W, GS_H, 0, 0, GS_W * SCALE, GS_H * SCALE, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            glEnable(GL_SCISSOR_TEST);
            haveScissor = 0;
            gsTargets[d->target].cleared = 1;
            cur = -1;
            continue;
        }
        if (d->native == GS_NATIVE_COPY_DATE || d->native == GS_NATIVE_COPY_IDS) {
            GLuint dst = d->native == GS_NATIVE_COPY_DATE ? sDateCopy : sAuxCopy;
            GLenum one[1] = {GL_COLOR_ATTACHMENT0};
            int rx = 0, ry = 0, rw = GS_W * SCALE, rh = GS_H * SCALE;
            if (!gsTargets[d->target].cleared) { continue; }
            /* the DATE copy: only what the draws that follow read (gs_draw.c); rows as in the Vulkan back end */
            if (d->native == 5 && !GsDraw_DateRegion(n, &rx, &ry, &rw, &rh)) { continue; }
            /* The copy of the frame's alpha ids (the outline and the DATE passes read it). glBlitFramebuffer is
               clipped by the scissor test, so with a partial scissor in force (the split screen's halves) only
               part of the alpha would be copied and the outline would draw a stale silhouette; turn it off for
               the blit. (Vulkan's copy pass is not affected, which is why only the GL back end showed it.) */
            glDisable(GL_SCISSOR_TEST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, sScratchRead);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sTgAux[d->target], 0);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sScratchDraw);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);
            glDrawBuffers(1, one);
            glBlitFramebuffer(rx, ry, rx + rw, ry + rh, rx, ry, rx + rw, ry + rh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glEnable(GL_SCISSOR_TEST);
            haveScissor = 0;
            cur = -1;
            continue;
        }
        if (d->native == GS_NATIVE_TABLE_PASS) {
            float params[4] = {(float)d->src, d->misc[0], 0.0f, 0.0f};
            if (!gsTargets[d->target].cleared) { continue; }
#ifdef __EMSCRIPTEN__
            /* the target without its depth attachment (sTgFboNoDepth): the next primitive binds the whole target */
            bind_target(d->target, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, sTgFboNoDepth[d->target]);
            cur = -1;
#else
            if (cur != d->target) { bind_target(d->target, 0); cur = d->target; }
#endif
            set_pipe(d->pipeline);
            set_textures(sTgDep[d->target], GS_SAMPLER_NEAREST_CLAMPED, sAuxCopy, GS_SAMPLER_NEAREST_CLAMPED, (GLuint)d->tex,
                         GS_SAMPLER_NEAREST_CLAMPED);
            set_params(params, 16);
            haveFu = 0; /* the Params block now holds this pass's: the next primitive sends its own again */
            if (d->blendc != lastBlendc) { glBlendColor(d->blendc, d->blendc, d->blendc, d->blendc); lastBlendc = d->blendc; }
            set_scissor(d->scissor);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            continue;
        }
        if (d->native) {
            float params[4];
            if (!gsTargets[d->target].cleared) { continue; }
            if (cur != d->target) { bind_target(d->target, 0); cur = d->target; }
            set_native(d->native);
            set_textures(sAuxCopy, GS_SAMPLER_NEAREST_CLAMPED, 0, GS_SAMPLER_NEAREST_CLAMPED, 0, GS_SAMPLER_NEAREST_CLAMPED);
            params[0] = 1.0f / (float)GS_W;
            params[1] = 1.0f / (float)GS_H;
            params[2] = GS_OUTLINE_DARKNESS;
            params[3] = getenv("BT3_FX_DEBUG") != NULL ? 1.0f : 0.0f;
            set_params(params, 16);
            haveFu = 0;
            set_scissor(d->scissor);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            continue;
        }
        /* an ordinary primitive */
        if (cur != d->target) {
            bind_target(d->target, !gsTargets[d->target].cleared);
            gsTargets[d->target].cleared = 1;
            cur = d->target;
            haveFu = 0;
            sCurPipe = -1;
            haveScissor = 0;
        } else if (!gsTargets[d->target].cleared) {
            gsTargets[d->target].cleared = 1;
        }
        set_pipe(d->pipeline);
        glBindVertexArray(d->vu ? sVaoVu : sVaoGs);
        if (d->vu) { set_uniform_u(&gsVuUni[d->uniform]); }
        memset(&fu, 0, sizeof(fu));
        memcpy(fu.mode, d->mode, sizeof(fu.mode));
        memcpy(fu.misc, d->misc, sizeof(fu.misc));
        memcpy(fu.rect, d->rect, sizeof(fu.rect));
        memcpy(fu.orig, d->orig, sizeof(fu.orig));
        fu.extra[0] = GsDraw_DepthMax(d);
        if (!haveFu || memcmp(&fu, &lastFu, sizeof(fu)) != 0) {
            set_params(&fu, sizeof(fu));
            memcpy(&lastFu, &fu, sizeof(fu));
            haveFu = 1;
        }
        set_textures((GLuint)d->tex, d->sampler, sDateCopy, GS_SAMPLER_NEAREST_CLAMPED, 0, GS_SAMPLER_NEAREST_CLAMPED);
        if (d->blendc != lastBlendc) { glBlendColor(d->blendc, d->blendc, d->blendc, d->blendc); lastBlendc = d->blendc; }
        set_scissor(d->scissor);
        if (d->vu) {
            glDrawElements(GL_TRIANGLES, (GLsizei)d->count, GL_UNSIGNED_INT, (const void *)(uintptr_t)(d->first * sizeof(uint32_t)));
        } else {
            glDrawArrays(kGlTopology[sPipes[d->pipeline].s.topology], (GLint)d->first, (GLsizei)d->count);
        }
    }

    /* the shown buffer: the one sceGsSwapDBuff set up for this frame, else the first target */
    best = gGsMainFbp >= 0 ? GsDraw_TargetGet((uint32_t)gGsMainFbp, 0) : -1;
    for (i = 0; best < 0 && i < gsTargetCount; i++) { best = i; }
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    {
        int sw, sh;
        SDL_GetWindowSize(sWindow, &sw, &sh);
        SDL_GetWindowSizeInPixels(sWindow, &sw, &sh); /* the drawable's real pixels (differs from the window on HiDPI / fullscreen) */
        glViewport(0, 0, sw, sh);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        if (best >= 0 && gsTargets[best].cleared) {
            int w, h, dx, dy;
            GsDraw_PictureSize(sw, sh, &w, &h);
            dx = (sw - w) / 2;
            dy = (sh - h) / 2;
            if (getenv("BT3_GS_VERBOSE") != NULL) {
                int ww, wh;
                SDL_GetWindowSize(sWindow, &ww, &wh);
                fprintf(stderr, "gl: present window %dx%d pixels %dx%d aspect %d -> picture %dx%d at (%d,%d)\n", ww, wh, sw, sh, Port_AspectMilli(), w, h, dx, dy);
            }
            {   /* where the picture is in the window, for the overlay's stage and song names (ui.cpp), counted from
                   the top as the overlay does */
                extern volatile int gUiPresentX, gUiPresentY, gUiPresentW, gUiPresentH;
                gUiPresentX = dx;
                gUiPresentY = sh - dy - h;
                gUiPresentW = w;
                gUiPresentH = h;
            }
            glViewport(dx, dy, w, h);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glUseProgram(sPresentProg);
            glBindVertexArray(sVaoGs); /* core profile needs a bound VAO to draw (the present uses no attributes) */
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sTgCol[best]);
            glBindSampler(0, sSamplers[GS_SAMPLER_LINEAR_CLAMPED]);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }
    /* BT3_GL_WINSHOT=<n>: dump the window's own framebuffer every n frames (what the user sees) */
    if (getenv("BT3_GL_WINSHOT") != NULL && atoi(getenv("BT3_GL_WINSHOT")) > 0 && (int)gGsFrame % atoi(getenv("BT3_GL_WINSHOT")) == 0) {
        int sw, sh, x, y;
        uint8_t *wp;
        char nm[64];
        FILE *wf;
        SDL_GetWindowSize(sWindow, &sw, &sh);
        wp = malloc((size_t)sw * sh * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, sw, sh, GL_RGBA, GL_UNSIGNED_BYTE, wp);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        snprintf(nm, sizeof(nm), "port/build/shots/window_%05u.ppm", gGsFrame);
        wf = fopen(nm, "wb");
        if (wf != NULL) {
            fprintf(wf, "P6\n%d %d\n255\n", sw, sh);
            for (y = sh - 1; y >= 0; y--) {
                for (x = 0; x < sw; x++) { fwrite(&wp[(y * sw + x) * 4], 1, 3, wf); }
            }
            fclose(wf);
        }
        free(wp);
    }
    TIMER_END();
    Ui_DrawGL(); /* the overlay, over the presented picture */
    SDL_GL_SwapWindow(sWindow);
    if (getenv("BT3_GS_VERBOSE") != NULL) {
        GLenum e = glGetError();
        if (e != GL_NO_ERROR) { fprintf(stderr, "bt3: gl: frame error 0x%x\n", e); }
    }

    /* BT3_SHOT=<n>: read the shown buffer back to port/build/shots/gpu_NNNNN.ppm (rows come back in texture
       order: the first row is the scene's top, as in the Vulkan back end's download) */
    if (best >= 0 && gsTargets[best].cleared && GsDraw_ShotDue()) {
        GLsizei w = GS_DISPLAY_W * SCALE, h = GS_DISPLAY_H * SCALE;
        int aux = GsDraw_ShotAux();
        uint8_t *px = malloc((size_t)w * h * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sTgFbo[best]);
        glReadBuffer(aux ? GL_COLOR_ATTACHMENT1 : GL_COLOR_ATTACHMENT0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, aux ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE, px);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        GsDraw_ShotWrite(px, w, h, aux);
        free(px);
    }

    GsDraw_FrameReset();
}

/* Target i's picture, reduced to the GS's display area (as the Vulkan back end's). */
static int gl_target_read(int i, uint8_t *rgba) {
    GLsizei w = GS_DISPLAY_W * SCALE, h = GS_DISPLAY_H * SCALE; /* (the picture's part of the target, as the screenshot's) */
    uint8_t *px;

    if (sTgFbo[i] == 0 || (px = malloc((size_t)w * h * 4)) == NULL) {
        return 0;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sTgFbo[i]);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    GsDraw_DisplayReduce(px, rgba); /* (rows come back in texture order: the first is the scene's top) */
    free(px);
    return 1;
}

static void gl_target_copy(int src, int dst) {
    GLenum one[1] = {GL_COLOR_ATTACHMENT0};
    if (sTgCol[src] == 0 || sTgCol[dst] == 0) {
        return;
    }
    glDisable(GL_SCISSOR_TEST); /* (the copy must be whole; frame_end sets its own state when it runs) */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sScratchRead);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sTgCol[src], 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sScratchDraw);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sTgCol[dst], 0);
    glDrawBuffers(1, one);
    glBlitFramebuffer(0, 0, GS_W * SCALE, GS_H * SCALE, 0, 0, GS_W * SCALE, GS_H * SCALE, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

GsBackend sGlBackend = {
    "opengl",
    gl_init,
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
    gl_target_read,
    gl_target_copy,
};
