#include "display.h"
#include "render_utils.h"
#include "console_utils.h"
#include "math_utils.h"
#include <cstring>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#include <pthread.h>
#endif

__attribute__((noinline))
static bool cells_equal(const ConsoleCell& c1, const ConsoleCell& c2, bool is_mono) {
    if (c1.character != c2.character || c1.is_ui != c2.is_ui || c1.is_border != c2.is_border)
        return false;
    if (is_mono) return true;
    if (c1.bg_color != c2.bg_color) return false;
    if (c1.character.data[0] == ' ' && c1.character.data[1] == '\0') return true;
    return c1.color == c2.color;
}

static inline int glyph_length(const char* d) {
    const unsigned char c = static_cast<unsigned char>(d[0]);
    if (c < 0x80)           return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    int len = 0;
    while (len < kMaxGlyphBytes && d[len]) len++;
    return len ? len : 1;
}

struct FlushState {
    char*    head;
    char*    lim;
    char*    base;
    int      term_cursor_x;
    int      term_cursor_y;
    RGBColor cfg;
    RGBColor cbg;
    int      cfi;
    int      cbi;
    RGBColor last_raw_fg;
    RGBColor last_raw_bg;
    int      last_mapped_fg;
    int      last_mapped_bg;
    int      ansi_mode;
    bool     is_mono;
    bool     is_truecolor;
    bool     first_frame;
};

static void flush_output(const char* ptr, size_t tlen) {
    if (tlen == 0) return;
#ifdef _WIN32
    static HANDLE hc = INVALID_HANDLE_VALUE;
    if (hc == INVALID_HANDLE_VALUE) hc = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD wr = 0;
    WriteFile(hc, ptr, (DWORD)tlen, &wr, NULL);
#else
    size_t rem = tlen;
    while (rem > 0) {
        ssize_t w = write(STDOUT_FILENO, ptr, rem);
        if (w <= 0) break;
        ptr += w;
        rem -= (size_t)w;
    }
#endif
}

static inline void check_flush(FlushState& state) {
    if (state.head >= state.lim) {
        flush_output(state.base, static_cast<size_t>(state.head - state.base));
        state.head = state.base;
    }
}

__attribute__((noinline))
static void move_cursor(FlushState& state, int target_x, int target_y) {
    if (target_y != state.term_cursor_y || target_x != state.term_cursor_x) {
        *state.head++ = '\x1b'; *state.head++ = '[';
        if (target_y == state.term_cursor_y && target_x > state.term_cursor_x) {
            const int diff = target_x - state.term_cursor_x;
            if (diff > 1) state.head = fast_itoa(diff, state.head);
            *state.head++ = 'C';
        } else {
            state.head = fast_itoa(target_y + 1, state.head); *state.head++ = ';';
            state.head = fast_itoa(target_x + 1, state.head); *state.head++ = 'H';
        }
        state.term_cursor_y = target_y;
        state.term_cursor_x = target_x;
    }
}

static int map_ansi_color(int mode, const RGBColor& col, RGBColor& cache_raw, int& cache_idx) {
    if (col.isReset() || mode == 0) return -1;
    if (col == cache_raw) return cache_idx;
    cache_raw = col;
    if (mode == 256)     cache_idx = rgbToAnsi256(col);
    else if (mode == 16) cache_idx = rgbToAnsi16(col);
    else                 cache_idx = rgbToAnsi8(col);
    return cache_idx;
}

static char* emit_color_code(char* p, bool is_truecolor, int ansi_mode, const RGBColor& c, int index, bool fg) {
    p[0] = fg ? '3' : '4';
    if (is_truecolor) {
        p[1] = '8'; p[2] = ';'; p[3] = '2'; p[4] = ';';
        p = fast_itoa(c.r, p + 5); *p++ = ';';
        p = fast_itoa(c.g, p);     *p++ = ';';
        return fast_itoa(c.b, p);
    }
    if (ansi_mode == 256) {
        p[1] = '8'; p[2] = ';'; p[3] = '5'; p[4] = ';';
        return fast_itoa(index, p + 5);
    }
    if (ansi_mode == 16) {
        int code = (index < kAnsi16BrightOffset)
            ? ((fg ? kAnsi16FgBase : kAnsi16BgBase) + index)
            : ((fg ? kAnsi16BrightFgBase : kAnsi16BrightBgBase) + index - kAnsi16BrightOffset);
        return fast_itoa(code, p);
    }
    return fast_itoa((fg ? 30 : 40) + (index & 7), p);
}

