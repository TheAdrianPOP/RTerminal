#include "nes.hpp"
#include "app_base.h"
#include "display.h"
#include "input.h"
#include "render_utils.h"
#include "str.h"
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

#include "audio.h"

struct NESSynth {
    double phase_p1 = 0;
    double phase_p2 = 0;
    double phase_tri = 0;
    double phase_noise = 0;
    
    uint16_t noise_reg = 1;
    
    // Duty cycle sequences
    static constexpr uint8_t pulse_sequences[4][8] = {
        {0, 1, 0, 0, 0, 0, 0, 0}, // 12.5%
        {0, 1, 1, 0, 0, 0, 0, 0}, // 25%
        {0, 1, 1, 1, 1, 0, 0, 0}, // 50%
        {1, 0, 0, 1, 1, 1, 1, 1}  // 75% (25% negated/shifted)
    };

    // Triangle sequence (32 steps)
    static constexpr uint8_t tri_sequence[32] = {
        15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };

    // Noise period table (NTSC)
    static constexpr uint16_t noise_periods[16] = {
        4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
    };

    void mix(int16_t* out_buf, int frames, cynes::APU* apu) {
        if (!apu) {
            memset(out_buf, 0, frames * 4);
            return;
        }

        double cpu_freq = 1789773.0;
        double sample_rate = 44100.0;

        for (int i = 0; i < frames; ++i) {
            uint8_t p1_val = 0, p2_val = 0, tri_val = 0, noise_val = 0, dmc_lvl = 0;

            // Pulse 1
            if (apu->is_channel_active(0)) {
                uint16_t period = apu->get_period(0);
                if (period > 7) {
                    double freq = cpu_freq / (16.0 * (period + 1));
                    phase_p1 += freq / sample_rate;
                    while (phase_p1 >= 1.0) phase_p1 -= 1.0;
                    
                    int duty_idx = (apu->regs[0x00] >> 6) & 0x03;
                    if (pulse_sequences[duty_idx][(int)(phase_p1 * 8)]) {
                        p1_val = apu->get_volume(0);
                    }
                }
            }

            // Pulse 2
            if (apu->is_channel_active(1)) {
                uint16_t period = apu->get_period(1);
                if (period > 7) {
                    double freq = cpu_freq / (16.0 * (period + 1));
                    phase_p2 += freq / sample_rate;
                    while (phase_p2 >= 1.0) phase_p2 -= 1.0;
                    
                    int duty_idx = (apu->regs[0x04] >> 6) & 0x03;
                    if (pulse_sequences[duty_idx][(int)(phase_p2 * 8)]) {
                        p2_val = apu->get_volume(1);
                    }
                }
            }

            // Triangle
            if (apu->is_channel_active(2) && apu->get_volume(2) > 0) {
                int tri_period = apu->regs[0x0A] | ((apu->regs[0x0B] & 0x07) << 8);
                if (tri_period > 2) {
                    double freq = cpu_freq / (32.0 * (tri_period + 1));
                    phase_tri += freq / sample_rate;
                    while (phase_tri >= 1.0) phase_tri -= 1.0;
                    tri_val = tri_sequence[(int)(phase_tri * 32)];
                }
            }

            // Noise
            if (apu->is_channel_active(3)) {
                uint8_t n_2 = apu->regs[0x0E];
                bool short_mode = n_2 & 0x80;
                int period_idx = n_2 & 0x0F;
                double freq = cpu_freq / noise_periods[period_idx];
                
                phase_noise += freq / sample_rate;
                while (phase_noise >= 1.0) {
                    phase_noise -= 1.0;
                    uint16_t feedback = (noise_reg & 1) ^ ((noise_reg >> (short_mode ? 6 : 1)) & 1);
                    noise_reg = (noise_reg >> 1) | (feedback << 14);
                }
                if (!(noise_reg & 1)) {
                    noise_val = apu->get_volume(3);
                }
            }

            // DMC
            dmc_lvl = apu->get_dmc_level();

            // NES Non-linear Mixer
            float pulse_sum = (float)p1_val + (float)p2_val;
            float pulse_out = (pulse_sum > 0) ? 95.88f / ((8128.0f / pulse_sum) + 100.0f) : 0.f;
            
            float tnd_sum = (float)tri_val / 8227.0f + (float)noise_val / 12241.0f + (float)dmc_lvl / 22638.0f;
            float tnd_out = (tnd_sum > 0) ? 159.79f / ((1.0f / tnd_sum) + 100.0f) : 0.f;

            int16_t sample = (int16_t)((pulse_out + tnd_out) * 30000.0f);
            out_buf[i * 2 + 0] = sample;
            out_buf[i * 2 + 1] = sample;
        }
    }
};

