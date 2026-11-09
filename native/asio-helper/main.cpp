
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <atomic>
#include <vector>
#include <string>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include "RtAudio.h"

static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 32) out += '?';
            else out += static_cast<char>(c);
        }
    }
    return out;
}

struct FloatRing {
    static constexpr size_t CAP = 1u << 18;
    std::vector<float> data;
    std::atomic<size_t> readAt{0};
    std::atomic<size_t> writeAt{0};

    FloatRing() : data(CAP, 0.0f) {}

    void push(float value) {
        size_t w = writeAt.load(std::memory_order_relaxed);
        size_t r = readAt.load(std::memory_order_acquire);
        if (w - r >= CAP) return;
        data[w & (CAP - 1)] = value;
        writeAt.store(w + 1, std::memory_order_release);
    }

    bool pop(float& value) {
        size_t r = readAt.load(std::memory_order_relaxed);
        size_t w = writeAt.load(std::memory_order_acquire);
        if (r == w) return false;
        value = data[r & (CAP - 1)];
        readAt.store(r + 1, std::memory_order_release);
        return true;
    }
};

struct AudioState {
    FloatRing ring;
    std::atomic<bool> stopping{false};
    unsigned int channels = 2;
};

static int audioCallback(
    void* output, void*, unsigned int frames, double,
    RtAudioStreamStatus, void* userData
) {
    AudioState* state = static_cast<AudioState*>(userData);
    float* dst = static_cast<float*>(output);
    const size_t count = static_cast<size_t>(frames) * state->channels;

    for (size_t i = 0; i < count; ++i) {
        float sample = 0.0f;
        state->ring.pop(sample);
        dst[i] = sample;
    }
    return state->stopping.load(std::memory_order_acquire) ? 1 : 0;
}

static void inputReader(AudioState* state) {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    unsigned char bytes[16384];
    unsigned char pending[sizeof(float)];
    size_t pendingCount = 0;
    DWORD received = 0;

    while (ReadFile(input, bytes, sizeof(bytes), &received, nullptr) && received) {
        for (DWORD i = 0; i < received; ++i) {
            pending[pendingCount++] = bytes[i];
            if (pendingCount == sizeof(float)) {
                float value;
                std::memcpy(&value, pending, sizeof(float));
                if (value == value) state->ring.push(value);
                pendingCount = 0;
            }
        }
    }
    state->stopping.store(true, std::memory_order_release);
}

static int listDevices() {
    try {
        RtAudio audio(RtAudio::Api::WINDOWS_ASIO);
        std::vector<unsigned int> ids = audio.getDeviceIds();

        std::printf("{\"devices\":[");
        bool first = true;
        for (unsigned int id : ids) {
            RtAudio::DeviceInfo info = audio.getDeviceInfo(id);
            if (!info.outputChannels) continue;

            if (!first) std::printf(",");
            first = false;

            std::printf(
                "{\"id\":%u,\"name\":\"%s\",\"stableId\":\"%s\",\"outputChannels\":%u,"
                "\"defaultSampleRate\":%u,\"sampleRates\":[",
                id, jsonEscape(info.name).c_str(), jsonEscape(info.name).c_str(),
                info.outputChannels, info.preferredSampleRate
            );

            for (size_t i = 0; i < info.sampleRates.size(); ++i) {
                if (i) std::printf(",");
                std::printf("%u", info.sampleRates[i]);
            }
            std::printf("]}");
        }
        std::printf("]}\n");
        std::fflush(stdout);
        return 0;
    } catch (const std::exception& e) {
        std::printf("{\"devices\":[],\"error\":\"%s\"}\n",
                    jsonEscape(e.what()).c_str());
        std::fflush(stdout);
        return 2;
    }
}

static int playDevice(unsigned int deviceId,
                      unsigned int sampleRate,
                      unsigned int channels) {
    RtAudio audio(RtAudio::Api::WINDOWS_ASIO);
    AudioState state;
    state.channels = channels;

    try {
        std::vector<unsigned int> ids = audio.getDeviceIds();
        if (std::find(ids.begin(), ids.end(), deviceId) == ids.end())
            throw std::runtime_error("ASIO device ID পাওয়া যায়নি");

        RtAudio::DeviceInfo info = audio.getDeviceInfo(deviceId);
        if (!info.outputChannels)
            throw std::runtime_error("ASIO output channel নেই");
        if (!channels || channels > info.outputChannels)
            throw std::runtime_error("ASIO channel সংখ্যা সমর্থিত নয়");

        RtAudio::StreamParameters output;
        output.deviceId = deviceId;
        output.nChannels = channels;
        output.firstChannel = 0;

        unsigned int bufferFrames = 0;
        audio.openStream(
            &output, nullptr, RTAUDIO_FLOAT32, sampleRate,
            &bufferFrames, &audioCallback, &state
        );
        audio.startStream();

        std::thread reader(inputReader, &state);

        std::printf(
            "{\"ready\":true,\"running\":true,\"actualSampleRate\":%u,"
            "\"bufferFrames\":%u,\"channels\":%u}\n",
            audio.getStreamSampleRate(), bufferFrames, channels
        );
        std::fflush(stdout);

        while (audio.isStreamRunning()) Sleep(10);

        if (audio.isStreamOpen()) audio.closeStream();
        if (reader.joinable()) reader.join();
        return 0;
    } catch (const std::exception& e) {
        state.stopping.store(true);
        if (audio.isStreamOpen()) {
            try { audio.closeStream(); } catch (...) {}
        }
        std::printf("{\"ready\":false,\"error\":\"%s\"}\n",
                    jsonEscape(e.what()).c_str());
        std::fflush(stdout);
        return 2;
    }
}

int main(int argc, char** argv) {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    if (argc == 2 && std::strcmp(argv[1], "--list") == 0)
        return listDevices();

    if (argc == 5 && std::strcmp(argv[1], "--play") == 0) {
        unsigned int id = static_cast<unsigned int>(std::strtoul(argv[2], nullptr, 10));
        unsigned int rate = static_cast<unsigned int>(std::strtoul(argv[3], nullptr, 10));
        unsigned int channels = static_cast<unsigned int>(std::strtoul(argv[4], nullptr, 10));
        if (!rate || !channels || channels > 2) return 2;
        return playDevice(id, rate, channels);
    }

    std::fprintf(stderr, "Usage: asio-helper --list | --play ID RATE CHANNELS\n");
    return 2;
}