__attribute__((noinline))
static void sync_colors(FlushState& state, const RGBColor& fg, const RGBColor& bg) {
    if (state.is_mono) {
        if (!state.cfg.isReset() || !state.cbg.isReset() || state.cfi != -1 || state.cbi != -1) {
            memcpy(state.head, "\x1b[0m", 4);
            state.head += 4;
            state.cfg = RGBColor{};
            state.cbg = RGBColor{};
            state.cfi = -1;
            state.cbi = -1;
        }
        return;
    }

    bool fg_changed = false;
    bool bg_changed = false;
    int fg_idx = -1;
    int bg_idx = -1;

    if (state.is_truecolor) {
        fg_changed = (!fg.isReset() && fg != state.cfg);
        bg_changed = (!bg.isReset() && bg != state.cbg);
    } else if (state.ansi_mode > 0) {
        if (!fg.isReset()) {
            fg_idx = map_ansi_color(state.ansi_mode, fg, state.last_raw_fg, state.last_mapped_fg);
            fg_changed = (fg_idx != state.cfi);
        }
        if (!bg.isReset()) {
            bg_idx = map_ansi_color(state.ansi_mode, bg, state.last_raw_bg, state.last_mapped_bg);
            bg_changed = (bg_idx != state.cbi);
        }
    }

    bool fg_needs_reset = fg.isReset() && (!state.cfg.isReset() || state.cfi != -1);
    bool bg_needs_reset = bg.isReset() && (!state.cbg.isReset() || state.cbi != -1);

    // If both foreground and background are returning to default, emit \x1b[0m directly
    if (fg_needs_reset && bg_needs_reset && !fg_changed && !bg_changed) {
        memcpy(state.head, "\x1b[0m", 4);
        state.head += 4;
        state.cfg = RGBColor{};
        state.cbg = RGBColor{};
        state.cfi = -1;
        state.cbi = -1;
        return;
    }

    if (fg_needs_reset) {
        memcpy(state.head, "\x1b[39m", 5);
        state.head += 5;
        state.cfg = RGBColor{};
        state.cfi = -1;
    }
    if (bg_needs_reset) {
        memcpy(state.head, "\x1b[49m", 5);
        state.head += 5;
        state.cbg = RGBColor{};
        state.cbi = -1;
    }

    if (!fg_changed && !bg_changed) return;

    *state.head++ = '\x1b';
    *state.head++ = '[';

    if (fg_changed) {
        state.head = emit_color_code(state.head, state.is_truecolor, state.ansi_mode, fg, fg_idx, true);
        state.cfg = fg;
        state.cfi = fg_idx;
    }
    if (fg_changed && bg_changed) {
        *state.head++ = ';';
    }
    if (bg_changed) {
        state.head = emit_color_code(state.head, state.is_truecolor, state.ansi_mode, bg, bg_idx, false);
        state.cbg = bg;
        state.cbi = bg_idx;
    }

    *state.head++ = 'm';
}

