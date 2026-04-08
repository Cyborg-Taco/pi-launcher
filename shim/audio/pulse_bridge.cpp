/**
 * Audio Bridge - Android AudioTrack to PulseAudio
 * 
 * Bridges Android's AudioTrack API to PulseAudio with optimized buffer
 * settings for Raspberry Pi 4 (1GB RAM, limited CPU).
 * 
 * Buffer: 4096 samples (83ms at 48kHz) - minimizes CPU wake-ups
 * 
 * Pi-specific assumptions:
 * - PulseAudio running with default sink
 * - Sample rate: 48000 Hz (standard for Android)
 * - Channels: 2 (stereo)
 * - Format: 16-bit signed integer (S16LE)
 */

#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <queue>

// ============================================================================
// Constants
// ============================================================================

// Buffer size optimized for Pi 4 - balance between latency and CPU usage
// 4096 samples = ~83ms at 48kHz = minimal wake-ups
static const int AUDIO_BUFFER_SAMPLES = 4096;
static const int AUDIO_SAMPLE_RATE = 48000;
static const int AUDIO_CHANNELS = 2;
static const int AUDIO_BYTES_PER_SAMPLE = 2;  // 16-bit
static const int AUDIO_BUFFER_SIZE = AUDIO_BUFFER_SAMPLES * AUDIO_CHANNELS * AUDIO_BYTES_PER_SAMPLE;

// ============================================================================
// Global State
// ============================================================================

struct AudioTrack {
    int sampleRate;
    int channels;
    int format;
    int bufferSize;
    bool playing;
    
    AudioTrack() : sampleRate(AUDIO_SAMPLE_RATE), channels(AUDIO_CHANNELS),
                   format(0), bufferSize(AUDIO_BUFFER_SIZE), playing(false) {}
};

static AudioTrack g_audioTrack;
static pa_simple* g_paStream = nullptr;
static std::atomic<bool> g_audioRunning(false);
static std::thread g_audioThread;
static std::mutex g_audioMutex;

// Audio buffer queue (thread-safe)
static std::queue<std::vector<int16_t>> g_audioQueue;
static const size_t MAX_QUEUE_SIZE = 8;  // Limit queued buffers to save memory

// ============================================================================
// PulseAudio Initialization
// ============================================================================

/**
 * Initialize PulseAudio stream
 */
static bool initPulseAudio() {
    if (g_paStream) return true;
    
    int error;
    
    // Sample format: 16-bit signed, little-endian, stereo
    static pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.channels = g_audioTrack.channels;
    ss.rate = g_audioTrack.sampleRate;
    
    std::cout << "[Audio] PulseAudio config: " << ss.rate << "Hz, " 
              << (int)ss.channels << "ch, 16-bit" << std::endl;
    
    // Create optimized stream attributes
    // Lower latency values = more CPU usage but lower delay
    // Higher values = less CPU but more delay
    static pa_buffer_attr buf;
    buf.maxlength = (uint32_t)-1;           // No maximum
    buf.fragsize = AUDIO_BUFFER_SIZE;       // Fragment size = our buffer
    
    // Create the stream
    g_paStream = pa_simple_new(
        nullptr,                    // Use default server
        "Minecraft-Bedrock-Pi",     // Application name
        PA_PLAYBACK,                // Playback stream
        nullptr,                    // Use default device
        "Minecraft Audio",          // Stream name
        &ss,                        // Sample format
        nullptr,                    // Use default channel map
        &buf,                       // Buffer attributes
        &error                      // Error code
    );
    
    if (!g_paStream) {
        std::cerr << "[Audio] Failed to create PulseAudio stream: " 
                  << pa_strerror(error) << std::endl;
        return false;
    }
    
    std::cout << "[Audio] PulseAudio stream created (buffer: " 
              << AUDIO_BUFFER_SAMPLES << " samples)" << std::endl;
    return true;
}

/**
 * Cleanup PulseAudio stream
 */
static void cleanupPulseAudio() {
    if (g_paStream) {
        pa_simple_free(g_paStream);
        g_paStream = nullptr;
    }
}

// ============================================================================
// Android AudioTrack API Implementation
// ============================================================================

