#include "gfx.hpp"
#define GLM_FORCE_RADIANS 1
#include "gtx/matrix_transform_2d.hpp"

#include <stdio.h>
#include <string.h>
#include <math.h>
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <VG/vgu.h>
#include <vector>
#include <cmath>
#include <fstream>
#include <iostream>

namespace otto {

using namespace glm;

struct Context {
  VGPath scratchPath = 0;

  std::vector<mat3> transformStack = { mat3() };

  VGFont font = VG_INVALID_HANDLE;
  std::unique_ptr<char[]> fontData;
  stbtt_fontinfo fontInfo;
  float fontAscent, fontDescent, fontLineGap;
  float fontSize = 14.0f;

  uint32_t textAlign = ALIGN_LEFT | ALIGN_BASELINE;
};

static Context ctx;

static void unpackRGB(uint32_t color, float &r, float &g, float &b) {
  r = (color         & 0xff) / 255.0f;
  g = ((color >> 8)  & 0xff) / 255.0f;
  b = ((color >> 16) & 0xff) / 255.0f;
}

static VGColorRampSpreadMode fromNSVG(NSVGspreadType spread) {
  switch (spread) {
    case NSVG_SPREAD_REFLECT: return VG_COLOR_RAMP_SPREAD_REFLECT;
    case NSVG_SPREAD_REPEAT: return VG_COLOR_RAMP_SPREAD_REPEAT;
    case NSVG_SPREAD_PAD:
    default:
      return VG_COLOR_RAMP_SPREAD_PAD;
  }
}

static VGJoinStyle fromNSVG(NSVGlineJoin join) {
  switch (join) {
    case NSVG_JOIN_ROUND: return VG_JOIN_ROUND;
    case NSVG_JOIN_BEVEL: return VG_JOIN_BEVEL;
    case NSVG_JOIN_MITER:
    default:
      return VG_JOIN_MITER;
  }
}

static VGCapStyle fromNSVG(NSVGlineCap cap) {
  switch (cap) {
    case NSVG_CAP_ROUND: return VG_CAP_ROUND;
    case NSVG_CAP_SQUARE: return VG_CAP_SQUARE;
    case NSVG_CAP_BUTT:
    default:
      return VG_CAP_BUTT;
  }
}

static VGPaint createPaintFromRGBA(float r, float g, float b, float a) {
  auto paint = vgCreatePaint();
  VGfloat color[] = { r, g, b, a };
  vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, color);
  return paint;
}

static VGPaint createPaintFromNSVGpaint(const NSVGpaint &svgPaint, float opacity = 1.0f) {
  auto paint = vgCreatePaint();

  if (svgPaint.type == NSVG_PAINT_COLOR) {
    VGfloat color[4];
    unpackRGB(svgPaint.color, color[0], color[1], color[2]);
    color[3] = opacity;
    vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
    vgSetParameterfv(paint, VG_PAINT_COLOR, 4, color);
  }

  // TODO(ryan): We don't need gradients yet, but I got about halfway through
  // implementing them. Finish this up when we actually need them.

  // else if (svgPaint.type == NSVG_PAINT_LINEAR_GRADIENT ||
  //          svgPaint.type == NSVG_PAINT_RADIAL_GRADIENT) {
  //   const auto &grad = *svgPaint.gradient;

  //   if (svgPaint.type == NSVG_PAINT_LINEAR_GRADIENT) {
  //     VGfloat points[] = {
  //     };
  //     vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_LINEAR_GRADIENT);
  //     vgSetParameterfv(paint, VG_PAINT_LINEAR_GRADIENT, 4, points);
  //   }

  //   VGfloat stops[5 * grad.nstops];
  //   for (int i = 0; i < grad.nstops; ++i) {
  //     auto s = &stops[i * 5];
  //     s[0] = grad.stops[i].offset;
  //     unpackRGB(grad.stops[i].color, s[1], s[2], s[3]);
  //     s[4] = 1.0f; // TODO(ryan): Are NSVG gradients always opaque?
  //   }

  //   vgSetParameteri(paint, VG_PAINT_COLOR_RAMP_SPREAD_MODE, fromNSVG(static_cast<NSVGspreadType>(grad.spread)));
  //   vgSetParameteri(paint, VG_PAINT_COLOR_RAMP_PREMULTIPLIED, false);
  //   vgSetParameterfv(paint, VG_PAINT_COLOR_RAMP_STOPS, 5 * grad.nstops, stops);
  // }

  return paint;
}


void strokePaint(const NSVGpaint &svgPaint, float opacity) {
  auto paint = createPaintFromNSVGpaint(svgPaint, opacity);
  vgSetPaint(paint, VG_STROKE_PATH);
  vgDestroyPaint(paint);
}