// ── Out-of-line run scanning and emission helpers ────────────────────────────
__attribute__((noinline))
static int find_run(const FlushState& state, const ConsoleCell* crow, const ConsoleCell* prow,
                    int max_len, int glen, bool& same_glyph) {
    const ConsoleCell& cur = crow[0];
    const bool is_space = (cur.character.data[0] == ' ' && cur.character.data[1] == '\0');
    // For spaces, do not clobber foreground state
    const RGBColor tfg = state.is_mono ? RGBColor{} : (is_space ? state.cfg : cur.color);
    const RGBColor tbg = state.is_mono ? RGBColor{} : cur.bg_color;

    int len = 1;
    while (len < max_len) {
        const ConsoleCell& next = crow[len];
        if (!state.first_frame && cells_equal(next, prow[len], state.is_mono)) break;
        if (!state.is_mono && next.bg_color != tbg) break;

        if (is_space) {
            if (next.character.data[0] != ' ' || next.character.data[1] != '\0') break;
        } else {
            if (next.is_ui != cur.is_ui || next.is_border != cur.is_border) break;
            if (!state.is_mono && next.color != tfg) break;
            if (next.character == cur.character) {
                // matching glyph
            } else if (glen == 1 && glyph_length(next.character.data) == 1) {
                same_glyph = false;
            } else {
                break;
            }
        }
        len++;
    }
    return len;
}

__attribute__((noinline))
static void emit_run(FlushState& state, const ConsoleCell* cells, int len, int glen, bool same_glyph) {
    if (same_glyph) {
        if (glen == 1) {
            memset(state.head, cells[0].character.data[0], len);
            state.head += len;
        } else {
            for (int r = 0; r < len; ++r) {
                memcpy(state.head, cells[0].character.data, glen);
                state.head += glen;
            }
        }
    } else {
        for (int r = 0; r < len; ++r) {
            *state.head++ = cells[r].character.data[0];
        }
    }
}

// ── Presenter thread (optional) ──────────────────────────────────────────────
#if TRETRO_ASYNC_PRESENT
struct PresentWorker {
    Display* owner;
    int      busy;     // 1 from the moment a frame is handed over until it is fully written (atomic)
    int      quit;
#ifdef _WIN32
    HANDLE   thread;
    HANDLE   wake;     // auto-reset event
    static DWORD WINAPI entry(LPVOID arg) {
        PresentWorker* w = static_cast<PresentWorker*>(arg);
        for (;;) {
            WaitForSingleObject(w->wake, INFINITE);
            if (__atomic_load_n(&w->quit, __ATOMIC_ACQUIRE)) break;
            Display& d = *w->owner;
            d.encodeAndWrite(d.present_buffer.data(), d.pend_w, d.pend_h, d.pend_mode);
            __atomic_store_n(&w->busy, 0, __ATOMIC_RELEASE);
        }
        return 0;
    }
    bool start()  { wake = CreateEventA(NULL, FALSE, FALSE, NULL); if (!wake) return false;
                    thread = CreateThread(NULL, 256 * 1024, entry, this, 0, NULL);
                    if (!thread) { CloseHandle(wake); return false; } return true; }
    void kick()   { SetEvent(wake); }
    void stop()   { __atomic_store_n(&quit, 1, __ATOMIC_RELEASE); SetEvent(wake);
                    WaitForSingleObject(thread, INFINITE); CloseHandle(thread); CloseHandle(wake); }
    static void idle_wait() { Sleep(1); }
#else
    pthread_t       thread;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    int             pending;
    static void* entry(void* arg) {
        PresentWorker* w = static_cast<PresentWorker*>(arg);
        for (;;) {
            pthread_mutex_lock(&w->mu);
            while (!w->pending && !w->quit) pthread_cond_wait(&w->cv, &w->mu);
            const bool q = w->quit != 0;
            w->pending = 0;
            pthread_mutex_unlock(&w->mu);
            if (q) break;
            Display& d = *w->owner;
            d.encodeAndWrite(d.present_buffer.data(), d.pend_w, d.pend_h, d.pend_mode);
            __atomic_store_n(&w->busy, 0, __ATOMIC_RELEASE);
        }
        return nullptr;
    }
    bool start()  { pthread_mutex_init(&mu, NULL); pthread_cond_init(&cv, NULL); pending = 0;
                    return pthread_create(&thread, NULL, entry, this) == 0; }
    void kick()   { pthread_mutex_lock(&mu); pending = 1; pthread_cond_signal(&cv); pthread_mutex_unlock(&mu); }
    void stop()   { pthread_mutex_lock(&mu); quit = 1; pthread_cond_signal(&cv); pthread_mutex_unlock(&mu);
                    pthread_join(thread, NULL); pthread_cond_destroy(&cv); pthread_mutex_destroy(&mu); }
    static void idle_wait() { usleep(200); }
#endif
};

