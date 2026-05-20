#pragma once
#ifndef AUDIO_H
#define AUDIO_H

#include "engine_config.h"
#include "vector.h"
#include "str.h"
#include "atomic.h"
#include "math_utils.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <mmsystem.h>
#  pragma comment(lib, "winmm.lib")
#else
#  include <pthread.h>
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
        return frame_count > 0
            ? (float)frame_count / (float)kAudioSampleRate
            : 0.f;
    }
};

struct AudioVoice {
    const AudioClip* clip      = nullptr;
    float            cursor    = 0.f;
    float            pitch     = 1.f;
    float            volume    = 1.f;
    float            pan       = 0.f;
    
    float            base_volume = 1.f;
    float            base_pitch  = 1.f;
    uint32_t         start_tick  = 0;
    
    Vec3             world_pos;
    bool             is_3d     = false;
    bool             looping   = false;
    bool             active    = false;
    int              id        = -1;
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
    int play(const AudioClip* clip,
             float volume  = 1.f,
             float pitch   = 1.f,
             bool  looping = false);

    int play3D(const AudioClip* clip,
               Vec3  world_pos,
               float volume  = 1.f,
               float pitch   = 1.f,
               bool  looping = false);

    void stop(int voice_id);
    void stopAll();
    bool isPlaying(int voice_id) const;
    float getVoiceCursor(int voice_id) const;

    void setVolume(int voice_id, float volume);
    void setPitch(int voice_id, float pitch);
    void setPan(int voice_id, float pan);
    void update(const AudioListener& listener);

    typedef void (*AudioCallback)(int16_t* out_buf, int frames, void* userdata);
    void setStreamCallback(AudioCallback cb, void* userdata) {
        lockVoices();
        stream_cb_ = cb;
        stream_userdata_ = userdata;
        unlockVoices();
    }
    void setStreamVolume(float v) { stream_volume_ = fast_clamp(v, 0.f, 2.f); }

    void  setMasterVolume(float v) { master_volume_ = fast_clamp(v, 0.f, 1.f); }
    float getMasterVolume()  const { return master_volume_; }

    bool isInitialised() const { return initialised_; }

protected:
    void mixBuffer(int16_t* out_buf);

private:
    void compute3DGain(const AudioVoice& v, const AudioListener& l,
                       float& out_gain_l, float& out_gain_r) const;

    void audioThreadLoop();

#ifdef _WIN32
    HWAVEOUT     wave_out_  = nullptr;
    WAVEHDR      wave_hdrs_[kAudioNumBuffers];
    int16_t*     wave_bufs_[kAudioNumBuffers];
    AtomicInt    free_bufs_;

    static void CALLBACK waveOutCallback(HWAVEOUT, UINT uMsg,
                                          DWORD_PTR dwInstance,
                                          DWORD_PTR, DWORD_PTR);
    static DWORD WINAPI  threadProc(LPVOID param);
    HANDLE       thread_handle_ = nullptr;
    HANDLE       wake_event_    = nullptr;
#else
    void*        pcm_handle_ = nullptr; // snd_pcm_t* on Linux (opaque here)
    pthread_t    thread_id_{};
    static void* threadProc(void* param);
#endif
    AudioVoice    voices_[kAudioMaxVoices];
    int           next_id_       = 1;
    float         master_volume_ = 1.f;
    float         stream_volume_ = 1.f;
    uint32_t      tick_counter_  = 0;
    AudioListener listener_;
    AudioCallback stream_cb_ = nullptr;
    void*         stream_userdata_ = nullptr;

    PodVector<AudioClip*> clips_;

    AtomicBool running_;
    bool       initialised_ = false;
#ifdef _WIN32
    CRITICAL_SECTION voice_cs_;
#else
    pthread_mutex_t  voice_mutex_;
#endif

    FORCE_INLINE void lockVoices() {
#ifdef _WIN32
        EnterCriticalSection(&voice_cs_);
#else
        pthread_mutex_lock(&voice_mutex_);
#endif
    }
    FORCE_INLINE void unlockVoices() {
#ifdef _WIN32
        LeaveCriticalSection(&voice_cs_);
#else
        pthread_mutex_unlock(&voice_mutex_);
#endif
    }
};

#endif