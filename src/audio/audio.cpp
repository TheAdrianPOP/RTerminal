#include "audio.h"
#include "console_utils.h"

// ── Custom CRT File I/O Declarations ─────────────────────────────────────────
#ifndef SEEK_SET
#  define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#  define SEEK_CUR 1
#endif
#ifndef SEEK_END
#  define SEEK_END 2
#endif

extern "C" {
    FILE*  __cdecl fopen(const char* filename, const char* mode);
    int    __cdecl fclose(FILE* stream);
    size_t __cdecl fread(void* ptr, size_t size, size_t count, FILE* stream);
    int    __cdecl fseek(FILE* stream, long offset, int origin);
    long   __cdecl ftell(FILE* stream);
}

#if defined(_WIN32)
static HMODULE             g_winmm_timer_module = nullptr;
static PFN_timeBeginPeriod g_pfn_timeBegin      = nullptr;
static PFN_timeEndPeriod   g_pfn_timeEnd        = nullptr;
static bool                g_winmm_timer_tried  = false;

static void ensure_winmm_timer() {
    if (g_winmm_timer_tried) return;
    g_winmm_timer_tried = true;
    g_winmm_timer_module = LoadLibraryA("winmm.dll");
    if (g_winmm_timer_module) {
        g_pfn_timeBegin = (PFN_timeBeginPeriod)(void*)GetProcAddress(g_winmm_timer_module, "timeBeginPeriod");
        g_pfn_timeEnd   = (PFN_timeEndPeriod)(void*)GetProcAddress(g_winmm_timer_module, "timeEndPeriod");
    }
}

MMRESULT WINAPI Dynamic_timeBeginPeriod(UINT uPeriod) {
    ensure_winmm_timer();
    return g_pfn_timeBegin ? g_pfn_timeBegin(uPeriod) : 11;
}

MMRESULT WINAPI Dynamic_timeEndPeriod(UINT uPeriod) {
    ensure_winmm_timer();
    return g_pfn_timeEnd ? g_pfn_timeEnd(uPeriod) : 11;
}
#endif

/* ── Fast Soft-Knee Saturator (Eliminates digital clipping pops) ──────────── */
static inline int16_t soft_clip16(int32_t sample) {
    constexpr int32_t kThreshold = 24576; // Start soft-limiting at ~75% FS
    if (sample > kThreshold) {
        int64_t diff = sample - kThreshold;
        int32_t sat = kThreshold + (int32_t)((diff * 8191) / (diff + 8191));
        return (int16_t)(sat > 32767 ? 32767 : sat);
    } else if (sample < -kThreshold) {
        int64_t diff = -sample - kThreshold;
        int32_t sat = kThreshold + (int32_t)((diff * 8191) / (diff + 8191));
        return (int16_t)(sat > 32767 ? -32767 : -sat);
    }
    return (int16_t)sample;
}

/* ── WAV Parsing Helpers ─────────────────────────────────────────────────── */
static uint16_t wav_u16(const uint8_t* p) {
    return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}

static uint32_t wav_u32(const uint8_t* p) {
    return (uint32_t)((unsigned)p[0] | ((unsigned)p[1] << 8) |
                      ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24));
}

static int convertToStereo44(const int16_t* src,
                             int src_frames, int src_channels, int src_rate,
                             int16_t* dst, int dst_capacity)
{
    if (src_frames <= 0 || src_channels <= 0 || src_rate <= 0 || dst_capacity <= 0)
        return 0;

    const float step = (float)src_rate / (float)kAudioSampleRate;
    int out = 0;
    float pos = 0.f;

    while (out < dst_capacity) {
        int i0 = (int)pos;
        if (i0 >= src_frames) break;
        int i1 = (i0 + 1 < src_frames) ? i0 + 1 : i0;
        float t = pos - (float)i0;
        float mono_l = 0.f, mono_r = 0.f;

        if (src_channels == 1) {
            float s0 = (float)src[i0], s1 = (float)src[i1];
            mono_l = mono_r = s0 + (s1 - s0) * t;
        } else if (src_channels == 2) {
            float l0 = (float)src[i0 * 2 + 0], l1 = (float)src[i1 * 2 + 0];
            float r0 = (float)src[i0 * 2 + 1], r1 = (float)src[i1 * 2 + 1];
            mono_l = l0 + (l1 - l0) * t;
            mono_r = r0 + (r1 - r0) * t;
        } else {
            float sum0 = 0.f, sum1 = 0.f;
            for (int c = 0; c < src_channels; ++c) {
                sum0 += (float)src[i0 * src_channels + c];
                sum1 += (float)src[i1 * src_channels + c];
            }
            mono_l = mono_r = (sum0 + (sum1 - sum0) * t) / (float)src_channels;
        }

        dst[out * 2 + 0] = soft_clip16((int32_t)mono_l);
        dst[out * 2 + 1] = soft_clip16((int32_t)mono_r);
        pos += step;
        ++out;
    }
    return out;
}

