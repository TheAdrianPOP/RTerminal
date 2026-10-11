#pragma once
#ifndef AUDIO_H
#define AUDIO_H

#include "engine_config.h"
#include "vector.h"
#include "str.h"
#include "atomic.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <mmsystem.h>
#else
#  include <pthread.h>
#  include <dlfcn.h>
#endif

#if defined(_WIN32)
MMRESULT WINAPI Dynamic_timeBeginPeriod(UINT uPeriod);
MMRESULT WINAPI Dynamic_timeEndPeriod(UINT uPeriod);

#  undef timeBeginPeriod
#  undef timeEndPeriod
#  define timeBeginPeriod Dynamic_timeBeginPeriod
#  define timeEndPeriod Dynamic_timeEndPeriod
#endif

#ifdef _WIN32
typedef MMRESULT (WINAPI *PFN_waveOutOpen)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
typedef MMRESULT (WINAPI *PFN_waveOutClose)(HWAVEOUT);
typedef MMRESULT (WINAPI *PFN_waveOutPrepareHeader)(HWAVEOUT, LPWAVEHDR, UINT);
typedef MMRESULT (WINAPI *PFN_waveOutUnprepareHeader)(HWAVEOUT, LPWAVEHDR, UINT);
typedef MMRESULT (WINAPI *PFN_waveOutWrite)(HWAVEOUT, LPWAVEHDR, UINT);
typedef MMRESULT (WINAPI *PFN_waveOutReset)(HWAVEOUT);

typedef MMRESULT (WINAPI *PFN_timeBeginPeriod)(UINT);
typedef MMRESULT (WINAPI *PFN_timeEndPeriod)(UINT);

struct WinMMFunctions {
    HMODULE module = nullptr;
    PFN_waveOutOpen waveOutOpen_fn = nullptr;
    PFN_waveOutClose waveOutClose_fn = nullptr;
    PFN_waveOutPrepareHeader waveOutPrepareHeader_fn = nullptr;
    PFN_waveOutUnprepareHeader waveOutUnprepareHeader_fn = nullptr;
    PFN_waveOutWrite waveOutWrite_fn = nullptr;
    PFN_waveOutReset waveOutReset_fn = nullptr;
    PFN_timeBeginPeriod timeBeginPeriod_fn = nullptr;
    PFN_timeEndPeriod timeEndPeriod_fn = nullptr;

    bool load() {
        if (module) return true;
        module = LoadLibraryA("winmm.dll");
        if (!module) return false;

        waveOutOpen_fn            = (PFN_waveOutOpen)(void*)GetProcAddress(module, "waveOutOpen");
        waveOutClose_fn           = (PFN_waveOutClose)(void*)GetProcAddress(module, "waveOutClose");
        waveOutPrepareHeader_fn   = (PFN_waveOutPrepareHeader)(void*)GetProcAddress(module, "waveOutPrepareHeader");
        waveOutUnprepareHeader_fn = (PFN_waveOutUnprepareHeader)(void*)GetProcAddress(module, "waveOutUnprepareHeader");
        waveOutWrite_fn           = (PFN_waveOutWrite)(void*)GetProcAddress(module, "waveOutWrite");
        waveOutReset_fn           = (PFN_waveOutReset)(void*)GetProcAddress(module, "waveOutReset");
        timeBeginPeriod_fn        = (PFN_timeBeginPeriod)(void*)GetProcAddress(module, "timeBeginPeriod");
        timeEndPeriod_fn          = (PFN_timeEndPeriod)(void*)GetProcAddress(module, "timeEndPeriod");

        return waveOutOpen_fn && waveOutClose_fn && waveOutPrepareHeader_fn &&
               waveOutUnprepareHeader_fn && waveOutWrite_fn && waveOutReset_fn;
    }

    void unload() {
        if (module) {
            FreeLibrary(module);
            module = nullptr;
        }
        waveOutOpen_fn = nullptr;
        waveOutClose_fn = nullptr;
        waveOutPrepareHeader_fn = nullptr;
        waveOutUnprepareHeader_fn = nullptr;
        waveOutWrite_fn = nullptr;
        waveOutReset_fn = nullptr;
        timeBeginPeriod_fn = nullptr;
        timeEndPeriod_fn = nullptr;
    }
};
#else
typedef int  (*PFN_snd_pcm_open)(void**, const char*, int, int);
typedef int  (*PFN_snd_pcm_set_params)(void*, int, int, unsigned int, unsigned int, int, unsigned int);
typedef int  (*PFN_snd_pcm_close)(void*);
typedef int  (*PFN_snd_pcm_drain)(void*);
typedef long (*PFN_snd_pcm_writei)(void*, const void*, unsigned long);
typedef int  (*PFN_snd_pcm_recover)(void*, int, int);

struct AlsaFunctions {
    void* module = nullptr;
    PFN_snd_pcm_open snd_pcm_open_fn = nullptr;
    PFN_snd_pcm_set_params snd_pcm_set_params_fn = nullptr;
    PFN_snd_pcm_close snd_pcm_close_fn = nullptr;
    PFN_snd_pcm_drain snd_pcm_drain_fn = nullptr;
    PFN_snd_pcm_writei snd_pcm_writei_fn = nullptr;
    PFN_snd_pcm_recover snd_pcm_recover_fn = nullptr;