bool Display::ensureWorker() {
    if (worker) return true;
    if (async_failed) return false;
    static_assert(sizeof(PresentWorker) <= sizeof(worker_mem), "grow Display::worker_mem");
    PresentWorker* w = reinterpret_cast<PresentWorker*>(worker_mem);
    w->owner = this; w->busy = 0; w->quit = 0;
    if (!w->start()) { async_failed = true; return false; }   // falls back to synchronous presenting
    worker = w;
    return true;
}

void Display::stopWorker() {
    if (!worker) return;
    waitIdle();
    worker->stop();
    worker = nullptr;
}

void Display::waitIdle() {
    while (worker && __atomic_load_n(&worker->busy, __ATOMIC_ACQUIRE)) PresentWorker::idle_wait();
}
#else
void Display::waitIdle() {}
bool Display::ensureWorker() { return false; }
void Display::stopWorker() {}
#endif

// ── Display Lifecycle ────────────────────────────────────────────────────────
static constexpr size_t kBytesPerCellWorstCase = 64u;

Display::Display() {
    output_capacity = 1024 * 1024;
    output_buffer.resize_uninitialized(output_capacity);
}

void Display::shutdown() {
    stopWorker();

    current_buffer.clear();
    current_buffer.shrink_to_fit();

    previous_buffer.clear();
    previous_buffer.shrink_to_fit();

    present_buffer.clear();
    present_buffer.shrink_to_fit();

    output_buffer.clear();
    output_buffer.shrink_to_fit();

    buffer_size     = 0;
    output_capacity = 0;
    first_frame     = true;
    sgr_invalid     = true;
}

Display::~Display() {
    shutdown();
}

bool Display::Resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        shutdown();
        return false;
    }
    const size_t size = static_cast<size_t>(width * height);
    const size_t required_output = size * kBytesPerCellWorstCase + DISPLAY_SAFETY_MARGIN;

    // Only touch the buffers the presenter may be using when something actually has to change.
    const bool will_realloc = required_output > output_capacity;
    const bool will_reset   = __atomic_load_n(&first_frame, __ATOMIC_ACQUIRE) || size != buffer_size;
    if (will_realloc || will_reset) waitIdle();

    if (will_realloc) {
        output_capacity = required_output;
        output_buffer.resize_uninitialized(output_capacity);
    }

    if (will_reset) {
        buffer_size = size;
        previous_buffer.assign(size, ConsoleCell());
        current_buffer.assign(size, ConsoleCell());
        present_buffer.assign(size, ConsoleCell());
        first_frame = true;
        InvalidateSGRState();
        return true;
    }
    return false;
}

void Display::InvalidateSGRState() {
    // The presenter consumes this flag (and resets its colour state) at the start of the next frame it encodes.
    __atomic_store_n(&sgr_invalid, true, __ATOMIC_RELEASE);
}

// ── High-Level Coordinator Flush ─────────────────────────────────────────────
void Display::Flush(int width, int height, const GraphicsConfig& gfx) {
    if (width <= 0 || height <= 0) return;
    if (static_cast<size_t>(width * height) > buffer_size) return;

#if TRETRO_ASYNC_PRESENT
    if (ensureWorker()) {
        // Terminal still busy with the previous frame: drop this one. Nothing was encoded, so the diff state stays
        // consistent with what the terminal actually shows and the next accepted frame catches up.
        if (__atomic_load_n(&worker->busy, __ATOMIC_ACQUIRE)) { ++dropped_frames; return; }

        memcpy(present_buffer.data(), current_buffer.data(), static_cast<size_t>(width) * height * sizeof(ConsoleCell));
        pend_w    = width;
        pend_h    = height;
        pend_mode = gfx.color_mode;
        __atomic_store_n(&worker->busy, 1, __ATOMIC_RELEASE);
        worker->kick();
        return;
    }
#endif
    encodeAndWrite(current_buffer.data(), width, height, gfx.color_mode);
}

