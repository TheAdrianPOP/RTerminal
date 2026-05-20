#include "display.h"
#include "render_utils.h"
#include "console_utils.h"
#include "math_utils.h"
#include <cstdio>
#include <cstring>
#include <climits>

#ifndef _WIN32
#include <unistd.h>
#endif

static const RGBColor ANSI_PALETTE[16] = {
    {0,0,0},{128,0,0},{0,128,0},{128,128,0},{0,0,128},{128,0,128},{0,128,128},{192,192,192},
    {128,128,128},{255,0,0},{0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255}
};
const RGBColor UI_BACKGROUND_COLOR  = {30,30,40};
const RGBColor DEFAULT_BACKGROUND_COLOR = {0,0,0};

static FORCE_INLINE void bayerDitherRGB(const RGBColor& input, int x, int y,
                                         int& jr, int& jg, int& jb)
{
    float lum = input.r * kRec601_R + input.g * kRec601_G + input.b * kRec601_B;
    float noise = getBayer8x8(x, y) - kBayerDitherMidpoint;

#if defined(RTERMINAL_ARCH_X86)
    // SIMD Parallelization: Compute R, G, B adjustments simultaneously
    SimdFloat4 v_lum(lum);
    SimdFloat4 v_noise(noise * kBayerDitherSpread);
    SimdFloat4 v_sat(kBayerDitherSaturation);
    SimdFloat4 v_in((float)input.r, (float)input.g, (float)input.b, 0.0f);
    
    // out = lum + (input - lum) * saturation + noise
    SimdFloat4 v_res = v_lum + ((v_in - v_lum) * v_sat) + v_noise;
    
    // Parallel Clamp [0, 255]
    v_res = simd_max(SimdFloat4(0.0f), simd_min(v_res, SimdFloat4(255.0f)));
    
    // Fast hardware float-to-int conversion
    __m128i v_int = _mm_cvtps_epi32(v_res.m);
    ALIGN(16) int res_arr[4];
    _mm_store_si128((__m128i*)res_arr, v_int);
    
    jr = res_arr[0];
    jg = res_arr[1];
    jb = res_arr[2];
#else
    jr = fast_clamp((int)(lum + (input.r - lum) * kBayerDitherSaturation + noise * kBayerDitherSpread), 0, 255);
    jg = fast_clamp((int)(lum + (input.g - lum) * kBayerDitherSaturation + noise * kBayerDitherSpread), 0, 255);
    jb = fast_clamp((int)(lum + (input.b - lum) * kBayerDitherSaturation + noise * kBayerDitherSpread), 0, 255);
#endif
}