    bool load() {
        if (module) return true;
        module = dlopen("libasound.so.2", RTLD_LAZY | RTLD_LOCAL);
        if (!module) module = dlopen("libasound.so", RTLD_LAZY | RTLD_LOCAL);
        if (!module) return false;

        snd_pcm_open_fn       = (PFN_snd_pcm_open)dlsym(module, "snd_pcm_open");
        snd_pcm_set_params_fn = (PFN_snd_pcm_set_params)dlsym(module, "snd_pcm_set_params");
        snd_pcm_close_fn      = (PFN_snd_pcm_close)dlsym(module, "snd_pcm_close");
        snd_pcm_drain_fn      = (PFN_snd_pcm_drain)dlsym(module, "snd_pcm_drain");
        snd_pcm_writei_fn     = (PFN_snd_pcm_writei)dlsym(module, "snd_pcm_writei");
        snd_pcm_recover_fn    = (PFN_snd_pcm_recover)dlsym(module, "snd_pcm_recover");

        return snd_pcm_open_fn && snd_pcm_set_params_fn && snd_pcm_close_fn &&
               snd_pcm_drain_fn && snd_pcm_writei_fn && snd_pcm_recover_fn;
    }

    void unload() {
        if (module) {
            dlclose(module);
            module = nullptr;
        }
        snd_pcm_open_fn = nullptr;
        snd_pcm_set_params_fn = nullptr;
        snd_pcm_close_fn = nullptr;
        snd_pcm_drain_fn = nullptr;
        snd_pcm_writei_fn = nullptr;
        snd_pcm_recover_fn = nullptr;
    }
};
#endif

struct AudioClip {
    int16_t*    samples     = nullptr;
    int         frame_count = 0;
    StrBuf<256> name;

    AudioClip()  = default;
    ~AudioClip() { delete[] samples; }

    AudioClip(const AudioClip&)            = delete;
    AudioClip& operator=(const AudioClip&) = delete;

    float duration() const {
        return frame_count > 0 ? (float)frame_count / (float)kAudioSampleRate : 0.f;
    }
};

struct AudioVoice {
    const AudioClip* clip       = nullptr;
    float            cursor     = 0.f;
    float            pitch      = 1.f;
    float            gain_l     = 1.f;
    float            gain_r     = 1.f;
    
    float            base_volume = 1.f;
    float            base_pitch  = 1.f;
    float            pan         = 0.f;
    uint32_t         start_tick  = 0;
    
    Vec3             world_pos;
    bool             is_3d      = false;
    bool             looping    = false;
    bool             active     = false;
    int              id         = -1;
};

struct AudioListener {
    Vec3 position;
    Vec3 forward = Vec3(0.f, 0.f, -1.f);
    Vec3 right   = Vec3(1.f, 0.f,  0.f);
};

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    bool init();
    void shutdown();
    AudioClip* loadWAV(const char* filename);

    int play(const AudioClip* clip, float volume = 1.f, float pitch = 1.f, bool looping = false);
    int play3D(const AudioClip* clip, Vec3 world_pos, float volume = 1.f, float pitch = 1.f, bool looping = false);

    void stop(int voice_id);
    void stopAll();
    bool isPlaying(int voice_id) const;
    float getVoiceCursor(int voice_id) const;

    void setVolume(int voice_id, float volume);
    void setPitch(int voice_id, float pitch);
    void setPan(int voice_id, float pan);
    void update(const AudioListener& listener);

    typedef void (*AudioCallback)(int16_t* out_buf, int frames, void* userdata);
    void setStreamCallback(AudioCallback cb, void* userdata);
    void setStreamVolume(float v) { stream_volume_ = fast_clamp(v, 0.f, 2.f); }

    void  setMasterVolume(float v) { master_volume_ = fast_clamp(v, 0.f, 1.f); }
    float getMasterVolume()  const { return master_volume_; }

    bool isInitialised() const { return initialised_; }

protected:
    void mixBuffer(int16_t* out_buf);

private:
    void computeGains(AudioVoice& v);
    void audioThreadLoop();

#ifdef _WIN32
    WinMMFunctions winmm_fn;
    HWAVEOUT     wave_out_  = nullptr;
    WAVEHDR      wave_hdrs_[kAudioNumBuffers];
    int16_t*     wave_bufs_[kAudioNumBuffers];
    AtomicInt    free_bufs_;

    static void CALLBACK waveOutCallback(HWAVEOUT, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR, DWORD_PTR);
    static DWORD WINAPI  threadProc(LPVOID param);
    HANDLE       thread_handle_ = nullptr;
    HANDLE       wake_event_    = nullptr;
#else
    AlsaFunctions alsa_fn;
    void*        pcm_handle_ = nullptr;
    pthread_t    thread_id_{};
    static void* threadProc(void* param);
#endif

    AudioVoice    voices_[kAudioMaxVoices];
    int           next_id_       = 1;
    float         master_volume_ = 1.f;
    float         stream_volume_ = 1.f;
    uint32_t      tick_counter_  = 0;
    AudioListener listener_;
    AudioCallback stream_cb_     = nullptr;
    void*         stream_userdata_ = nullptr;

    PodVector<AudioClip*> clips_;

    AtomicBool running_;
    bool       initialised_ = false;

    // Mutable to enable thread-safe read locks in const methods
#ifdef _WIN32
    mutable CRITICAL_SECTION voice_cs_;
#else
    mutable pthread_mutex_t  voice_mutex_;
#endif

    int32_t*     mix_l_      = nullptr;
    int32_t*     mix_r_      = nullptr;
    int16_t*     stream_buf_ = nullptr;

    FORCE_INLINE void lockVoices() const {
#ifdef _WIN32
        EnterCriticalSection(&voice_cs_);
#else
        pthread_mutex_lock(&voice_mutex_);
#endif
    }
    FORCE_INLINE void unlockVoices() const {
#ifdef _WIN32
        LeaveCriticalSection(&voice_cs_);
#else
        pthread_mutex_unlock(&voice_mutex_);
#endif
    }
};

#endif // AUDIO_H