void fillPaint(const NSVGpaint &svgPaint, float opacity) {
  auto paint = createPaintFromNSVGpaint(svgPaint, opacity);
  vgSetPaint(paint, VG_FILL_PATH);
  vgDestroyPaint(paint);
}

void strokeColor(float r, float g, float b, float a) {
  auto paint = createPaintFromRGBA(r, g, b, a);
  vgSetPaint(paint, VG_STROKE_PATH);
  vgDestroyPaint(paint);
}
void strokeColor(const vec4 &color) {
  strokeColor(color.r, color.g, color.b, color.a);
}
void strokeColor(const vec3 &color) {
  strokeColor(color.r, color.g, color.b);
}

void fillColor(float r, float g, float b, float a) {
  auto paint = createPaintFromRGBA(r, g, b, a);
  vgSetPaint(paint, VG_FILL_PATH);
  vgDestroyPaint(paint);
}
void fillColor(const vec4 &color) {
  fillColor(color.r, color.g, color.b, color.a);
}
void fillColor(const vec3 &color) {
  fillColor(color.r, color.g, color.b);
}

void strokeWidth(VGfloat width) {
  vgSetf(VG_STROKE_LINE_WIDTH, width);
}

void strokeCap(VGCapStyle cap) {
  vgSeti(VG_STROKE_CAP_STYLE, cap);
}

void strokeJoin(VGJoinStyle join) {
  vgSeti(VG_STROKE_JOIN_STYLE, join);
}


void moveTo(VGPath path, float x, float y) {
  VGubyte segs[] = { VG_MOVE_TO };
  VGfloat coords[] = { x, y };
  vgAppendPathData(path, 1, segs, coords);
}

void lineTo(VGPath path, float x, float y) {
  VGubyte segs[] = { VG_LINE_TO };
  VGfloat coords[] = { x, y };
  vgAppendPathData(path, 1, segs, coords);
}

void cubicTo(VGPath path, float x1, float y1, float x2, float y2, float x3, float y3) {
  VGubyte segs[] = { VG_CUBIC_TO };
  VGfloat coords[] = { x1, y1, x2, y2, x3, y3 };
  vgAppendPathData(path, 1, segs, coords);
}

static float RAD_TO_DEG = 180.0f / M_PI;

void arc(VGPath path, float x, float y, float w, float h, float startAngle, float endAngle) {
  float as = startAngle * RAD_TO_DEG;
  float ae = (endAngle - startAngle) * RAD_TO_DEG;
  vguArc(path, x, y, w, h, as, ae, VGU_ARC_OPEN);
}

void rect(VGPath path, float x, float y, float w, float h) {
  static const VGubyte segs[] = { VG_MOVE_TO, VG_LINE_TO, VG_LINE_TO, VG_LINE_TO, VG_CLOSE_PATH };
  float x2 = x + w, y2 = y + h;
  VGfloat coords[] = { x, y, x, y2, x2, y2, x2, y };
  vgAppendPathData(path, 5, segs, coords);
}


//
// Scratch Path Operators
//

void beginPath() {
  if (ctx.scratchPath == VG_INVALID_HANDLE) {
    ctx.scratchPath = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F, 1.0f, 0.0f, 0, 0,
                                   VG_PATH_CAPABILITY_ALL);
  }
  else {
    vgClearPath(ctx.scratchPath, VG_PATH_CAPABILITY_ALL);
  }
}

void moveTo(float x, float y) {
  moveTo(ctx.scratchPath, x, y);
}
void moveTo(const vec2 &pos) {
  moveTo(pos.x, pos.y);
}

void lineTo(float x, float y) {
  lineTo(ctx.scratchPath, x, y);
}
void lineTo(const vec2 &pos) {
  lineTo(pos.x, pos.y);
}

