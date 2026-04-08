#include <pulse/simple.h>
#include <pulse/error.h>
#include <iostream>
#include <vector>
#include <mutex>

// Audio bridge to wrap Android API directly to PulseAudio
// Ensures minimal CPU wakeup through a hardcoded buffer limit.

namespace android {
    class AudioTrack {
    public:
        pa_simple *s = nullptr;
        std::vector<char> buffer;
        std::mutex mtx;

        AudioTrack() {
            // Memory constraint: Only allocating room for 4096 frames (4096 * 4 bytes).
            // This safely dodges the 1MB limit while maintaining rapid throughput.
            buffer.reserve(4096 * 4); 
            
            pa_sample_spec ss;
            ss.format = PA_SAMPLE_S16LE; // Standard Android frame
            ss.channels = 2;
            ss.rate = 44100;
            
            int error;
            // Provide NULL as default sink name, PulseAudio will autoselect active BCM2835 device.
            s = pa_simple_new(NULL, "Minecraft", PA_STREAM_PLAYBACK, NULL, "Game Audio", &ss, NULL, NULL, &error);
            if (!s) {
                std::cerr << "PulseAudio initialization failed: " << pa_strerror(error) << std::endl;
            }
        }
        
        ~AudioTrack() {
            if (s) {
                pa_simple_free(s);
            }
        }

        void write(const void* audioData, size_t sizeInBytes) {
            std::lock_guard<std::mutex> lock(mtx);
            if (!s) return;
            int error;
            if (pa_simple_write(s, audioData, sizeInBytes, &error) < 0) {
                std::cerr << "PulseAudio write fault: " << pa_strerror(error) << std::endl;
            }
        }
    };
}

// Mangled export interception for AudioTrack C++ creation
extern "C" {
    void* _ZN7android10AudioTrackC1Ev(void* thiz) {
        // Placement new over the allocated chunk from the game engine
        return new (thiz) android::AudioTrack();
    }
    
    void _ZN7android10AudioTrackD1Ev(android::AudioTrack* thiz) {
        thiz->~AudioTrack();
    }
}