class NESApp : public AppBase {
public:
    NESApp(const char* rom_path) : AppBase(60.1f, 1.0f), rom_path_(rom_path) {
        gfx_.pixel_mode = false;
        gfx_.color_mode = ColorMode::TrueColor;
        gfx_.show_ui = true;
        timer_accumulator_ = 0.f;
    }

    virtual ~NESApp() {
        audio_.shutdown();
        for (int i = 0; i < SNAPSHOT_SLOTS; i++) delete[] snap_bufs_[i];
#ifdef _WIN32
        timeEndPeriod(1);
#endif
    }

protected:
    static void audioCallback(int16_t* out_buf, int frames, void* userdata) {
        NESApp* self = (NESApp*)userdata;
        if (self->nes_) {
            self->synth_.mix(out_buf, frames, &self->nes_->apu);
        } else {
            memset(out_buf, 0, frames * 4);
        }
    }

    bool onInit() override {
#ifdef _WIN32
        timeBeginPeriod(1);
#endif
        if (!showLoadingScreen()) return false;

        printf("Loading ROM: %s\n", rom_path_);
        nes_.reset(new cynes::NES(rom_path_));
        if (!nes_ || !nes_->is_valid()) {
            printf("Failed to load ROM!\n");
            platform_sleep(2.0);
            return false;
        }

        if (audio_.init()) {
            audio_.setStreamCallback(audioCallback, this);
            audio_.setMasterVolume(0.5f);
        }
        
        display_.Resize(width, height);

        // Take initial snapshot so T-key works from the very start
        takeSnapshot();
        return true;
    }

    void onFrame(float dt) override {
        if (status_timer_ > 0) status_timer_ -= dt;
        handleInput();

        if (paused_) return;

        // Fast forward support (10x speed)
        float speed_mult = input_.isKeyDown(KEY_F) ? 10.0f : 1.0f;
        const float target_dt = (1.0f / 60.0988f) / speed_mult;

        timer_accumulator_ += dt;
        if (timer_accumulator_ > 0.1f) timer_accumulator_ = 0.1f;

        // Auto-snapshot every 5 seconds as a safe revert point
        snapshot_timer_ += dt;
        if (snapshot_timer_ >= 5.0f) {
            snapshot_timer_ = 0.0f;
            takeSnapshot();
        }

        while (timer_accumulator_ >= target_dt) {
            nes_->step(controller_state_, 1);
            timer_accumulator_ -= target_dt;
        }

        renderNES();
        if (gfx_.show_ui) renderUI();
        display_.Flush(width, height, gfx_);
    }

    void onResize(int w, int h) override {
        display_.Resize(w, h);
        clearConsole();
    }

    void loadROM(const char* path) {
        printf("Changing ROM to: %s\n", path);
        auto new_nes = OwnedSingle<cynes::NES>(new cynes::NES(path));
        if (new_nes->is_valid()) {
            nes_ = std::move(new_nes);
            rom_path_storage_ = path;
            rom_path_ = rom_path_storage_.c_str();
            status_msg_ = "ROM LOADED";
            status_col_ = RGBColor(100, 255, 100);
            status_timer_ = 2.0f;
        } else {
            status_msg_ = "LOAD FAILED";
            status_col_ = RGBColor(255, 100, 100);
            status_timer_ = 2.0f;
        }
    }

private:
    void handleInput() {
        input_.update();

        // Drag & Drop / Console Paste detection
        const char* text = input_.getTextInput();
        if (text && *text) {
            StrBuf<1024> s = text;
            char* p = s.data;
            int l = s.len;
            
            // Trim whitespace from end
            while (l > 0 && (p[l-1] == ' ' || p[l-1] == '\r' || p[l-1] == '\n' || p[l-1] == '\t')) {
                p[l-1] = '\0';
                l--;
            }
            // Trim whitespace from start
            while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') {
                p++;
                l--;
            }
            // Strip quotes
            if (l >= 2 && p[0] == '"' && p[l-1] == '"') {
                p[l-1] = '\0';
                p++;
                l -= 2;
            }
            
            if (l > 4 && (strstr(p, ".nes") || strstr(p, ".NES"))) {
                loadROM(p);
            }
        }

