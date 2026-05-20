#ifndef DISPLAY_H
#define DISPLAY_H

#include "vector.h"
#include "console.h"
#include "engine_config.h"
#include "str.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

class Display {
public:
    Display();
    ~Display();

    bool Resize(int width, int height);
    void Flush(int width, int height, const GraphicsConfig& gfx);

#ifdef _WIN32
    void FlushLegacy(int width, int height, const GraphicsConfig& gfx);
#endif

    ConsoleCell*       GetBuffer()       { return current_buffer.data(); }
    const ConsoleCell* GetBuffer() const { return current_buffer.data(); }

private:
    PodVector<ConsoleCell> current_buffer;
    PodVector<ConsoleCell> previous_buffer;
    PodVector<char>        output_buffer;

    size_t buffer_size     = 0;
    size_t output_capacity = 0;
    int    term_cursor_x   = -1;
    int    term_cursor_y   = -1;
    bool   first_frame     = true;

    StrBuf<256> last_ramp;
    ColorMode   last_color_mode   = ColorMode::TrueColor;
    bool        last_pixel_mode   = false;
    bool        last_show_normals = false;
    float       last_exposure     = 0.0f;

#ifdef _WIN32
    PodVector<CHAR_INFO> ci_curr;
    PodVector<CHAR_INFO> ci_prev;
    size_t ci_capacity = 0;
#endif
};

#endif