/* ── AudioSystem Implementation ─────────────────────────────────────────── */
AudioSystem::AudioSystem() {
    memset(voices_, 0, sizeof(voices_));
    for (int i = 0; i < kAudioMaxVoices; ++i) voices_[i].id = -1;

#ifdef _WIN32
    memset(wave_hdrs_, 0, sizeof(wave_hdrs_));
    memset(wave_bufs_, 0, sizeof(wave_bufs_));
    InitializeCriticalSection(&voice_cs_);
#else
    pthread_mutex_init(&voice_mutex_, nullptr);
#endif

    master_volume_ = 1.f;
    stream_volume_ = 1.f;
    tick_counter_  = 0;
    running_.store(false);

    mix_l_      = nullptr;
    mix_r_      = nullptr;
    stream_buf_ = nullptr;
}

AudioSystem::~AudioSystem() {
    shutdown();

#ifdef _WIN32
    DeleteCriticalSection(&voice_cs_);
#else
    pthread_mutex_destroy(&voice_mutex_);
#endif

    delete[] mix_l_;
    delete[] mix_r_;
    delete[] stream_buf_;
}

void AudioSystem::computeGains(AudioVoice& v) {
    if (!v.is_3d) {
        // Constant-power panning for 2D audio
        float p = fast_clamp(v.pan, -1.f, 1.f);
        float angle = (p + 1.f) * (3.14159265f * 0.25f);
        v.gain_l = v.base_volume * fast_cos(angle);
        v.gain_r = v.base_volume * fast_sin(angle);
        return;
    }

    // 3D Audio positioning
    Vec3 delta = v.world_pos - listener_.position;
    float dist = delta.magnitude();

    float atten = 1.f;
    if (dist > kAudioRefDist) {
        float t = fast_clamp((dist - kAudioRefDist) / (kAudioMaxDist - kAudioRefDist), 0.f, 1.f);
        atten = 1.f - t;
    }
    float effective_vol = v.base_volume * atten;

    float pan = 0.f;
    if (dist > 0.001f) {
        Vec3 dir = delta * (1.f / dist);
        pan = fast_clamp(dir.dot(listener_.right), -1.f, 1.f);
    }
    v.pan = pan;

    float angle = (pan + 1.f) * (3.14159265f * 0.25f);
    v.gain_l = effective_vol * fast_cos(angle);
    v.gain_r = effective_vol * fast_sin(angle);
}

void AudioSystem::setVolume(int voice_id, float volume) {
    if (voice_id < 0) return;
    lockVoices();
    for (int i = 0; i < kAudioMaxVoices; ++i) {
        if (voices_[i].id == voice_id) {
            voices_[i].base_volume = fast_clamp(volume, 0.f, 2.f);
            computeGains(voices_[i]);
            break;
        }
    }
    unlockVoices();
}

void AudioSystem::setPitch(int voice_id, float pitch) {
    if (voice_id < 0) return;
    lockVoices();
    for (int i = 0; i < kAudioMaxVoices; ++i) {
        if (voices_[i].id == voice_id) {
            voices_[i].base_pitch = fast_clamp(pitch, 0.01f, 4.f);
            voices_[i].pitch = voices_[i].base_pitch;
            break;
        }
    }
    unlockVoices();
}

void AudioSystem::setPan(int voice_id, float pan) {
    if (voice_id < 0) return;
    lockVoices();
    for (int i = 0; i < kAudioMaxVoices; ++i) {
        if (voices_[i].id == voice_id) {
            voices_[i].pan = fast_clamp(pan, -1.f, 1.f);
            computeGains(voices_[i]);
            break;
        }
    }
    unlockVoices();
}

void AudioSystem::setStreamCallback(AudioCallback cb, void* userdata) {
    lockVoices();
    stream_cb_ = cb;
    stream_userdata_ = userdata;
    unlockVoices();
}