extern "C" {

/**
 * Initialize Android audio system
 * Called from game to set up audio
 * 
 * Buffer size reasoning: 4096 samples at 48kHz = 83ms
 * This is a balance between:
 * - Latency (we want < 100ms for responsive audio)
 * - CPU usage (Pi 4 ARM Cortex-A72 can handle this)
 * - Memory (8192 bytes per buffer is trivial on 1GB system)
 */
void android_audio_init(int sampleRate, int channels) {
    std::cout << "[Audio] Initializing Android audio (rate=" << sampleRate 
              << ", ch=" << channels << ")" << std::endl;
    
    g_audioTrack.sampleRate = sampleRate > 0 ? sampleRate : AUDIO_SAMPLE_RATE;
    g_audioTrack.channels = channels > 0 ? channels : AUDIO_CHANNELS;
    
    // Allocate buffer for audio playback
    // Size: 4096 samples * 2 channels * 2 bytes = 16KB
    // This is the primary audio buffer - kept small to minimize memory usage
    g_audioTrack.bufferSize = AUDIO_BUFFER_SAMPLES * g_audioTrack.channels * AUDIO_BYTES_PER_SAMPLE;
    
    // Initialize PulseAudio
    if (!initPulseAudio()) {
        std::cerr << "[Audio] WARNING: PulseAudio init failed, audio will not work" << std::endl;
    }
    
    g_audioRunning = true;
    std::cout << "[Audio] Audio initialized (buffer: " << g_audioTrack.bufferSize << " bytes)" << std::endl;
}

/**
 * Play audio samples through PulseAudio
 * 
 * Samples: int16_t array of audio data
 * numSamples: number of samples (per channel) to play
 * 
 * Note: This receives interleaved stereo data from Android
 */
void android_audio_play(const int16_t* samples, int numSamples) {
    if (!g_paStream || !samples || numSamples <= 0) {
        return;
    }
    
    // Ensure we have the right amount of data
    // numSamples is per-channel, so total samples = numSamples * channels
    int totalSamples = numSamples * g_audioTrack.channels;
    
    // Handle case where numSamples doesn't match our buffer exactly
    static std::vector<int16_t> buffer;
    buffer.resize(AUDIO_BUFFER_SAMPLES * g_audioTrack.channels);
    
    if (totalSamples >= (int)buffer.size()) {
        // Full buffer - copy directly
        memcpy(buffer.data(), samples, buffer.size() * sizeof(int16_t));
    } else {
        // Partial buffer - copy what we have, zero-pad rest
        memcpy(buffer.data(), samples, totalSamples * sizeof(int16_t));
        memset(buffer.data() + totalSamples, 0, 
               (buffer.size() - totalSamples) * sizeof(int16_t));
    }
    
    int error;
    int result = pa_simple_write(g_paStream, buffer.data(), 
                                  buffer.size() * sizeof(int16_t), &error);
    
    if (result < 0) {
        std::cerr << "[Audio] Write error: " << pa_strerror(error) << std::endl;
    }
}

/**
 * Stop audio playback
 */
void android_audio_stop() {
    g_audioRunning = false;
    
    if (g_paStream) {
        int error;
        pa_simple_drain(g_paStream, &error);
    }
}

/**
 * Flush audio buffers
 */
void android_audio_flush() {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    
    // Clear queued audio
    while (!g_audioQueue.empty()) {
        g_audioQueue.pop();
    }
}

/**
 * Get audio buffer size (in bytes)
 */
int android_audio_get_buffer_size() {
    return g_audioTrack.bufferSize;
}

/**
 * Get current sample rate
 */
int android_audio_get_sample_rate() {
    return g_audioTrack.sampleRate;
}

/**
 * Get number of channels
 */
int android_audio_get_channels() {
    return g_audioTrack.channels;
}

/**
 * Set master volume (0.0 - 1.0)
 */
void android_audio_set_volume(float volume) {
    // PulseAudio volume would be set here
    // For now, we just clamp and ignore
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    std::cout << "[Audio] Volume set to: " << (int)(volume * 100) << "%" << std::endl;
}

/**
 * Cleanup audio system - call on game exit
 */
void android_audio_cleanup() {
    std::cout << "[Audio] Cleaning up..." << std::endl;
    
    g_audioRunning = false;
    
    // Wait for audio thread to finish
    if (g_audioThread.joinable()) {
        g_audioThread.join();
    }
    
    // Flush and drain
    android_audio_flush();
    android_audio_stop();
    
    // Cleanup PulseAudio
    cleanupPulseAudio();
    
    std::cout << "[Audio] Cleanup complete" << std::endl;
}

} // extern "C"

// ============================================================================
// Extended Audio Functions (if needed by game)
// ============================================================================

/**
 * AudioTrack::play() - start playback
 */
void android_audiotrack_play() {
    g_audioTrack.playing = true;
}

/**
 * AudioTrack::pause() - pause playback  
 */
void android_audiotrack_pause() {
    g_audioTrack.playing = false;
}

/**
 * AudioTrack::stop() - stop playback
 */
void android_audiotrack_stop() {
    g_audioTrack.playing = false;
    android_audio_stop();
}

/**
 * AudioTrack::flush() - flush buffers
 */
void android_audiotrack_flush() {
    android_audio_flush();
}

/**
 * AudioTrack::release() - release resources
 */
void android_audiotrack_release() {
    android_audio_cleanup();
}

// Static initialization
static __attribute__((constructor)) void audio_bridge_init() {
    std::cout << "[Audio] Audio bridge initialized (PulseAudio, 4096-sample buffer)" << std::endl;
}