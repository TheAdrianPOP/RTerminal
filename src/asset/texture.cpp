#include "engine_config.h"
#include "texture.h"
#include "math_utils.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

static const RGBColor kTexLoadOkColor = {100, 255, 100};

static Vec3 ansi256_to_rgb(int code) {
    if (code < 0 || code > kAnsiMaxCode) return Vec3(1.f,1.f,1.f);
    if (code < 16) {
        const float c[][3] = {
            {0,0,0},{.5f,0,0},{0,.5f,0},{.5f,.5f,0},
            {0,0,.5f},{.5f,0,.5f},{0,.5f,.5f},{.75f,.75f,.75f},
            {.5f,.5f,.5f},{1,0,0},{0,1,0},{1,1,0},
            {0,0,1},{1,0,1},{0,1,1},{1,1,1}
        };
        return Vec3(c[code][0],c[code][1],c[code][2]);
    }
    if (code <= kAnsiCubeEnd) {
        int v=code-16;
        const int vals[]={0,95,135,175,215,255};
        return Vec3(vals[v/36]/255.f,vals[(v%36)/6]/255.f,vals[v%6]/255.f);
    }
    float g=((code-kAnsiGrayBase)*kAnsiGrayStep+kAnsiGrayStartF)/255.f;
    return Vec3(g,g,g);
}

struct CacheEntry { StrBuf<256> name; Texture* tex; };
static PodVector<CacheEntry>* texture_cache_ptr = nullptr;
static PodVector<CacheEntry>& get_cache() {
    if (!texture_cache_ptr) texture_cache_ptr = new PodVector<CacheEntry>();
    return *texture_cache_ptr;
}

static FORCE_INLINE void uv_to_xy(float u, float v, int w, int h, int& x, int& y) {
    int ix = (int)(u * (float)w);
    int iy = (int)((1.f - v) * (float)h);
    x = ix % w; if (x < 0) x += w;
    y = iy % h; if (y < 0) y += h;
}

Texture* Texture::load(const char* filename, MsgCallback cb) {
    PodVector<CacheEntry>& cache = get_cache();
    for (auto& e : cache)
        if (e.name == filename) return e.tex;

    FILE* f = fopen(filename, "rb");
    if (!f) return nullptr;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) { fclose(f); return nullptr; }

    char* buf = new char[(size_t)fsize + 1];
    fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[fsize] = '\0';

    int w = 0, h = 0;
    char* p = buf;
    while (*p) {
        while (*p == '\r' || *p == '\n') ++p;
        if (!*p) break;
        if (*p == '#') { while (*p && *p != '\n') ++p; continue; }

        int row_w = 0;
        char* line_start = p;
        while (*p && *p != '\n' && *p != '\r') {
            while (*p && isspace((unsigned char)*p) && *p != '\n' && *p != '\r') ++p;
            if (!*p || *p == '\n' || *p == '\r' || *p == '#') break;
            ++row_w;
            while (*p && !isspace((unsigned char)*p)) ++p;
        }
        if (row_w > 0) { if (row_w > w) w = row_w; ++h; }
        while (*p == '\r' || *p == '\n') ++p;
        (void)line_start;
    }

    if (w == 0 || h == 0) { delete[] buf; return nullptr; }

    Texture* tex = new Texture();
    tex->width  = w;
    tex->height = h;
    tex->pixels = new uint8_t[(size_t)(w * h)];
    memset(tex->pixels, kTexDefaultPixel, (size_t)(w * h));
    tex->name   = filename;

    p = buf; int row = 0;
    while (*p && row < h) {
        while (*p == '\r' || *p == '\n') ++p;
        if (!*p) break;
        if (*p == '#') { while (*p && *p != '\n') ++p; continue; }

        int col = 0;
        while (*p && *p != '\n' && *p != '\r' && col < w) {
            while (*p && isspace((unsigned char)*p) && *p != '\n' && *p != '\r') ++p;
            if (!*p || *p == '\n' || *p == '\r' || *p == '#') break;
            char* end = nullptr;
            long val = strtol(p, &end, 10);
            if (end == p) break;
            int idx = (int)val;
            if (idx < 0) idx = 0; if (idx > kAnsiMaxCode) idx = kAnsiMaxCode;
            tex->pixels[row * w + col] = (uint8_t)idx;
            p = end; ++col;
        }
        while (*p && *p != '\n') ++p;
        ++row;
    }
    delete[] buf;

    CacheEntry entry;
    entry.name = filename;
    entry.tex  = tex;
    cache.push_back(entry);

    char msg[kTexMsgBufSize];
    snprintf(msg, sizeof(msg), "Loaded Tex: %dx%d", w, h);
    cb(msg, kTexLoadOkColor);

    return tex;
}

Vec3 Texture::getColor(float u, float v) const {
    if (!pixels || width==0 || height==0) return Vec3(.5f,.5f,.5f);
    int x, y; uv_to_xy(u, v, width, height, x, y);
    return ansi256_to_rgb(pixels[y * width + x]);
}

bool Texture::getCharacterAndColor(float u, float v, char& out_char, Vec3& out_color) const {
    if (!pixels || width==0 || height==0) {
        out_char=' '; out_color=Vec3(.5f,.5f,.5f); return false;
    }
    int x, y; uv_to_xy(u, v, width, height, x, y);
    out_color = ansi256_to_rgb(pixels[y * width + x]);
    out_char  = ' ';
    return true;
}