// Diffs `cur` against the previous frame, encodes the changes as ANSI and writes them out in one go.
void Display::encodeAndWrite(const ConsoleCell* cur_buf, int width, int height, ColorMode mode) {
    if (mode != last_color_mode) {
        __atomic_store_n(&first_frame, true, __ATOMIC_RELEASE);
        last_color_mode = mode;
        InvalidateSGRState();
    }

    int ansi_mode = 0;
    if (mode == ColorMode::Ansi256)      ansi_mode = 256;
    else if (mode == ColorMode::Ansi16)  ansi_mode = 16;
    else if (mode == ColorMode::Ansi8)   ansi_mode = 8;

    FlushState state;
    state.head           = output_buffer.data();
    state.base           = state.head;
    state.lim            = state.head + output_capacity - DISPLAY_SAFETY_MARGIN;
    state.term_cursor_x  = -1;
    state.term_cursor_y  = -1;
    state.cfg            = sgr_cfg;
    state.cbg            = sgr_cbg;
    state.cfi            = sgr_cfi;
    state.cbi            = sgr_cbi;
    state.last_raw_fg    = {-1, -1, -1};
    state.last_raw_bg    = {-1, -1, -1};
    state.last_mapped_fg = -1;
    state.last_mapped_bg = -1;
    state.ansi_mode      = ansi_mode;
    state.is_mono        = (mode == ColorMode::Monochrome);
    state.is_truecolor   = (mode == ColorMode::TrueColor);
    state.first_frame    = first_frame;

    memcpy(state.head, "\x1b[?2026h\x1b[?25l", 14);
    state.head += 14;

    // Guaranteed hardware SGR reset on mode change, resize, or invalidation
    const bool sgr_was_invalid = __atomic_exchange_n(&sgr_invalid, false, __ATOMIC_ACQ_REL);
    if (first_frame || sgr_was_invalid) {
        memcpy(state.head, "\x1b[0m", 4);
        state.head += 4;
        state.cfg = RGBColor{};
        state.cbg = RGBColor{};
        state.cfi = -1;
        state.cbi = -1;
    }

    const size_t cell_row_bytes = static_cast<size_t>(width) * sizeof(ConsoleCell);
    const ConsoleCell* crow = cur_buf;
    ConsoleCell* prow = previous_buffer.data();

    for (int y = 0; y < height; ++y, crow += width, prow += width) {
        if (!state.first_frame && memcmp(crow, prow, cell_row_bytes) == 0) {
            continue;
        }

        for (int x = 0; x < width; ++x) {
            if (!state.first_frame) {
                while (x < width && cells_equal(crow[x], prow[x], state.is_mono)) {
                    prow[x] = crow[x];
                    x++;
                }
                if (x >= width) break;
            }

            check_flush(state);

            const ConsoleCell& cur = crow[x];
            const bool is_space = (cur.character.data[0] == ' ' && cur.character.data[1] == '\0');
            const int glen = is_space ? 1 : glyph_length(cur.character.data);
            bool same_glyph = true;

            int run_len = find_run(state, &crow[x], &prow[x], width - x, glen, same_glyph);

            const RGBColor tfg = state.is_mono ? RGBColor{} : (is_space ? state.cfg : cur.color);
            const RGBColor tbg = state.is_mono ? RGBColor{} : cur.bg_color;

            sync_colors(state, tfg, tbg);
            move_cursor(state, x, y);
            emit_run(state, &crow[x], run_len, glen, same_glyph);

            memcpy(&prow[x], &crow[x], run_len * sizeof(ConsoleCell));
            state.term_cursor_x += run_len;
            x += run_len - 1;
        }
    }

    sgr_cfg = state.cfg;
    sgr_cbg = state.cbg;
    sgr_cfi = state.cfi;
    sgr_cbi = state.cbi;

    memcpy(state.head, "\x1b[1;1H\x1b[?2026l", 14);
    state.head += 14;

    const size_t tlen = static_cast<size_t>(state.head - state.base);
    flush_output(state.base, tlen);
    __atomic_store_n(&first_frame, false, __ATOMIC_RELEASE);
}