static int applyBayer16(const RGBColor& input, int x, int y) {
    if (input.r < 0) return -1;
    int jr, jg, jb;
    bayerDitherRGB(input, x, y, jr, jg, jb);

#if defined(RTERMINAL_ARCH_X86)
    SimdFloat4 vj_r((float)jr);
    SimdFloat4 vj_g((float)jg);
    SimdFloat4 vj_b((float)jb);
    SimdFloat4 vPr((float)kPercR);
    SimdFloat4 vPg((float)kPercG);
    SimdFloat4 vPb((float)kPercB);

    // Hardware-aligned Structure of Arrays (SoA) layout for ANSI palette
    ALIGN(16) static const float PAL_R[16] = {0,128,0,128,0,128,0,192,128,255,0,255,0,255,0,255};
    ALIGN(16) static const float PAL_G[16] = {0,0,128,128,0,0,128,192,128,0,255,255,0,0,255,255};
    ALIGN(16) static const float PAL_B[16] = {0,0,0,0,128,128,128,192,128,0,0,0,255,255,255,255};

    SimdFloat4 min_d(MAX_FLOAT);
    SimdFloat4 best_idx(0.0f);
    SimdFloat4 cur_idx(0.0f, 1.0f, 2.0f, 3.0f);
    SimdFloat4 step_idx(4.0f);

    // Compare 4 Palette colors per cycle (Branchless loop)
    for (int i = 0; i < 4; ++i) { 
        SimdFloat4 pal_r(&PAL_R[i * 4]);
        SimdFloat4 pal_g(&PAL_G[i * 4]);
        SimdFloat4 pal_b(&PAL_B[i * 4]);

        SimdFloat4 dr = vj_r - pal_r;
        SimdFloat4 dg = vj_g - pal_g;
        SimdFloat4 db = vj_b - pal_b;

        SimdFloat4 dist = (dr * dr * vPr) + (dg * dg * vPg) + (db * db * vPb);

        // Track minimum distances using native blends
        SimdFloat4 mask = dist < min_d;
        min_d = simd_blend(mask, dist, min_d);
        best_idx = simd_blend(mask, cur_idx, best_idx);
        
        cur_idx = cur_idx + step_idx;
    }

    // Unpack final minimum vector
    ALIGN(16) float d_arr[4];
    ALIGN(16) float i_arr[4];
    _mm_store_ps(d_arr, min_d.m);
    _mm_store_ps(i_arr, best_idx.m);

    int best = (int)i_arr[0];
    float md = d_arr[0];
    for(int j=1; j<4; ++j) {
        if (d_arr[j] < md) {
            md = d_arr[j];
            best = (int)i_arr[j];
        }
    }
    return best;
#else
    int best = 0, md = INT_MAX;
    for (int i = 0; i < kAnsi16Count; ++i) {
        int dr = jr - ANSI_PALETTE[i].r, dg = jg - ANSI_PALETTE[i].g, db = jb - ANSI_PALETTE[i].b;
        int d  = (dr * dr * kPercR) + (dg * dg * kPercG) + (db * db * kPercB);
        if (d < md) { md = d; best = i; if (md == 0) break; }
    }
    return best;
#endif
}

static int applyBayer256(const RGBColor& input, int x, int y) {
    if (input.r < 0) return -1;
    int jr, jg, jb;
    bayerDitherRGB(input, x, y, jr, jg, jb);
    return rgbToAnsi256({jr, jg, jb});
}

static FORCE_INLINE ConsoleGlyph colorToGlyph(const RGBColor& color,
                                               const GraphicsConfig& gfx,
                                               const PodVector<ConsoleGlyph>& cache)
{
    if (cache.empty()) return ConsoleGlyph(' ');
    float lv = color.r*gfx.luma_weights.x + color.g*gfx.luma_weights.y + color.b*gfx.luma_weights.z;
    int lum = fast_clamp((int)(lv + 0.5f), 0, 255);
    int idx = (lum*(int)(cache.size()-1))/255;
    return cache[cache.size()-1-(size_t)idx];
}

static ConsoleGlyph luminanceToShadeGlyph(const RGBColor& color) {
    float lum = color.r * kRec601_R + color.g * kRec601_G + color.b * kRec601_B;
    static const ConsoleGlyph s_space(' ');
    static const ConsoleGlyph s_shade1("\xe2\x96\x91");
    static const ConsoleGlyph s_shade2("\xe2\x96\x92");
    static const ConsoleGlyph s_shade3("\xe2\x96\x93");
    static const ConsoleGlyph s_shade4("\xe2\x96\x88");

    if (lum < kLumaBand1) return s_space;
    if (lum < kLumaBand2) return s_shade1;
    if (lum < kLumaBand3) return s_shade2;
    if (lum < kLumaBand4) return s_shade3;
    return s_shade4;
}

Display::Display() {
    output_capacity = 1024 * 1024; // Start with 1MB
    output_buffer.resize_uninitialized(output_capacity);
}
Display::~Display() {}

bool Display::Resize(int width, int height) {
    if (width<=0||height<=0) return false;
    size_t size=(size_t)(width*height);
    
    // Dynamically adjust output buffer capacity if needed 
    size_t required_output = size * 128 + DISPLAY_SAFETY_MARGIN;
    if (required_output > output_capacity) {
        output_capacity = required_output;
        output_buffer.resize_uninitialized(output_capacity);
    }

    if (first_frame||size!=buffer_size) {
        buffer_size=size;
        
        previous_buffer.assign(size, ConsoleCell());
        current_buffer.assign(size, ConsoleCell());

#ifdef _WIN32
        if(size>ci_capacity){
            ci_curr.resize_uninitialized(size);
            ci_prev.resize_uninitialized(size);
            memset(ci_prev.data(), 0, size * sizeof(CHAR_INFO));
            ci_capacity=size;
        } else { 
            memset(ci_prev.data(), 0, size * sizeof(CHAR_INFO)); 
        }
#endif
        first_frame=true; return true;
    }
    return false;
}

