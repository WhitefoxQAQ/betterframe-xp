// Implementation of the shared drawing helpers.
#include "draw.h"

#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

/**
 * Cleanup callback cairo calls to close the FT_Face once it's no longer needed.
 * @param face the FT_Face
 */
void destroyFtFace(void* face) {
    FT_Done_Face(static_cast<FT_Face>(face));
}

const cairo_user_data_key_t kFtFaceKey {};

}  // namespace

FontSet::FontSet() {
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) == 0) ftLibrary_ = library;
}

FontSet::~FontSet() {
    release();
    // Don't close FT_Library here: the FT_Face must stay alive as long as cairo holds it
    // (the OS will clean it up on exit).
}

cairo_font_face_t* FontSet::createFace(const std::string& path, bool bold) {
    FT_Face face = nullptr;
    if (ftLibrary_ != nullptr && FT_New_Face(static_cast<FT_Library>(ftLibrary_), path.c_str(), 0, &face) == 0) {
        cairo_font_face_t* cairoFace = cairo_ft_font_face_create_for_ft_face(face, 0);
        if (cairo_font_face_set_user_data(cairoFace, &kFtFaceKey, face, destroyFtFace) == CAIRO_STATUS_SUCCESS) {
            return cairoFace;
        }
        cairo_font_face_destroy(cairoFace);
        FT_Done_Face(face);
    }
    std::fprintf(stderr, "[draw] can't read font %s, falling back to Noto Sans CJK JP\n", path.c_str());
    return cairo_toy_font_face_create("Noto Sans CJK JP", CAIRO_FONT_SLANT_NORMAL,
                                      bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
}

void FontSet::release() {
    // If a cairo_t still holds a reference, the font stays alive until that reference is dropped
    if (regular_ != nullptr) cairo_font_face_destroy(regular_);
    if (bold_ != nullptr) cairo_font_face_destroy(bold_);
    regular_ = nullptr;
    bold_ = nullptr;
}

void FontSet::load(const std::string& regularPath, const std::string& boldPath) {
    if (regular_ != nullptr && regularPath == regularPath_ && boldPath == boldPath_) return;
    release();
    regular_ = createFace(regularPath, false);
    bold_ = createFace(boldPath, true);
    regularPath_ = regularPath;
    boldPath_ = boldPath;
}

double Pen::measure(const std::string& text, double size, bool isBold) const {
    cairo_set_font_face(cr, isBold ? fonts->bold() : fonts->regular());
    cairo_set_font_size(cr, size);
    cairo_text_extents_t extents;
    cairo_text_extents(cr, text.c_str(), &extents);
    return extents.x_advance;
}

double Pen::text(double x, double y, const std::string& text, double size, Color c, bool isBold,
                 bool alignRight) const {
    color(c);
    if (alignRight) {
        const double width = measure(text, size, isBold);
        cairo_move_to(cr, x - width, y);
        cairo_show_text(cr, text.c_str());
        return width;
    }
    // For left alignment, skip a separate width measurement and derive the width from the
    // current point after drawing (so text layout only happens once).
    cairo_set_font_face(cr, isBold ? fonts->bold() : fonts->regular());
    cairo_set_font_size(cr, size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text.c_str());
    double endX = x;
    double endY = y;
    cairo_get_current_point(cr, &endX, &endY);
    return endX - x;
}

void Pen::roundedRect(double x, double y, double w, double h, double r) const {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

void Pen::dot(double x, double y, Color c) const {
    color(c);
    cairo_arc(cr, x, y, 4.0, 0, 2 * M_PI);
    cairo_fill(cr);
}

void surfaceToRgba(cairo_surface_t* surface, std::vector<uint8_t>& out) {
    // Precompute the un-premultiply math (c * 255 / a) as a lookup table so we don't divide per pixel
    static const std::vector<uint8_t> unpremultiply = [] {
        std::vector<uint8_t> table(256 * 256);
        for (uint32_t a = 0; a < 256; ++a) {
            for (uint32_t c = 0; c < 256; ++c) {
                const uint32_t value = a == 0 ? 0 : std::min<uint32_t>(255, (c * 255 + a / 2) / a);
                table[a * 256 + c] = static_cast<uint8_t>(value);
            }
        }
        return table;
    }();

    cairo_surface_flush(surface);
    const int width = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const uint8_t* data = cairo_image_surface_get_data(surface);
    out.resize(static_cast<size_t>(width) * height * 4);
    uint8_t* dst = out.data();
    for (int y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(data + static_cast<size_t>(y) * stride);
        for (int x = 0; x < width; ++x) {
            // cairo stores premultiplied ARGB (native-endian 32-bit); un-premultiply and reorder to RGBA
            const uint32_t p = row[x];
            const uint32_t a = p >> 24;
            const uint8_t* line = unpremultiply.data() + a * 256;
            dst[0] = line[(p >> 16) & 0xFF];
            dst[1] = line[(p >> 8) & 0xFF];
            dst[2] = line[p & 0xFF];
            dst[3] = static_cast<uint8_t>(a);
            dst += 4;
        }
    }
}