void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
  cubicTo(ctx.scratchPath, x1, y1, x2, y2, x3, y3);
}
void cubicTo(const vec2 &p1, const vec2 p2, const vec2 &p3) {
  cubicTo(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
}

void arc(float cx, float cy, float w, float h, float angleStart, float angleEnd) {
  arc(ctx.scratchPath, cx, cy, w, h, angleStart, angleEnd);
}
void arc(const vec2 &ctr, const vec2 &size, float angleStart, float angleEnd) {
  arc(ctr.x, ctr.y, size.x, size.y, angleStart, angleEnd);
}

void circle(float cx, float cy, float radius) {
  auto d = radius * 2.0f;
  arc(cx, cy, d, d, 0.0f, M_PI * 2.0f);
}
void circle(const vec2 &ctr, float radius) {
  circle(ctr.x, ctr.y, radius);
}

void rect(float x, float y, float w, float h) {
  rect(ctx.scratchPath, x, y, w, h);
}
void rect(const glm::vec2 &pos, const glm::vec2 &size) {
  rect(pos.x, pos.y, size.x, size.y);
}


void fill() {
  vgDrawPath(ctx.scratchPath, VG_FILL_PATH);
}

void stroke() {
  vgDrawPath(ctx.scratchPath, VG_STROKE_PATH);
}

void fillAndStroke() {
  vgDrawPath(ctx.scratchPath, VG_FILL_PATH | VG_STROKE_PATH);
}


void clearColor(float r, float g, float b, float a) {
  VGfloat color[] = { r, g, b, a };
  vgSetfv(VG_CLEAR_COLOR, 4, color);
}
void clearColor(const vec4 &color) {
  clearColor(color.r, color.g, color.b, color.a);
}
void clearColor(const vec3 &color) {
  clearColor(color.r, color.g, color.b);
}

void clear(float x, float y, float w, float h) {
  vgClear(x, y, w, h);
}


//
// SVG
//

void draw(const NSVGimage &svg) {
  for (auto shape = svg.shapes; shape != NULL; shape = shape->next) {
    bool hasStroke = shape->stroke.type != NSVG_PAINT_NONE;
    bool hasFill = shape->fill.type != NSVG_PAINT_NONE;

    if (hasFill) {
      fillPaint(shape->fill, shape->opacity);
    }

    if (hasStroke) {
      strokeWidth(shape->strokeWidth);
      strokeJoin(fromNSVG(static_cast<NSVGlineJoin>(shape->strokeLineJoin)));
      strokeCap(fromNSVG(static_cast<NSVGlineCap>(shape->strokeLineCap)));
      strokePaint(shape->stroke, shape->opacity);
    }

    auto vgPath = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F, 1.0f, 0.0f, 0, 0, VG_PATH_CAPABILITY_ALL);

    for (auto path = shape->paths; path != NULL; path = path->next) {
      moveTo(vgPath, path->pts[0], path->pts[1]);
      for (int i = 0; i < path->npts - 1; i += 3) {
        float* p = &path->pts[i * 2];
        cubicTo(vgPath, p[2], p[3], p[4], p[5], p[6], p[7]);
      }
    }

    vgDrawPath(vgPath, (hasFill   ? VG_FILL_PATH   : 0) |
                       (hasStroke ? VG_STROKE_PATH : 0));
    vgDestroyPath(vgPath);
  }
}

void draw(const NSVGimage *img) {
  draw(*img);
}


//
// Stack
//

static void loadMatrix() {
  vgLoadMatrix(&ctx.transformStack.back()[0][0]);
}


void pushTransform() {
  ctx.transformStack.push_back(ctx.transformStack.back());
}

void popTransform() {
  ctx.transformStack.pop_back();
  loadMatrix();
}

void setTransform(const mat3 &xf) {
  ctx.transformStack.back() = xf;
  loadMatrix();
}

const mat3 getTransform() {
  return ctx.transformStack.back();
}


void translate(const vec2 &vec) {
  ctx.transformStack.back() = translate(ctx.transformStack.back(), vec);
  loadMatrix();
}
void translate(float x, float y) {
  translate(vec2(x, y));
}

void rotate(float radians) {
  ctx.transformStack.back() = rotate(ctx.transformStack.back(), radians);
  loadMatrix();
}

void scale(const vec2 &vec) {
  ctx.transformStack.back() = scale(ctx.transformStack.back(), vec);
  loadMatrix();
}
void scale(float x, float y) {
  scale(vec2(x, y));
}
void scale(float s) {
  scale(vec2(s));
}


//
// Text
//

static const float FONT_SCALE = 1.0f / 768.0f;

static std::unique_ptr<char[]> loadFileBinary(const std::string &path) {
  std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
  if (file.is_open()) {
    auto size = file.tellg();
    auto buffer = std::unique_ptr<char[]>(new char[size]);

    file.seekg(0, std::ios::beg);
    file.read(buffer.get(), size);
    file.close();

    return buffer;
  }
  return {};
}

static VGFont createVGFontFromTTFont(const stbtt_fontinfo &info) {
  auto font = vgCreateFont(info.numGlyphs);
  for (int i = 0; i < info.numGlyphs; ++i) {
    stbtt_vertex *verts;
    auto numVerts = stbtt_GetGlyphShape(&info, i, &verts);

    VGubyte segs[numVerts];
    VGshort coords[numVerts * 4];
    int numCoords = 0;
    for (int j = 0; j < numVerts; ++j) {
      const auto &v = verts[j];
      switch (v.type) {
        case STBTT_vmove: {
          segs[j] = VG_MOVE_TO;
          coords[numCoords++] = v.x;
          coords[numCoords++] = v.y;
        } break;
        case STBTT_vline: {
          segs[j] = VG_LINE_TO;
          coords[numCoords++] = v.x;
          coords[numCoords++] = v.y;
        } break;
        case STBTT_vcurve: {
          segs[j] = VG_QUAD_TO;
          coords[numCoords++] = v.cx;
          coords[numCoords++] = v.cy;
          coords[numCoords++] = v.x;
          coords[numCoords++] = v.y;
        } break;
      }
    }

    stbtt_FreeShape(&info, verts);

    auto path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_S_16, FONT_SCALE, 0.0f, 0, 0,
                             VG_PATH_CAPABILITY_ALL);
    vgAppendPathData(path, numVerts, segs, coords);

    int advanceWidth;
    stbtt_GetGlyphHMetrics(&info, i, &advanceWidth, nullptr);

    VGfloat origin[] = { 0.0f, 0.0f };
    VGfloat escapement[] = { advanceWidth * FONT_SCALE, 0.0f };
    vgSetGlyphToPath(font, i, path, VG_FALSE, origin, escapement);

    vgDestroyPath(path);
  }
  return font;
}