bool AudioSystem::init() {
    if (initialised_) return true;

    mix_l_      = new int32_t[kAudioMixFrames];
    mix_r_      = new int32_t[kAudioMixFrames];
    stream_buf_ = new int16_t[kAudioMixFrames * 2];

    memset(mix_l_, 0, kAudioMixFrames * sizeof(int32_t));
    memset(mix_r_, 0, kAudioMixFrames * sizeof(int32_t));
    memset(stream_buf_, 0, kAudioMixFrames * 2 * sizeof(int16_t));

#ifdef _WIN32
    if (!winmm_fn.load()) return false;

    if (winmm_fn.timeBeginPeriod_fn) {
        winmm_fn.timeBeginPeriod_fn(1);
    }

    for (int i = 0; i < kAudioNumBuffers; ++i) {
        wave_bufs_[i] = new int16_t[kAudioMixFrames * kAudioChannels];
        memset(wave_bufs_[i], 0, (size_t)kAudioBufBytes);
    }

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = (WORD)kAudioChannels;
    wfx.nSamplesPerSec  = (DWORD)kAudioSampleRate;
    wfx.wBitsPerSample  = (WORD)(kAudioBytesPerSample * 8);
    wfx.nBlockAlign     = (WORD)(kAudioChannels * kAudioBytesPerSample);
    wfx.nAvgBytesPerSec = (DWORD)(kAudioSampleRate * wfx.nBlockAlign);

    wake_event_ = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!wake_event_) return false;

    MMRESULT res = winmm_fn.waveOutOpen_fn(&wave_out_, WAVE_MAPPER, &wfx,
                                           (DWORD_PTR)waveOutCallback,
                                           (DWORD_PTR)this,
                                           CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR) {
        CloseHandle(wake_event_);
        wake_event_ = nullptr;
        return false;
    }

    free_bufs_.store(0);
    for (int i = 0; i < kAudioNumBuffers; ++i) {
        WAVEHDR& hdr       = wave_hdrs_[i];
        hdr.lpData         = (LPSTR)wave_bufs_[i];
        hdr.dwBufferLength = (DWORD)kAudioBufBytes;
        hdr.dwFlags        = 0;
        winmm_fn.waveOutPrepareHeader_fn(wave_out_, &hdr, sizeof(WAVEHDR));
        winmm_fn.waveOutWrite_fn(wave_out_, &hdr, sizeof(WAVEHDR));
    }

    running_.store(true);
    thread_handle_ = CreateThread(nullptr, 0, threadProc, this, 0, nullptr);
    if (!thread_handle_) { shutdown(); return false; }

#ifdef _WIN32
    SetThreadPriority(thread_handle_, THREAD_PRIORITY_TIME_CRITICAL); // <--- ADD THIS
#endif

#elif defined(__linux__)
    if (!alsa_fn.load()) return false;

    void* opened = nullptr;
    if (alsa_fn.snd_pcm_open_fn(&opened, "default", 0, 0) < 0)
        return false;

    if (alsa_fn.snd_pcm_set_params_fn(opened, 2 /* S16_LE */, 3 /* RW_INTERLEAVED */,
                                      kAudioChannels, kAudioSampleRate, 1, 500000) < 0) {
        alsa_fn.snd_pcm_close_fn(opened);
        return false;
    }

    pcm_handle_ = opened;
    running_.store(true);
    if (pthread_create(&thread_id_, nullptr, threadProc, this) != 0) {
        alsa_fn.snd_pcm_close_fn(opened);
        pcm_handle_ = nullptr;
        running_.store(false);
        return false;
    }
#endif

    initialised_ = true;
    return true;
}

