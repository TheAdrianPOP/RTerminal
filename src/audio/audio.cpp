#include "audio.h"
#include "console_utils.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef _WIN32
#  include <unistd.h>
#  if defined(__linux__)
#    include <alsa/asoundlib.h>
#  endif
#endif

static uint16_t wav_u16(const uint8_t* p) {
    return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}
static uint32_t wav_u32(const uint8_t* p) {
    return (uint32_t)((unsigned)p[0] | ((unsigned)p[1]<<8) |
                      ((unsigned)p[2]<<16) | ((unsigned)p[3]<<24));
}
 
static int convertToStereo44(const int16_t* src,
                               int src_frames, int src_channels, int src_rate,
                               int16_t* dst, int dst_capacity)
{
    const float step = (float)src_rate / (float)kAudioSampleRate;
    int out = 0; float pos = 0.f;
    while (out < dst_capacity) {
        int   i0 = (int)pos;
        if (i0 >= src_frames) break;
        int   i1 = (i0 + 1 < src_frames) ? i0 + 1 : i0;
        float t  = pos - (float)i0;
        float mono_l = 0.f, mono_r = 0.f;
        if (src_channels == 1) {
            float s0 = (float)src[i0], s1 = (float)src[i1];
            mono_l = mono_r = s0 + (s1 - s0) * t;
        } else if (src_channels == 2) {
            float l0=(float)src[i0*2+0],l1=(float)src[i1*2+0];
            float r0=(float)src[i0*2+1],r1=(float)src[i1*2+1];
            mono_l = l0+(l1-l0)*t; mono_r = r0+(r1-r0)*t;
        } else {
            float sum0=0.f,sum1=0.f;
            for (int c=0;c<src_channels;++c){
                sum0+=(float)src[i0*src_channels+c];
                sum1+=(float)src[i1*src_channels+c];
            }
            mono_l=mono_r=(sum0+(sum1-sum0)*t)/(float)src_channels;
        }
        auto clamp16=[](float v)->int16_t{
            if(v>32767.f)return 32767;if(v<-32768.f)return-32768;return(int16_t)v;
        };
        dst[out*2+0]=clamp16(mono_l); dst[out*2+1]=clamp16(mono_r);
        pos+=step; ++out;
    }
    return out;
}

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
}

AudioSystem::~AudioSystem() {
    shutdown();

#ifdef _WIN32
    DeleteCriticalSection(&voice_cs_);
#else
    pthread_mutex_destroy(&voice_mutex_);
#endif
}

void AudioSystem::setVolume(int voice_id, float volume) {
    if (voice_id < 0) return;
    lockVoices();
    for (int i = 0; i < kAudioMaxVoices; ++i) {
        if (voices_[i].id == voice_id) {
            voices_[i].base_volume = fast_clamp(volume, 0.f, 2.f);
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
            if (!voices_[i].is_3d) voices_[i].pitch = voices_[i].base_pitch;
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
            break;
        }
    }
    unlockVoices();
}

