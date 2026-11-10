// NanoVG (third_party/nanovg, zlib licence) with its GL2 backend, drawing through the client's GL slots.
// The GL3 backend opens with "#version 150 core", which needs an OpenGL 3.2 context; the game's own floor is
// OpenGL 2.1, so the same overlay has to draw from GLSL 1.10 too. This backend needs no vertex array objects,
// and the framebuffer it renders into is reached through the EXT entry points aliased in gl_slots.cpp.
#define SLINKY_GL_MACROS
#include "slinky/gl_slots.h"
#include "nanovg.h"

// glcorearb.h carries the core profile only: NanoVG's GL2 texture path writes these two compatibility enums,
// which are fixed constants of GL 1.4 and accepted by every desktop 2.1 driver.
#ifndef GL_LUMINANCE
#define GL_LUMINANCE 0x1909
#endif
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

#define NANOVG_GL2_IMPLEMENTATION
// nanovg_gl_utils.h grants GL2 an FBO on OS X only and compiles the helpers out everywhere else. The EXT
// aliases make the same calls land on a 2.1 driver, which is the context this backend exists for.
#define NANOVG_FBO_VALID 1
// the GL3 unit already defines the plain names; each backend keeps its own image-handle lookup
#define nvgluCreateFramebuffer nvgluCreateFramebufferGL2
#define nvgluDeleteFramebuffer nvgluDeleteFramebufferGL2
#define nvgluBindFramebuffer nvgluBindFramebufferGL2
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"