void AudioSystem::shutdown() {
    if (!initialised_) return;

    running_.store(false);

#ifdef _WIN32
    if (wake_event_) SetEvent(wake_event_);
    if (thread_handle_) {
        WaitForSingleObject(thread_handle_, 2000);
        CloseHandle(thread_handle_);
        thread_handle_ = nullptr;
    }
    if (wave_out_) {
        if (winmm_fn.waveOutReset_fn) winmm_fn.waveOutReset_fn(wave_out_);
        for (int i = 0; i < kAudioNumBuffers; ++i) {
            if (winmm_fn.waveOutUnprepareHeader_fn) {
                winmm_fn.waveOutUnprepareHeader_fn(wave_out_, &wave_hdrs_[i], sizeof(WAVEHDR));
            }
        }
        if (winmm_fn.waveOutClose_fn) winmm_fn.waveOutClose_fn(wave_out_);
        wave_out_ = nullptr;
    }
    for (int i = 0; i < kAudioNumBuffers; ++i) {
        delete[] wave_bufs_[i];
        wave_bufs_[i] = nullptr;
    }
    if (wake_event_) { CloseHandle(wake_event_); wake_event_ = nullptr; }

    if (winmm_fn.timeEndPeriod_fn) {
        winmm_fn.timeEndPeriod_fn(1);
    }
    winmm_fn.unload();
#elif defined(__linux__)
    if (pcm_handle_) {
        pthread_join(thread_id_, nullptr);
        if (alsa_fn.snd_pcm_drain_fn) alsa_fn.snd_pcm_drain_fn(pcm_handle_);
        if (alsa_fn.snd_pcm_close_fn) alsa_fn.snd_pcm_close_fn(pcm_handle_);
        pcm_handle_ = nullptr;
    }
    alsa_fn.unload();
#endif

    for (AudioClip* c : clips_) delete c;
    clips_.clear();

    delete[] mix_l_; mix_l_ = nullptr;
    delete[] mix_r_; mix_r_ = nullptr;
    delete[] stream_buf_; stream_buf_ = nullptr;

    initialised_ = false;
}

AudioClip* AudioSystem::loadWAV(const char* filename) {
    if (!filename) return nullptr;
    for (AudioClip* c : clips_)
        if (c->name == filename) return c;

    FILE* f = fopen(filename, "rb");
    if (!f) return nullptr;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 44) { fclose(f); return nullptr; }

    uint8_t* buf = new uint8_t[(size_t)fsize];
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        delete[] buf; fclose(f); return nullptr;
    }
    fclose(f);

    if (buf[0]!='R'||buf[1]!='I'||buf[2]!='F'||buf[3]!='F'||
        buf[8]!='W'||buf[9]!='A'||buf[10]!='V'||buf[11]!='E') {
        delete[] buf; return nullptr;
    }

    uint16_t fmt_tag = 0, fmt_channels = 0, fmt_bits = 0;
    uint32_t fmt_rate = 0;
    const uint8_t* data_ptr = nullptr;
    uint32_t data_size = 0;

    const uint8_t* p = buf + 12, *end = buf + (size_t)fsize;
    while (p + 8 <= end) {
        uint32_t id = wav_u32(p), size = wav_u32(p + 4);
        p += 8;
        if (p + size > end) {
            size = (uint32_t)(end - p);
        }
        if (id == 0x20746d66u && size >= 16) { // "fmt "
            fmt_tag      = wav_u16(p);
            fmt_channels = wav_u16(p + 2);
            fmt_rate     = wav_u32(p + 4);
            fmt_bits     = wav_u16(p + 14);
        } else if (id == 0x61746164u) {        // "data"
            data_ptr  = p;
            data_size = size;
        }
        p += size + (size & 1u);
    }

    if (fmt_tag != 1 || !data_ptr || fmt_channels == 0 || fmt_bits != 16 || fmt_rate == 0) {
        delete[] buf; return nullptr;
    }

    int src_frames = (int)(data_size / ((uint32_t)fmt_channels * 2u));
    if (src_frames <= 0) { delete[] buf; return nullptr; }

    int dst_cap = (int)(((int64_t)src_frames * kAudioSampleRate) / fmt_rate) + 64;
    int16_t* dst = new int16_t[(size_t)(dst_cap * kAudioChannels)];
    int out_frames = convertToStereo44((const int16_t*)data_ptr, src_frames, (int)fmt_channels, (int)fmt_rate, dst, dst_cap);
    delete[] buf;

    if (out_frames <= 0) { delete[] dst; return nullptr; }

    AudioClip* clip = new AudioClip();
    clip->samples = dst;
    clip->frame_count = out_frames;
    clip->name = filename;
    clips_.push_back(clip);
    return clip;
}