bool AudioSystem::init() {
    if (initialised_) return true;

#ifdef _WIN32
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

    MMRESULT res = waveOutOpen(&wave_out_, WAVE_MAPPER, &wfx,
                               (DWORD_PTR)waveOutCallback,
                               (DWORD_PTR)this,
                               CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR) {
        CloseHandle(wake_event_); wake_event_ = nullptr;
        return false;
    }

    free_bufs_.store(0);
    for (int i = 0; i < kAudioNumBuffers; ++i) {
        WAVEHDR& hdr     = wave_hdrs_[i];
        hdr.lpData       = (LPSTR)wave_bufs_[i];
        hdr.dwBufferLength = (DWORD)kAudioBufBytes;
        hdr.dwFlags      = 0;
        waveOutPrepareHeader(wave_out_, &hdr, sizeof(WAVEHDR));
        waveOutWrite(wave_out_, &hdr, sizeof(WAVEHDR));
    }

    running_.store(true);
    thread_handle_ = CreateThread(nullptr, 0, threadProc, this, 0, nullptr);
    if (!thread_handle_) { shutdown(); return false; }

#elif defined(__linux__)
    snd_pcm_t* opened = nullptr;
    if (snd_pcm_open(&opened, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
        return false;

    if (snd_pcm_set_params(opened, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                           kAudioChannels, kAudioSampleRate, 1, 500000) < 0) {
        snd_pcm_close(opened);
        return false;
    }

    pcm_handle_ = opened;
    running_.store(true);
    if (pthread_create(&thread_id_, nullptr, threadProc, this) != 0) {
        snd_pcm_close(opened);
        pcm_handle_ = nullptr;
        running_.store(false);
        return false;
    }

#else
    (void)pcm_handle_;
    return false;
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
        CloseHandle(thread_handle_); thread_handle_ = nullptr;
    }
    if (wave_out_) {
        waveOutReset(wave_out_);
        for (int i = 0; i < kAudioNumBuffers; ++i)
            waveOutUnprepareHeader(wave_out_, &wave_hdrs_[i], sizeof(WAVEHDR));
        waveOutClose(wave_out_); wave_out_ = nullptr;
    }
    for (int i = 0; i < kAudioNumBuffers; ++i) {
        delete[] wave_bufs_[i]; wave_bufs_[i] = nullptr;
    }
    if (wake_event_) { CloseHandle(wake_event_); wake_event_ = nullptr; }
#elif defined(__linux__)
    if (pcm_handle_) {
        running_.store(false);
        pthread_join(thread_id_, nullptr);
        auto* pcm = static_cast<snd_pcm_t*>(pcm_handle_);
        snd_pcm_drain(pcm);
        snd_pcm_close(pcm);
        pcm_handle_ = nullptr;
    }
#endif

    for (AudioClip* c : clips_) delete c;
    clips_.clear();

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
 
    // FIX: was fread(buf, 1, fsize, f) with no return-value check.
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        delete[] buf; fclose(f); return nullptr;
    }
    fclose(f);
 
    if (buf[0]!='R'||buf[1]!='I'||buf[2]!='F'||buf[3]!='F'||
        buf[8]!='W'||buf[9]!='A'||buf[10]!='V'||buf[11]!='E') {
        delete[] buf; return nullptr;
    }
 
    uint16_t fmt_tag=0,fmt_channels=0,fmt_bits=0;
    uint32_t fmt_rate=0;
    const uint8_t* data_ptr=nullptr;
    uint32_t data_size=0;
 
    const uint8_t* p=buf+12, *end=buf+(size_t)fsize;
    while (p+8<=end) {
        uint32_t id=wav_u32(p), size=wav_u32(p+4); p+=8;
        if(p+size>end) break;
        if(id==0x20746d66u && size>=16){
            fmt_tag=wav_u16(p); fmt_channels=wav_u16(p+2);
            fmt_rate=wav_u32(p+4); fmt_bits=wav_u16(p+14);
        } else if(id==0x61746164u){ data_ptr=p; data_size=size; }
        p+=size+(size&1u);
    }
 
    if(fmt_tag!=1||!data_ptr||fmt_channels==0||fmt_bits!=16){
        delete[] buf; return nullptr;
    }
 
    int src_frames=(int)(data_size/((uint32_t)fmt_channels*2u));
    if(src_frames<=0){ delete[] buf; return nullptr; }
 
    int dst_cap=(int)((float)src_frames*(float)kAudioSampleRate/(float)fmt_rate)+kAudioSampleRate;
    int16_t* dst=new int16_t[(size_t)(dst_cap*kAudioChannels)];
    int out_frames=convertToStereo44((int16_t*)data_ptr,src_frames,(int)fmt_channels,(int)fmt_rate,dst,dst_cap);
    delete[] buf;
 
    if(out_frames<=0){ delete[] dst; return nullptr; }
 
    AudioClip* clip=new AudioClip();
    clip->samples=dst; clip->frame_count=out_frames; clip->name=filename;
    clips_.push_back(clip);
    return clip;
}