void loadFont(const std::string &path) {
  ctx.fontData = loadFileBinary(path);
  if (ctx.fontData) {
    if (stbtt_InitFont(&ctx.fontInfo, reinterpret_cast<uint8_t *>(ctx.fontData.get()), 0)) {
      ctx.font = createVGFontFromTTFont(ctx.fontInfo);

      int ascent, descent, lineGap;
      stbtt_GetFontVMetrics(&ctx.fontInfo, &ascent, &descent, &lineGap);
      ctx.fontAscent = ascent * FONT_SCALE;
      ctx.fontDescent = descent * FONT_SCALE;
      ctx.fontLineGap = lineGap * FONT_SCALE;
    }
    else {
      std::cerr << "Failed to load font from: " << path << std::endl;
    }
  }
}

void fontSize(float size) {
  ctx.fontSize = size;
}

void textAlign(uint32_t align) {
  ctx.textAlign = align;
}

namespace utf8 {

// Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.

#define UTF8_ACCEPT 0
#define UTF8_REJECT 12

static const uint8_t utf8d[] = {
  // The first part of the table maps bytes to character classes that
  // to reduce the size of the transition table and create bitmasks.
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
   8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

  // The second part is a transition table that maps a combination
  // of a state of the automaton and a character class to a state.
   0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
  12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
  12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
  12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
  12,36,12,12,12,12,12,12,12,12,12,12,
};

static uint32_t decode(uint32_t *state, uint32_t *codep, uint32_t byte) {
  uint32_t type = utf8d[byte];

  *codep = (*state != UTF8_ACCEPT) ? (byte & 0x3fu) | (*codep << 6)
                                   : (0xff >> type) & (byte);

  *state = utf8d[256 + *state + type];
  return *state;
}

} // utf8

// TODO(ryan): Do we need to take the glyph bounding box into consideration here?
static float getGlyphsWidth(const std::vector<uint32_t> &indices) {
  float width = 0;
  int advanceWidth;
  for (auto i : indices) {
    stbtt_GetGlyphHMetrics(&ctx.fontInfo, i, &advanceWidth, nullptr);
    width += advanceWidth * FONT_SCALE;
  }
  return width;
}

void fillText(const std::string &text) {
  std::vector<uint32_t> indices;
  indices.reserve(text.length());

  // Decode string into utf8 codepoints and then glyph indices
  {
    uint32_t codepoint;
    uint32_t decodeState = 0;
    for (int i = 0; i < text.length(); ++i) {
      if (!utf8::decode(&decodeState, &codepoint, text[i])) {
        indices.push_back(stbtt_FindGlyphIndex(&ctx.fontInfo, codepoint));
      }
    }
  }

  // Determine origin from alignment
  {
    VGfloat origin[] = { 0.0f, 0.0f };

    if (!(ctx.textAlign & ALIGN_LEFT)) {
      auto width = getGlyphsWidth(indices);
      if (ctx.textAlign & ALIGN_RIGHT)
        origin[0] = -width;
      else if (ctx.textAlign & ALIGN_CENTER)
        origin[0] = width * -0.5f;
    }

    if (ctx.textAlign & ALIGN_TOP)
      origin[1] = -ctx.fontAscent;
    else if (ctx.textAlign & ALIGN_BOTTOM)
      origin[1] = -ctx.fontDescent;
    else if (ctx.textAlign & ALIGN_MIDDLE)
      origin[1] = 0.5f * (ctx.fontAscent - ctx.fontDescent) - ctx.fontAscent;

    vgSetfv(VG_GLYPH_ORIGIN, 2, origin);
  }

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_GLYPH_USER_TO_SURFACE);
  loadMatrix();
  vgScale(ctx.fontSize, -ctx.fontSize);

  vgDrawGlyphs(ctx.font, indices.size(), &indices[0], nullptr, nullptr, VG_FILL_PATH, true);

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
}

} // otto