int AudioSystem::play(const AudioClip* clip, float volume, float pitch, bool looping) {
    if (!clip || clip->frame_count == 0) return -1;

    lockVoices();
    int found = -1;
    for (int i = 0; i < kAudioMaxVoices; ++i) {
        if (!voices_[i].active) { found = i; break; }
    }

    // Voice stealing with preference to lowest volume and oldest playback
    if (found < 0) {
        float min_gain = 1e10f;
        uint32_t oldest_tick = 0xFFFFFFFF;
        for (int i = 0; i < kAudioMaxVoices; ++i) {
            float g = voices_[i].gain_l + voices_[i].gain_r;
            if (g < min_gain || (g == min_gain && voices_[i].start_tick < oldest_tick)) {
                min_gain = g;
                oldest_tick = voices_[i].start_tick;
                found = i;
            }
        }
    }

    if (found < 0) { unlockVoices(); return -1; }

    AudioVoice& v = voices_[found];
    v.clip        = clip;
    v.cursor      = 0.f;
    v.base_pitch  = pitch > 0.f ? pitch : 1.f;
    v.base_volume = fast_clamp(volume, 0.f, 2.f);
    v.pitch       = v.base_pitch;
    v.pan         = 0.f;
    v.world_pos   = Vec3(0.f, 0.f, 0.f);
    v.is_3d       = false;
    v.looping     = looping;
    v.start_tick  = tick_counter_++;
    v.id          = next_id_++;
    if (next_id_ <= 0) next_id_ = 1;

    computeGains(v);
    v.active = true;

    int id = v.id;
    unlockVoices();
    return id;
}

int AudioSystem::play3D(const AudioClip* clip, Vec3 world_pos, float volume, float pitch, bool looping) {
    if (!clip || clip->frame_count == 0) return -1;

    lockVoices();
    int found = -1;
    for (int i = 0; i < kAudioMaxVoices; ++i) {
        if (!voices_[i].active) { found = i; break; }
    }

    if (found < 0) {
        float min_gain = 1e10f;
        uint32_t oldest_tick = 0xFFFFFFFF;
        for (int i = 0; i < kAudioMaxVoices; ++i) {
            float g = voices_[i].gain_l + voices_[i].gain_r;
            if (g < min_gain || (g == min_gain && voices_[i].start_tick < oldest_tick)) {
                min_gain = g;
                oldest_tick = voices_[i].start_tick;
                found = i;
            }
        }
    }

    if (found < 0) { unlockVoices(); return -1; }

    AudioVoice& v = voices_[found];
    v.clip        = clip;
    v.cursor      = 0.f;
    v.base_pitch  = pitch > 0.f ? pitch : 1.f;
    v.base_volume = fast_clamp(volume, 0.f, 2.f);
    v.pitch       = v.base_pitch;
    v.pan         = 0.f;
    v.world_pos   = world_pos;
    v.is_3d       = true;
    v.looping     = looping;
    v.start_tick  = tick_counter_++;
    v.id          = next_id_++;
    if (next_id_ <= 0) next_id_ = 1;

    computeGains(v);
    v.active = true;

    int id = v.id;
    unlockVoices();
    return id;
}

void AudioSystem::stop(int voice_id) {
    if (voice_id < 0) return;
    lockVoices();
    for (int i = 0; i < kAudioMaxVoices; ++i) {
        if (voices_[i].active && voices_[i].id == voice_id) {
            voices_[i].active = false;
            voices_[i].clip   = nullptr;
            break;
        }
    }
    unlockVoices();
}

void AudioSystem::stopAll() {
    lockVoices();
    for (int i = 0; i < kAudioMaxVoices; ++i) {
        voices_[i].active = false;
        voices_[i].clip   = nullptr;
    }
    unlockVoices();
}

bool AudioSystem::isPlaying(int voice_id) const {
    if (voice_id < 0) return false;
    lockVoices();
    for (int i = 0; i < kAudioMaxVoices; ++i) {
        if (voices_[i].active && voices_[i].id == voice_id) {
            unlockVoices();
            return true;
        }
    }
    unlockVoices();
    return false;
}

float AudioSystem::getVoiceCursor(int voice_id) const {
    if (voice_id < 0) return -1.f;
    lockVoices();
    for (int i = 0; i < kAudioMaxVoices; ++i) {
        if (voices_[i].active && voices_[i].id == voice_id) {
            float c = voices_[i].cursor;
            unlockVoices();
            return c;
        }
    }
    unlockVoices();
    return -1.f;
}

void AudioSystem::update(const AudioListener& listener) {
    if (!initialised_) return;

    lockVoices();
    listener_ = listener;
    for (int i = 0; i < kAudioMaxVoices; ++i) {
        AudioVoice& v = voices_[i];
        if (v.active && v.is_3d) {
            computeGains(v);
        }
    }
    unlockVoices();
}

