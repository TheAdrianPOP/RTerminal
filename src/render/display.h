#ifndef DISPLAY_H
#define DISPLAY_H

#include "vector.h"
#include "console.h"
#include "engine_config.h"

// 1 (default): Flush() only snapshots the frame; a presenter thread diffs, encodes and writes it, and frames are
// dropped (not queued) while the terminal is still busy with the previous one, so a slow terminal can no longer
// stall emulation or audio. 0: previous behaviour (everything inline on the calling thread).
#ifndef TRETRO_ASYNC_PRESENT
#define TRETRO_ASYNC_PRESENT 1
#endif

struct PresentWorker;

class Display {
public:
    Display();
    ~Display();

    void shutdown();
    bool Resize(int width, int height);
    void Flush(int width, int height, const GraphicsConfig& gfx);

    void InvalidateSGRState();

    // Blocks until the frame currently being written (if any) is fully out. Call it before writing raw escape
    // sequences to the terminal from elsewhere, so they cannot interleave with a frame.
    void waitIdle();

    // Frames skipped because the terminal was still busy (always 0 with TRETRO_ASYNC_PRESENT=0).
    unsigned droppedFrames() const { return dropped_frames; }

    ConsoleCell* GetBuffer() { return current_buffer.data(); }

private:
    friend struct PresentWorker;

    void encodeAndWrite(const ConsoleCell* cur, int width, int height, ColorMode mode);
    bool ensureWorker();
    void stopWorker();

    PodVector<ConsoleCell> current_buffer;
    PodVector<ConsoleCell> previous_buffer;
    PodVector<char>        output_buffer;
    PodVector<ConsoleCell> present_buffer;   // snapshot owned by the presenter while a frame is in flight

    size_t buffer_size     = 0;
    size_t output_capacity = 0;
    bool   first_frame     = true;
    bool   sgr_invalid     = true;

    ColorMode last_color_mode = ColorMode::TrueColor;

    RGBColor sgr_cfg{};    
    RGBColor sgr_cbg{};    
    int      sgr_cfi = -1; 
    int      sgr_cbi = -1; 

    PresentWorker* worker       = nullptr;
    alignas(16) unsigned char worker_mem[192];   // storage for the presenter state (no heap use)
    bool           async_failed = false;
    int            pend_w       = 0;
    int            pend_h       = 0;
    ColorMode      pend_mode    = ColorMode::TrueColor;
    unsigned       dropped_frames = 0;
};

#endif // DISPLAY_H