        // NES Controller mapping
        controller_state_ = 0;
        if (input_.isKeyDown(KEY_Z) || input_.isKeyDown(KEY_K))      controller_state_ |= 0x80; // A
        if (input_.isKeyDown(KEY_X) || input_.isKeyDown(KEY_J))      controller_state_ |= 0x40; // B
        if (input_.isKeyDown(KEY_SHIFT) || input_.isKeyDown(KEY_SPACE)) controller_state_ |= 0x20; // Select
        if (input_.isKeyDown(KEY_ENTER))                             controller_state_ |= 0x10; // Start
        if (input_.isKeyDown(KEY_UP)    || input_.isKeyDown(KEY_W))  controller_state_ |= 0x08; // Up
        if (input_.isKeyDown(KEY_DOWN)  || input_.isKeyDown(KEY_S))  controller_state_ |= 0x04; // Down
        if (input_.isKeyDown(KEY_LEFT)  || input_.isKeyDown(KEY_A))  controller_state_ |= 0x02; // Left
        if (input_.isKeyDown(KEY_RIGHT) || input_.isKeyDown(KEY_D))  controller_state_ |= 0x01; // Right

        // Emulator Commands
        if (input_.isKeyPressed(KEY_T)) {
            revertSnapshot();
        }

        if (input_.isKeyPressed(KEY_R)) {
            nes_->reset();
            status_msg_ = "EMULATOR RESET";
            status_col_ = RGBColor(255, 255, 0);
            status_timer_ = 2.0f;
        }

        if (input_.isKeyPressed(KEY_P)) gfx_.pixel_mode = !gfx_.pixel_mode;
        if (input_.isKeyPressed(KEY_C)) {
            int mode = (int)gfx_.color_mode + 1;
            if (mode > (int)ColorMode::Monochrome) mode = 0;
            gfx_.color_mode = (ColorMode)mode;
        }
        if (input_.isKeyPressed(KEY_U)) gfx_.show_ui = !gfx_.show_ui;
        if (input_.isKeyPressed(KEY_O)) scale_mode_ = (scale_mode_ + 1) % 3;
        if (input_.isKeyPressed(KEY_H)) show_instructions_ = !show_instructions_;
        if (input_.isKeyPressed(KEY_I)) show_debug_ = !show_debug_;

        if (input_.isKeyPressed(KEY_M)) {
            muted_ = !muted_;
            audio_.setMasterVolume(muted_ ? 0.0f : volume_);
            status_msg_ = muted_ ? "AUDIO MUTED" : "AUDIO UNMUTED";
            status_col_ = muted_ ? RGBColor(255, 100, 100) : RGBColor(100, 255, 100);
            status_timer_ = 1.0f;
        }

        if (input_.isKeyPressed(KEY_Q)) paused_ = !paused_;

        if (input_.isKeyPressed('5')) saveState();
        if (input_.isKeyPressed('8')) loadState();

        if (input_.isKeyPressed(KEY_G)) {
            nes_->set_glitch_mode(!nes_->get_glitch_mode());
            status_msg_ = nes_->get_glitch_mode() ? "GLITCH MODE ENABLED" : "GLITCH MODE DISABLED";
            status_col_ = nes_->get_glitch_mode() ? RGBColor(255, 100, 255) : RGBColor(100, 255, 100);
            status_timer_ = 2.0f;
        }