void AudioSystem::mixBuffer(int16_t* out_buf) {
    if (!mix_l_ || !mix_r_) return;

    memset(mix_l_, 0, kAudioMixFrames * sizeof(int32_t));
    memset(mix_r_, 0, kAudioMixFrames * sizeof(int32_t));

    lockVoices();

    const float master = master_volume_;

    for (int vi = 0; vi < kAudioMaxVoices; ++vi) {
        AudioVoice& v = voices_[vi];
        if (!v.active || !v.clip || v.clip->frame_count == 0) continue;

        const int16_t* samples = v.clip->samples;
        const int      total   = v.clip->frame_count;
        const float    step    = v.pitch;

        const float sc_l = v.gain_l * master;
        const float sc_r = v.gain_r * master;

        float cursor = v.cursor;
        bool  done   = false;

        for (int f = 0; f < kAudioMixFrames; ++f) {
            if (cursor >= (float)total) {
                if (v.looping) {
                    while (cursor >= (float)total) cursor -= (float)total;
                } else {
                    done = true;
                    break;
                }
            }

            int   i0 = (int)cursor;
            float t  = cursor - (float)i0;
            int   i1 = i0 + 1;
            if (i1 >= total) {
                i1 = v.looping ? 0 : i0;
            }

            float s_l = (float)samples[i0 * 2 + 0] + ((float)samples[i1 * 2 + 0] - (float)samples[i0 * 2 + 0]) * t;
            float s_r = (float)samples[i0 * 2 + 1] + ((float)samples[i1 * 2 + 1] - (float)samples[i0 * 2 + 1]) * t;

            mix_l_[f] += (int32_t)(s_l * sc_l);
            mix_r_[f] += (int32_t)(s_r * sc_r);

            cursor += step;
        }

        v.cursor = cursor;
        if (done) {
            v.active = false;
            v.clip   = nullptr;
        }
    }

    AudioCallback active_cb = stream_cb_;
    void* active_ud = stream_userdata_;
    const float active_stream_vol = stream_volume_;

    unlockVoices();

    // Invoke stream callback outside voice lock to prevent audio thread lock contention
    if (active_cb && stream_buf_) {
        active_cb(stream_buf_, kAudioMixFrames, active_ud);
        const float stream_scale = active_stream_vol * master;
        for (int f = 0; f < kAudioMixFrames; ++f) {
            mix_l_[f] += (int32_t)((float)stream_buf_[f * 2 + 0] * stream_scale);
            mix_r_[f] += (int32_t)((float)stream_buf_[f * 2 + 1] * stream_scale);
        }
    }

    // Soft-limit saturator to prevent digital clicks
    for (int f = 0; f < kAudioMixFrames; ++f) {
        out_buf[f * 2 + 0] = soft_clip16(mix_l_[f]);
        out_buf[f * 2 + 1] = soft_clip16(mix_r_[f]);
    }
}

void AudioSystem::audioThreadLoop() {
#ifdef _WIN32
    while (running_.load()) {
        WaitForSingleObject(wake_event_, 10);

        for (int i = 0; i < kAudioNumBuffers; ++i) {
            WAVEHDR& hdr = wave_hdrs_[i];
            if (!(hdr.dwFlags & WHDR_DONE)) continue;

            hdr.dwFlags &= ~WHDR_DONE;
            mixBuffer((int16_t*)hdr.lpData);
            if (winmm_fn.waveOutWrite_fn) {
                winmm_fn.waveOutWrite_fn(wave_out_, &hdr, sizeof(WAVEHDR));
            }
        }
    }
#elif defined(__linux__)
    int16_t buf[kAudioMixFrames * kAudioChannels];

    while (running_.load() && pcm_handle_) {
        mixBuffer(buf);
        if (alsa_fn.snd_pcm_writei_fn) {
            long frames = alsa_fn.snd_pcm_writei_fn(pcm_handle_, buf, kAudioMixFrames);
            if (frames < 0 && alsa_fn.snd_pcm_recover_fn) {
                frames = alsa_fn.snd_pcm_recover_fn(pcm_handle_, (int)frames, 0);
            }
            if (frames < 0) break;
        }
    }
#endif
}

#ifdef _WIN32
DWORD WINAPI AudioSystem::threadProc(LPVOID param) {
    static_cast<AudioSystem*>(param)->audioThreadLoop();
    return 0;
}

void CALLBACK AudioSystem::waveOutCallback(HWAVEOUT, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR, DWORD_PTR) {
    if (uMsg == WOM_DONE) {
        AudioSystem* self = reinterpret_cast<AudioSystem*>(dwInstance);
        SetEvent(self->wake_event_);
    }
}
#else
void* AudioSystem::threadProc(void* param) {
    static_cast<AudioSystem*>(param)->audioThreadLoop();
    return nullptr;
}
#endif