int AudioSystem::play(const AudioClip* clip,
                       float volume, float pitch, bool looping)
{
    if (!clip || clip->frame_count == 0) return -1;

    lockVoices();
    int found = -1;
    for (int i = 0; i < kAudioMaxVoices; ++i) {
        if (!voices_[i].active) { found = i; break; }
    }

    if (found < 0) {
        float min_vol = 1e10f;
        uint32_t oldest_tick = 0xFFFFFFFF;
        for (int i = 0; i < kAudioMaxVoices; ++i) {
            if (voices_[i].volume < min_vol) {
                min_vol = voices_[i].volume;
                oldest_tick = voices_[i].start_tick;
                found = i;
            } else if (voices_[i].volume == min_vol && voices_[i].start_tick < oldest_tick) {
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
    v.volume      = v.base_volume;
    v.pan         = 0.f;
    v.world_pos   = Vec3(0.f, 0.f, 0.f);
    v.is_3d       = false;
    v.looping     = looping;
    v.active      = true;
    v.start_tick  = tick_counter_++;
    v.id          = next_id_++;
    if (next_id_ <= 0) next_id_ = 1;

    int id = v.id;
    unlockVoices();
    return id;
}

int AudioSystem::play3D(const AudioClip* clip,
                         Vec3 world_pos,
                         float volume, float pitch, bool looping)
{
    if (!clip || clip->frame_count == 0) return -1;

    lockVoices();
    int found = -1;
    for (int i = 0; i < kAudioMaxVoices; ++i) {
        if (!voices_[i].active) { found = i; break; }
    }

    if (found < 0) {
        float min_vol = 1e10f;
        uint32_t oldest_tick = 0xFFFFFFFF;
        for (int i = 0; i < kAudioMaxVoices; ++i) {
            if (voices_[i].volume < min_vol) {
                min_vol = voices_[i].volume;
                oldest_tick = voices_[i].start_tick;
                found = i;
            } else if (voices_[i].volume == min_vol && voices_[i].start_tick < oldest_tick) {
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
    v.volume      = 0.f;
    v.pan         = 0.f;
    v.world_pos   = world_pos;
    v.is_3d       = true;
    v.looping     = looping;
    v.active      = true;
    v.start_tick  = tick_counter_++;
    v.id          = next_id_++;
    if (next_id_ <= 0) next_id_ = 1;

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
    for (int i = 0; i < kAudioMaxVoices; ++i)
        if (voices_[i].active && voices_[i].id == voice_id) return true;
    return false;
}

float AudioSystem::getVoiceCursor(int voice_id) const {
    if (voice_id < 0) return -1.f;
    for (int i = 0; i < kAudioMaxVoices; ++i)
        if (voices_[i].active && voices_[i].id == voice_id) return voices_[i].cursor;
    return -1.f;
}

void AudioSystem::compute3DGain(const AudioVoice& v, const AudioListener& l,
                                 float& out_gain_l, float& out_gain_r) const
{
    Vec3  delta = v.world_pos - l.position;
    float dist  = delta.magnitude();

    float gain = 1.f;
    if (dist > kAudioRefDist) {
        float t = fast_clamp((dist - kAudioRefDist) /
                              (kAudioMaxDist - kAudioRefDist), 0.f, 1.f);
        gain = 1.f - t;
    }
    gain *= v.volume;

    float pan = 0.f;
    if (dist > 0.001f) {
        Vec3 dir = delta * (1.f / dist);
        pan = fast_clamp(dir.dot(l.right), -1.f, 1.f);
    }

    float angle  = (pan + 1.f) * (HALF_PI * 0.5f);
    out_gain_l   = gain * fast_cos(angle);
    out_gain_r   = gain * fast_sin(angle);
}

void AudioSystem::update(const AudioListener& listener) {
    if (!initialised_) return;

    lockVoices();
    listener_ = listener;

    for (int i = 0; i < kAudioMaxVoices; ++i) {
        AudioVoice& v = voices_[i];
        if (!v.active || !v.is_3d) continue;

        float gl, gr;
        compute3DGain(v, listener_, gl, gr);

        float total = gl + gr;
        if (total > 0.001f) v.pan = (gr - gl) / total;
        else                 v.pan = 0.f;
        v.volume = (gl + gr) * 0.5f * v.base_volume;
        v.pitch  = v.base_pitch; 
    }
    unlockVoices();
}

void AudioSystem::mixBuffer(int16_t* out_buf) {
    static int32_t mix_l[kAudioMixFrames];
    static int32_t mix_r[kAudioMixFrames];
    memset(mix_l, 0, sizeof(mix_l));
    memset(mix_r, 0, sizeof(mix_r));

    lockVoices();

    const float master = master_volume_;

    for (int vi = 0; vi < kAudioMaxVoices; ++vi) {
        AudioVoice& v = voices_[vi];
        if (!v.active || !v.clip || v.clip->frame_count == 0) continue;

        const int16_t* samples    = v.clip->samples;
        const int      total      = v.clip->frame_count;
        const float    step       = v.pitch;

        float p   = fast_clamp(v.pan, -1.f, 1.f);
        float sc_l = (1.f - p) * 0.5f * v.volume * master;
        float sc_r = (1.f + p) * 0.5f * v.volume * master;

        float cursor = v.cursor;
        bool  done   = false;

        for (int f = 0; f < kAudioMixFrames; ++f) {
            int   i0 = (int)cursor;
            float t  = cursor - (float)i0;

            if (i0 >= total) {
                if (v.looping) {
                    cursor -= (float)total;
                    i0 = (int)cursor;
                    t  = cursor - (float)i0;
                } else { done = true; break; }
            }

            int i1 = i0 + 1;
            if (i1 >= total) i1 = v.looping ? 0 : i0;

            float s_l = (float)samples[i0*2+0] + ((float)samples[i1*2+0] - (float)samples[i0*2+0]) * t;
            float s_r = (float)samples[i0*2+1] + ((float)samples[i1*2+1] - (float)samples[i0*2+1]) * t;

            mix_l[f] += (int32_t)(s_l * sc_l);
            mix_r[f] += (int32_t)(s_r * sc_r);

            cursor += step;
        }

        v.cursor = cursor;
        if (done) { v.active = false; v.clip = nullptr; }
    }

    if (stream_cb_) {
        static int16_t stream_buf[kAudioMixFrames * 2];
        stream_cb_(stream_buf, kAudioMixFrames, stream_userdata_);
        
        const float stream_scale = stream_volume_ * master;
        for (int f = 0; f < kAudioMixFrames; ++f) {
            mix_l[f] += (int32_t)((float)stream_buf[f * 2 + 0] * stream_scale);
            mix_r[f] += (int32_t)((float)stream_buf[f * 2 + 1] * stream_scale);
        }
    }

    unlockVoices();

    for (int f = 0; f < kAudioMixFrames; ++f) {
        int32_t l = mix_l[f];
        int32_t r = mix_r[f];
        if (l >  32767) l =  32767; else if (l < -32768) l = -32768;
        if (r >  32767) r =  32767; else if (r < -32768) r = -32768;
        out_buf[f*2+0] = (int16_t)l;
        out_buf[f*2+1] = (int16_t)r;
    }
}

void AudioSystem::audioThreadLoop() {
#ifdef _WIN32
    int16_t scratch[kAudioMixFrames * kAudioChannels];

    while (running_.load()) {
        WaitForSingleObject(wake_event_, 10);

        for (int i = 0; i < kAudioNumBuffers; ++i) {
            WAVEHDR& hdr = wave_hdrs_[i];
            if (!(hdr.dwFlags & WHDR_DONE)) continue;

            hdr.dwFlags &= ~WHDR_DONE;
            mixBuffer((int16_t*)hdr.lpData);
            waveOutWrite(wave_out_, &hdr, sizeof(WAVEHDR));
        }
    }

    waveOutReset(wave_out_);

#elif defined(__linux__)
    int16_t buf[kAudioMixFrames * kAudioChannels];
    auto* pcm = static_cast<snd_pcm_t*>(pcm_handle_);

    while (running_.load() && pcm) {
        mixBuffer(buf);
        snd_pcm_sframes_t frames = snd_pcm_writei(pcm, buf, kAudioMixFrames);
        if (frames < 0)
            frames = snd_pcm_recover(pcm, (int)frames, 0);
        if (frames < 0)
            break;
    }
#else
    while (running_.load()) {
        platform_sleep(0.05);
    }
#endif
}

#ifdef _WIN32
DWORD WINAPI AudioSystem::threadProc(LPVOID param) {
    static_cast<AudioSystem*>(param)->audioThreadLoop();
    return 0;
}

void CALLBACK AudioSystem::waveOutCallback(HWAVEOUT,
                                                        UINT    uMsg,
                                                        DWORD_PTR dwInstance,
                                                        DWORD_PTR, DWORD_PTR)
{
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