        if (input_.isKeyPressed(KEY_ESC)) exit(0);

        // Volume control
        if (input_.isKeyPressed(KEY_0)) {
            volume_ += 0.05f;
            if (volume_ > 1.0f) volume_ = 1.0f;
            if (!muted_) audio_.setMasterVolume(volume_);
        }
        if (input_.isKeyPressed(KEY_9)) {
            volume_ -= 0.05f;
            if (volume_ < 0.0f) volume_ = 0.0f;
            if (!muted_) audio_.setMasterVolume(volume_);
        }
    }

    void saveState() {
        if (!nes_) return;
        cynes::SaveState ss_size(cynes::SaveState::Mode::Size);
        nes_->stream_state(ss_size);
        size_t size = ss_size.size();
        
        uint8_t* b = new uint8_t[size];
        cynes::SaveState ss(cynes::SaveState::Mode::Save, b);
        nes_->stream_state(ss);
        
        FILE* f = fopen("save.bin", "wb");
        if (f) {
            fwrite(b, 1, size, f);
            fclose(f);
            setStatusMessage("STATE SAVED", RGBColor(100, 255, 100));
        }
        delete[] b;
    }

    void loadState() {
        if (!nes_) return;
        FILE* f = fopen("save.bin", "rb");
        if (!f) {
            setStatusMessage("NO SAVE FOUND", RGBColor(255, 100, 100));
            return;
        }
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t* b = new uint8_t[size];
        fread(b, 1, size, f);
        fclose(f);
        cynes::SaveState ss(cynes::SaveState::Mode::Load, b);
        nes_->stream_state(ss);
        delete[] b;
        setStatusMessage("STATE LOADED", RGBColor(255, 255, 100));
    }

    void setStatusMessage(const char* msg, RGBColor col) {
        status_msg_ = msg;
        status_col_ = col;
        status_timer_ = 2.0f;
    }

    void takeSnapshot() {
        if (!nes_) return;

        // Size the buffers once
        if (snap_size_ == 0) {
            cynes::SaveState ss_size(cynes::SaveState::Mode::Size);
            nes_->stream_state(ss_size);
            snap_size_ = ss_size.size();
        }

        // Allocate this slot if needed
        if (!snap_bufs_[snap_write_]) {
            snap_bufs_[snap_write_] = new uint8_t[snap_size_];
        }

        cynes::SaveState ss(cynes::SaveState::Mode::Save, snap_bufs_[snap_write_]);
        nes_->stream_state(ss);

        snap_write_ = (snap_write_ + 1) % SNAPSHOT_SLOTS;
        if (snap_count_ < SNAPSHOT_SLOTS) snap_count_++;
        snap_revert_depth_ = 0; // reset depth — we're on a fresh branch
    }

    void revertSnapshot() {
        if (!nes_ || snap_count_ == 0) return;

        // Step one deeper on each T press, up to the oldest slot
        int max_depth = snap_count_ < SNAPSHOT_SLOTS ? snap_count_ : SNAPSHOT_SLOTS;
        if (snap_revert_depth_ < max_depth) snap_revert_depth_++;

        // The slot to restore: walk backwards from (snap_write_ - 1)
        int slot = (snap_write_ - snap_revert_depth_ + SNAPSHOT_SLOTS * 2) % SNAPSHOT_SLOTS;
        if (!snap_bufs_[slot]) return;

        cynes::SaveState ss(cynes::SaveState::Mode::Load, snap_bufs_[slot]);
        nes_->stream_state(ss);

        char msg[32];
        sprintf(msg, "REVERT %d/%d", snap_revert_depth_, snap_count_ < SNAPSHOT_SLOTS ? snap_count_ : SNAPSHOT_SLOTS);
        setStatusMessage(msg, RGBColor(100, 200, 255));
    }

    bool showLoadingScreen() {
        clearConsole();
        setCursorVisibility(false);
        
        int mx = width / 2;
        int my = height / 2;

        auto drawBar = [&](const char* task, float pct) {
            setCursorPosition(mx - 20, my - 2);
            printf("\x1b[38;2;255;215;0m RTERMINAL NES EMULATOR \x1b[0m");
            
            setCursorPosition(mx - 20, my);
            int bw = 30;
            int fill = (int)(pct * bw);
            printf("[");
            for (int i = 0; i < bw; ++i) printf(i < fill ? "\x1b[32m#\x1b[0m" : ".");
            printf("] %3d%%", (int)(pct * 100));
            
            setCursorPosition(mx - 20, my + 1);
            printf(" %-36s", task);
            fflush(stdout);
        };

        drawBar("Initializing Core...", 0.2f); platform_sleep(0.3);
        drawBar("Preparing Render Pipeline...", 0.5f); platform_sleep(0.3);
        drawBar("Mapping Virtual Hardware...", 0.8f); platform_sleep(0.3);
        drawBar("Ready!", 1.0f); platform_sleep(0.2);
        
        clearConsole();
        return true;
    }

    void drawWindow(const char* title, int x, int y, int w, int h, RGBColor border_col) {
        ConsoleCell* buf = display_.GetBuffer();
        if (!buf) return;

        RGBColor bg(15, 15, 25);
        RGBColor title_bg(40, 40, 60);

        for (int j = 0; j < h; ++j) {
            int cy = y + j;
            if (cy < 0 || cy >= height) continue;
            for (int i = 0; i < w; ++i) {
                int cx = x + i;
                if (cx < 0 || cx >= width) continue;

                ConsoleGlyph glyph(' ');
                RGBColor fg = border_col;
                RGBColor cell_bg = bg;

                if (j == 0) { // Title bar
                    cell_bg = title_bg;
                    int title_len = (int)strlen(title);
                    int tx = (w - title_len) / 2;
                    if (i >= tx && i < tx + title_len) {
                        glyph = title[i - tx];
                        fg = RGBColor(255, 255, 255);
                    } else if (i == 0 || i == w - 1) glyph = "+";
                    else glyph = "-";
                } else if (j == h - 1) { // Bottom border
                    glyph = (i == 0 || i == w - 1) ? "+" : "-";
                } else if (i == 0 || i == w - 1) { // Side borders
                    glyph = "|";
                }

                buf[cy * width + cx] = ConsoleCell(glyph, fg, cell_bg, true, false);
            }
        }
        
        // Simple Shadow effect
        for(int i=1; i<w+1; ++i) {
            int sx = x + i; int sy = y + h;
            if (sx >= 0 && sx < width && sy >= 0 && sy < height) {
                buf[sy * width + sx].color.r /= 2;
                buf[sy * width + sx].color.g /= 2;
                buf[sy * width + sx].color.b /= 2;
                buf[sy * width + sx].bg_color.r /= 2;
                buf[sy * width + sx].bg_color.g /= 2;
                buf[sy * width + sx].bg_color.b /= 2;
            }
        }
        for(int j=1; j<h; ++j) {
            int sx = x + w; int sy = y + j;
            if (sx >= 0 && sx < width && sy >= 0 && sy < height) {
                buf[sy * width + sx].color.r /= 2;
                buf[sy * width + sx].color.g /= 2;
                buf[sy * width + sx].color.b /= 2;
                buf[sy * width + sx].bg_color.r /= 2;
                buf[sy * width + sx].bg_color.g /= 2;
                buf[sy * width + sx].bg_color.b /= 2;
            }
        }
    }

    void showInstructions() {
        int box_w = 26;
        const char* lines[] = {
            "  - D-PAD: Arrows/WASD",
            "  - A BTN: Z or K",
            "  - B BTN: X or J",
            "  - START: Enter",
            "  - SEL: Shift/Space",
            "------------------------",
            "  [P] Pixel  [C] Color",
            "  [O] Scale  [U] UI",
            "  [5] Save   [8] Load",
            "  [T] Revert [R] Reset",
            "  [Q] Pause  [F] Fast",
            "  [G] Glitch [M] Mute",
            "  [I] Debug  [H] Help   [ESC] Quit"
        };
        int box_h = (int)(sizeof(lines) / sizeof(lines[0])) + 2;
        int start_x = 2;
        int start_y = (height - box_h) / 2;

        drawWindow("CONTROLS", start_x, start_y, box_w, box_h, RGBColor(80, 120, 200));
        
        ConsoleCell* buf = display_.GetBuffer();
        for (int y = 0; y < (int)(sizeof(lines) / sizeof(lines[0])); ++y) {
            int cy = start_y + y + 1;
            const char* l = lines[y];
            for (int x = 0; l[x]; ++x) {
                int cx = start_x + x + 1;
                if (cx < start_x + box_w - 1 && cy < height) {
                    buf[cy * width + cx].character = l[x];
                    buf[cy * width + cx].color = RGBColor(200, 200, 220);
                }
            }
        }
    }

    // ── Live CPU / APU debug overlay ─────────────────────────────────────────
    void renderDebugOverlay() {
        if (!nes_) return;
        ConsoleCell* buf = display_.GetBuffer();
        if (!buf) return;

        const cynes::CPU::DebugState ds = nes_->cpu.get_debug_state();
        cynes::APU& apu = nes_->apu;

        // ── Layout: right-side panel, 32 wide ────────────────────────────────
        const int PANEL_W   = 34;
        const int TRACE_ROWS = cynes::CPU::TRACE_SIZE;   // one row per trace entry
        const int PANEL_H   = 5               // title + registers line + flags + separator + apu header
                            + 5               // 5 APU channel rows
                            + 1               // separator
                            + 1               // trace header
                            + TRACE_ROWS      // trace entries
                            + 1;              // bottom border
        const int bx = width - PANEL_W - 1;
        const int by = 1;

        if (bx < 0) return;  // terminal too narrow

        RGBColor border_col(80, 160, 255);
        RGBColor header_col(200, 220, 255);
        RGBColor dim_col   (130, 130, 160);
        RGBColor val_col   (255, 220, 100);
        RGBColor flag_on   (100, 255, 120);
        RGBColor flag_off  (80,  80,  100);
        RGBColor apu_on    (100, 255, 180);
        RGBColor apu_off   (70,  70,   90);
        RGBColor trace_col (200, 200, 160);
        RGBColor trace_hi  (255, 255, 100);   // most-recent entry
        RGBColor frozen_col(255, 80,   80);
        RGBColor bg        (10,  12,   20);

        // Helper: write a string into the buffer at (cx, cy).
        auto put = [&](int cx, int cy, const char* s, RGBColor fg) {
            for (; *s && cx < width; ++s, ++cx) {
                if (cx >= 0 && cy >= 0 && cy < height)
                    buf[cy * width + cx] = ConsoleCell(*s, fg, bg, true, false);
            }
        };

        // Helper: write a formatted char[] line.
        char line[64];

        // Draw panel background + border using drawWindow.
        drawWindow("CPU / APU LIVE", bx, by, PANEL_W, PANEL_H, border_col);

        int row = by + 1;

        // ── Registers row ────────────────────────────────────────────────────
        sprintf(line, " PC:%04X  A:%02X X:%02X Y:%02X SP:%02X",
                ds.pc, ds.a, ds.x, ds.y, ds.sp);
        put(bx + 1, row++, line, ds.frozen ? frozen_col : val_col);

        // ── Status flags row ─────────────────────────────────────────────────
        {
            // NVubDIZC — show each flag letter coloured by its state.
            const char flags_label[] = " P:";
            put(bx + 1, row, flags_label, dim_col);
            int fx = bx + 1 + (int)strlen(flags_label);

            struct { const char* name; uint8_t bit; } FLAG_DEFS[] = {
                {"N",0x80},{"V",0x40},{"u",0x20},{"B",0x10},
                {"D",0x08},{"I",0x04},{"Z",0x02},{"C",0x01}
            };
            for (auto& fd : FLAG_DEFS) {
                bool set = (ds.status & fd.bit) != 0;
                put(fx, row, fd.name, set ? flag_on : flag_off);
                fx += (int)strlen(fd.name) + 1;
            }
            row++;
        }

        if (ds.frozen) {
            put(bx + 1, row++, " *** CPU FROZEN (illegal opcode) ***", frozen_col);
        } else {
            put(bx + 1, row++, "", dim_col);  // blank spacer
        }

        // ── APU channels ─────────────────────────────────────────────────────
        put(bx + 1, row++, " APU CHANNELS", header_col);

        const char* ch_names[] = {"Pulse1","Pulse2","Tri   ","Noise ","DMC   "};
        for (uint8_t ch = 0; ch < 5; ++ch) {
            bool active = apu.is_channel_active(ch);
            if (ch < 4) {
                uint16_t period = apu.get_period(ch);
                uint8_t  vol    = apu.get_volume(ch);
                sprintf(line, " %s  %s vol:%2u per:%4u",
                        ch_names[ch], active ? "ON " : "off", vol, period);
            } else {
                uint8_t lvl = apu.get_dmc_level();
                sprintf(line, " %s  %s lvl:%3u", ch_names[ch], active ? "ON " : "off", lvl);
            }
            put(bx + 1, row++, line, active ? apu_on : apu_off);
        }

        // ── Instruction trace ─────────────────────────────────────────────────
        put(bx + 1, row++, " --------------------------------", dim_col);
        put(bx + 1, row++, " RECENT INSTRUCTIONS (oldest->)", header_col);

        for (int i = 0; i < ds.trace_count; ++i) {
            const auto& te = ds.trace[i];
            bool is_latest = (i == ds.trace_count - 1);
            sprintf(line, " %04X  %-3s  A:%02X X:%02X Y:%02X",
                    te.pc, cynes::CPU::opcode_name(te.opcode),
                    te.a, te.x, te.y);
            put(bx + 1, row++, line, is_latest ? trace_hi : trace_col);
            if (row >= by + PANEL_H - 1) break;
        }
    }

    void renderUI() {
        ConsoleCell* buf = display_.GetBuffer();
        if (!buf) return;

        if (show_instructions_) {
            showInstructions();
        }

        if (show_debug_ && nes_) {
            renderDebugOverlay();
        }

        if (!gfx_.show_ui) return;

        // Bottom HUD
        const char* color_names[] = {"TrueColor", "Ansi256", "Ansi16", "Mono"};
        const char* scale_names[] = {"Original", "Fit", "Fill"};
        const char* mode_name = gfx_.pixel_mode ? "Solid" : "Char";
        
        const char* last_slash = rom_path_;
        for(const char* p = rom_path_; *p; ++p) if(*p == '/' || *p == '\\') last_slash = p + 1;

        char info[160];
        sprintf(info, "ROM: %s | FPS: %d | %s | %s | %s", 
                last_slash, (int)current_fps, mode_name, color_names[(int)gfx_.color_mode], scale_names[scale_mode_]);
        
        int box_w = (int)strlen(info) + 4;
        int box_h = 3;
        int bx = (width - box_w) / 2;
        int by = height - 4;

        drawWindow("NES STATUS", bx, by, box_w, box_h, RGBColor(100, 180, 100));
        
        for (int i = 0; info[i]; ++i) {
            int cx = bx + i + 2;
            int cy = by + 1;
            if (cx < width && cy < height) {
                buf[cy * width + cx].character = info[i];
                buf[cy * width + cx].color = RGBColor(220, 220, 180);
            }
        }

        // Status Notification
        if (status_timer_ > 0) {
            int msg_len = (int)strlen(status_msg_);
            int mx = bx + (box_w - msg_len) / 2;
            int my = by - 1;
            for (int i = 0; status_msg_[i]; ++i) {
                int cx = mx + i;
                if (cx >= 0 && cx < width && my >= 0) {
                    buf[my * width + cx] = ConsoleCell(status_msg_[i], status_col_, RGBColor(10, 10, 10), true, false);
                }
            }
        }
    }

    void renderNES() {
        const uint8_t* fb = nes_->get_frame_buffer();
        ConsoleCell* buf = display_.GetBuffer();
        if (!buf) return;

        // NES resolution: 256x240
        int nes_w = 256;
        int nes_h = 240;
        
        int target_w = nes_w;
        int target_h = nes_h;

        if (scale_mode_ == 1) { // Fit
            float aspect = (float)nes_w / (float)nes_h;
            target_h = height - (gfx_.show_ui ? 2 : 0);
            target_w = (int)(target_h * aspect);
            if (target_w > width) {
                target_w = width;
                target_h = (int)(target_w / aspect);
            }
        } else if (scale_mode_ == 2) { // Fill
            target_w = width;
            target_h = height - (gfx_.show_ui ? 1 : 0);
        }

        int start_x = (width - target_w) / 2;
        int start_y = (height - (gfx_.show_ui ? 1 : 0) - target_h) / 2;

        // Clear buffer with solid black to avoid terminal artifacts in borders
        for (int i = 0; i < width * height; ++i) {
            buf[i] = ConsoleCell(' ', RGBColor(0,0,0), RGBColor(0,0,0));
        }

        updateRampCache(gfx_.ascii_ramp);
        const auto& glyphs = getRampCache().glyphs;
        int glyphCount = (int)glyphs.size();

        for (int y = 0; y < target_h; ++y) {
            int cy = start_y + y;
            if (cy < 0 || cy >= height) continue;

            int nes_y = (y * nes_h) / target_h;
            if (nes_y >= nes_h) nes_y = nes_h - 1;

            for (int x = 0; x < target_w; ++x) {
                int cx = start_x + x;
                if (cx < 0 || cx >= width) continue;

                int nes_x = (x * nes_w) / target_w;
                if (nes_x >= nes_w) nes_x = nes_w - 1;

                int nes_idx = (nes_y * nes_w + nes_x) * 3;
                uint8_t r = fb[nes_idx + 0];
                uint8_t g = fb[nes_idx + 1];
                uint8_t b = fb[nes_idx + 2];

                if (gfx_.pixel_mode) {
                    buf[cy * width + cx] = ConsoleCell(' ', RGBColor(r, g, b), RGBColor(r, g, b));
                } else {
                    float luma = (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
                    int glyphIdx = (int)((1.0f - luma) * (glyphCount - 1));
                    if (glyphIdx < 0) glyphIdx = 0;
                    if (glyphIdx >= glyphCount) glyphIdx = glyphCount - 1;

                    buf[cy * width + cx] = ConsoleCell(
                        glyphs[glyphIdx], 
                        RGBColor(r, g, b), 
                        RGBColor(0, 0, 0),
                        true, false
                    );
                }
            }
        }
    }

    const char* rom_path_;
    StrBuf<1024> rom_path_storage_;
    OwnedSingle<cynes::NES> nes_;
    uint16_t controller_state_ = 0;
    GraphicsConfig gfx_;
    Display display_;
    InputManager input_;
    AudioSystem audio_;
    NESSynth synth_;
    const char* status_msg_ = "";
    RGBColor    status_col_;
    float       status_timer_ = 0.f;
    float       timer_accumulator_ = 0.f;
    bool        muted_ = false;
    float       volume_ = 0.5f;
    bool        paused_ = false;
    int scale_mode_ = 0; // 0: Original, 1: Fit, 2: Fill
    bool show_instructions_ = true;
    bool show_debug_ = false;
    // Time-revert ring buffer (10 slots, one every 5s = ~50s of history)
    static constexpr int SNAPSHOT_SLOTS = 10;
    uint8_t*    snap_bufs_[SNAPSHOT_SLOTS] = {};
    size_t      snap_size_        = 0;
    int         snap_write_       = 0;  // next slot to write
    int         snap_count_       = 0;  // how many valid slots
    int         snap_revert_depth_= 0;  // how deep we've rewound
    float       snapshot_timer_   = 0.f;
};

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: RTerminal.exe <path_to_rom.nes>\n");
        printf("Example: RTerminal.exe smb.nes\n");
        
        // Try a fallback default if nothing else
        const char* default_rom = "smb.nes";
        printf("Attempting to load default: %s\n", default_rom);
        
        NESApp app(default_rom);
        app.run();
        return 0;
    }

    const char* rom = argv[1];
    NESApp app(rom);
    app.run();
    return 0;
}