void Display::Flush(int width, int height, const GraphicsConfig& gfx) {
    if (width<=0||height<=0) return;
    
    bool changed=(gfx.ascii_ramp!=last_ramp)||(gfx.color_mode!=last_color_mode)||
                 (gfx.pixel_mode!=last_pixel_mode)||(gfx.show_normals!=last_show_normals)||
                 (!compare_ff_relative(gfx.exposure, last_exposure));
    if (changed) {
        first_frame=true; last_ramp=gfx.ascii_ramp; last_color_mode=gfx.color_mode;
        last_pixel_mode=gfx.pixel_mode; last_show_normals=gfx.show_normals;
        last_exposure=gfx.exposure;
    }

    char* head=output_buffer.data();
    char* const lim=output_buffer.data()+output_capacity-DISPLAY_SAFETY_MARGIN;

    memcpy(head, "\x1b[?2026h\x1b[?25l", 14);
    head += 14;

    term_cursor_x = 0;
    term_cursor_y = 0;

    RGBColor cfg, cbg; 
    int cfi=-1, cbi=-1;

    const bool is_pixel = gfx.pixel_mode;
    const bool is_mono = (gfx.color_mode == ColorMode::Monochrome);
    const bool is_ansi256 = (gfx.color_mode == ColorMode::Ansi256);
    const bool is_truecolor = (gfx.color_mode == ColorMode::TrueColor);
    const bool use_bayer = (is_ansi256 || gfx.color_mode == ColorMode::Ansi16);

    updateRampCache(gfx.ascii_ramp);
    const PodVector<ConsoleGlyph>& ascii_cache = tl_ramp_cache.glyphs;

    bool buf_overflow=false;
    for(int y=0;y<height&&!buf_overflow;++y){
        int ro=y*width;
        for(int x=0;x<width;++x){
        if (head >= lim) {
 #ifdef _WIN32
            char msg[64]; wsprintfA(msg, "[Flush] buf limit y=%d x=%d\r\n", y, x);
            DWORD w; WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, (DWORD)lstrlenA(msg), &w, NULL);
 #else
            fprintf(stderr, "[Flush] output buffer limit hit at y=%d x=%d\n", y, x);
 #endif
            buf_overflow = true; break;
        }


            int idx=ro+x;
            const ConsoleCell& cur = current_buffer[idx];

            if(!first_frame) {
                const ConsoleCell* cur_ptr = &current_buffer[idx];
                const ConsoleCell* prev_ptr = &previous_buffer[idx];
                
                if (*cur_ptr == *prev_ptr) {
                    int skip = 1;
                    const int max_skip = width - x;
                    
                    while (skip < max_skip && cur_ptr[skip] == prev_ptr[skip]) {
                        skip++;
                    }
                    if (skip >= 4) {
                        x += skip - 1;
                        continue;
                    }
                }
            }

            if(y!=term_cursor_y||x!=term_cursor_x){
                if (y == term_cursor_y && x > term_cursor_x && (x - term_cursor_x) < 20) {
                    int diff = x - term_cursor_x;
                    *head++='\x1b';*head++='[';
                    if (diff > 1) head=fast_itoa(diff, head);
                    *head++='C';
                } else {
                    *head++='\x1b';*head++='[';
                    head=fast_itoa(y+1,head);*head++=';';
                    head=fast_itoa(x+1,head);*head++='H';
                }
                term_cursor_y=y;term_cursor_x=x;
            }

            ConsoleGlyph oc; 
            RGBColor tfg, tbg; 
            int tfi=-1, tbi=-1;

            if(cur.is_ui){
                if(is_pixel&&cur.is_border){
                    oc=' '; tbg=cur.color;
                } else {
                    oc=cur.character; tfg=cur.color; 
                    tbg=cur.bg_color.isReset() ? (is_pixel ? UI_BACKGROUND_COLOR : RGBColor{}) : cur.bg_color;
                }
                if(is_mono) tfg=tbg=RGBColor{};
            } else {
                if(is_pixel){
                    oc=' '; tbg=cur.color;
                } else {
                    oc=colorToGlyph(cur.color,gfx,ascii_cache);
                    tfg=cur.color; tbg=cur.bg_color;
                }
                if(is_mono){
                    if(is_pixel) oc=luminanceToShadeGlyph(cur.color);
                    tfg=tbg=RGBColor{};
                }
            }

            if(use_bayer){
                if(cur.is_ui || !cur.bg_color.isReset()){
                    if(is_ansi256){tfi=rgbToAnsi256(tfg);tbi=rgbToAnsi256(tbg);}
                    else{tfi=rgbToAnsi16(tfg);tbi=rgbToAnsi16(tbg);}
                } else {
                    if(is_ansi256){tfi=applyBayer256(tfg,x,y);tbi=applyBayer256(tbg,x,y);}
                    else{tfi=applyBayer16(tfg,x,y);tbi=applyBayer16(tbg,x,y);}
                }
            }

            if(tfg.isReset()||(use_bayer&&tfi==-1)){
                if(!cfg.isReset()){
                    *head++='\x1b';*head++='[';*head++='3';*head++='9';*head++='m';
                    cfg=RGBColor{}; cfi=-1;
                }
            } else {
                if(is_truecolor){
                    if(tfg!=cfg){ write_ansi_truecolor(head, tfg, true); cfg=tfg; }
                } else {
                    if(tfi!=cfi){
                        if(is_ansi256) write_ansi_256(head,tfi,true);
                        else write_ansi_16(head,tfi,true);
                        cfi=tfi;
                    }
                }
            }

            if(tbg.isReset()||(use_bayer&&tbi==-1)){
                if(!cbg.isReset()){
                    *head++='\x1b';*head++='[';*head++='4';*head++='9';*head++='m';
                    cbg=RGBColor{}; cbi=-1;
                }
            } else {
                if(is_truecolor){
                    if(tbg!=cbg){ write_ansi_truecolor(head, tbg, false); cbg=tbg; }
                } else {
                    if(tbi!=cbi){
                        if(is_ansi256) write_ansi_256(head,tbi,false);
                        else write_ansi_16(head,tbi,false);
                        cbi=tbi;
                    }
                }
            }

            for(int k=0;k<kMaxGlyphBytes;++k){
                if(!oc.data[k]) break;
                *head++=oc.data[k];
            }
            term_cursor_x++;
            previous_buffer[idx]=cur;
        }
    }

    write_ansi_reset(head);
    memcpy(head, "\x1b[1;1H\x1b[?2026l", 14);
    head += 14;

    size_t tlen=(size_t)(head-output_buffer.data());
    if(tlen>0){
        const char* ptr=output_buffer.data(); 
#ifdef _WIN32
        HANDLE hc=GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD wr=0;
        WriteFile(hc,ptr,(DWORD)tlen,&wr,NULL);
#else
        size_t rem = tlen;
        while(rem > 0){
            ssize_t w = write(STDOUT_FILENO, ptr, rem);
            if(w <= 0) break;
            ptr += w;
            rem -= (size_t)w;
        }
#endif
    }
    first_frame=false;
}

#ifdef _WIN32
void Display::FlushLegacy(int width, int height, const GraphicsConfig& gfx) {
    if(width<=0||height<=0)return; (void)gfx;
    size_t total=(size_t)(width*height);
    if(ci_capacity<total)return;
    const ConsoleCell* src=current_buffer.data(); 
    CHAR_INFO* dst=ci_curr.data();
    for(int i=0;i<(int)total;++i){
        WCHAR wch=(WCHAR)(unsigned char)src[i].character.data[0]; if(!wch)wch=L' ';
        dst[i].Char.UnicodeChar=wch;
        dst[i].Attributes=ansi16ToWinAttr(src[i].color.isReset()?7:rgbToAnsi16(src[i].color),0);
    }
    flushBufferWindowsLegacy(width,height,ci_curr.data(),ci_prev.data());
    first_frame=false;
}
#endif