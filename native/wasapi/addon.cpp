#include <node_api.h>
#include "RtAudio.h"

#include <memory>
#include <vector>
#include <string>
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <atomic>
#include <algorithm>
static std::unique_ptr<RtAudio> g_audio;
static unsigned int g_channels = 2;
static unsigned int g_sampleRate = 48000;

// Single-producer / single-consumer PCM ring buffer.
static constexpr size_t RING_CAPACITY = 1u << 20;
static constexpr size_t RING_MASK = RING_CAPACITY - 1;
static std::vector<float> g_ringBuffer;
static std::atomic<uint64_t> g_writePosition{0};
static std::atomic<uint64_t> g_readPosition{0};
static std::atomic<uint64_t> g_underrunFrames{0};

static napi_value ThrowError(napi_env env, const char* message) {
    napi_throw_error(env, nullptr, message);
    return nullptr;
}

static void SetNumber(napi_env env, napi_value object,
                      const char* name, double value) {
    napi_value v;
    napi_create_double(env, value, &v);
    napi_set_named_property(env, object, name, v);
}

static void SetBoolean(napi_env env, napi_value object,
                       const char* name, bool value) {
    napi_value v;
    napi_get_boolean(env, value, &v);
    napi_set_named_property(env, object, name, v);
}

static void SetString(napi_env env, napi_value object,
                      const char* name, const std::string& value) {
    napi_value v;
    napi_create_string_utf8(env, value.c_str(), value.size(), &v);
    napi_set_named_property(env, object, name, v);
}

static bool ReadUint(napi_env env, napi_value object,
                     const char* name, unsigned int& value) {
    bool exists = false;
    if (napi_has_named_property(env, object, name, &exists) != napi_ok ||
        !exists) return false;

    napi_value v;
    if (napi_get_named_property(env, object, name, &v) != napi_ok)
        return false;

    napi_valuetype type;
    napi_typeof(env, v, &type);
    if (type != napi_number) return false;

    uint32_t result = 0;
    if (napi_get_value_uint32(env, v, &result) != napi_ok)
        return false;

    value = result;
    return true;
}

/* Real-time callback: no allocation, locks, or JavaScript calls. */
static int AudioCallback(
    void* outputBuffer, void*, unsigned int frames, double,
    RtAudioStreamStatus, void*
) {
    if (!outputBuffer) return 0;

    const size_t requested = static_cast<size_t>(frames) * g_channels;
    float* output = static_cast<float*>(outputBuffer);
    std::memset(output, 0, requested * sizeof(float));

    if (g_ringBuffer.empty()) {
        g_underrunFrames.fetch_add(frames, std::memory_order_relaxed);
        return 0;
    }

    const uint64_t read = g_readPosition.load(std::memory_order_relaxed);
    const uint64_t write = g_writePosition.load(std::memory_order_acquire);
    const uint64_t available = write - read;
    const size_t count = static_cast<size_t>(
        std::min<uint64_t>(available, requested)
    );

    const size_t offset = static_cast<size_t>(read & RING_MASK);
    const size_t first = std::min(count, RING_CAPACITY - offset);

    if (first)
        std::copy_n(g_ringBuffer.data() + offset, first, output);

    if (count > first)
        std::copy_n(g_ringBuffer.data(), count - first, output + first);

    g_readPosition.store(read + count, std::memory_order_release);

    if (count < requested) {
        g_underrunFrames.fetch_add(
            (requested - count) / g_channels,
            std::memory_order_relaxed
        );
    }
    return 0;
}

/* Accept interleaved Float32 PCM. Returns complete frames accepted. */
static napi_value WriteAudio(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

    if (argc < 1)
        return ThrowError(env, "A Float32Array audio buffer is required.");

    if (!g_audio || !g_audio->isStreamRunning())
        return ThrowError(env, "WASAPI output is not running.");

    napi_typedarray_type type;
    size_t length = 0;
    void* data = nullptr;
    napi_value arrayBuffer;
    size_t byteOffset = 0;

    if (napi_get_typedarray_info(
            env, argv[0], &type, &length, &data,
            &arrayBuffer, &byteOffset) != napi_ok ||
        type != napi_float32_array || !data) {
        return ThrowError(env, "Audio data must be a Float32Array.");
    }

    if (!g_channels || length % g_channels != 0)
        return ThrowError(env, "Audio buffer has an invalid channel layout.");

    if (length > RING_CAPACITY || g_ringBuffer.empty())
        return ThrowError(env, "Audio block exceeds native buffer capacity.");

    const uint64_t write =
        g_writePosition.load(std::memory_order_relaxed);
    const uint64_t read =
        g_readPosition.load(std::memory_order_acquire);
    const uint64_t used = write - read;

    if (used > RING_CAPACITY)
        return ThrowError(env, "Native audio buffer state is invalid.");

    size_t accepted = std::min(
        length, RING_CAPACITY - static_cast<size_t>(used)
    );
    accepted -= accepted % g_channels;

    const size_t offset = static_cast<size_t>(write & RING_MASK);
    const size_t first = std::min(accepted, RING_CAPACITY - offset);
    const float* input = static_cast<const float*>(data);

    if (first)
        std::copy_n(input, first, g_ringBuffer.data() + offset);

    if (accepted > first)
        std::copy_n(input + first, accepted - first, g_ringBuffer.data());

    g_writePosition.store(write + accepted, std::memory_order_release);

    napi_value result;
    napi_create_uint32(
        env, static_cast<uint32_t>(accepted / g_channels), &result
    );
    return result;
}

static napi_value GetDevices(napi_env env, napi_callback_info) {
    try {
        RtAudio audio(RtAudio::WINDOWS_WASAPI);
        std::vector<unsigned int> ids = audio.getDeviceIds();

        napi_value array;
        napi_create_array(env, &array);

        uint32_t index = 0;

        for (unsigned int id : ids) {
            RtAudio::DeviceInfo info = audio.getDeviceInfo(id);

            if (!info.outputChannels) continue;

            napi_value item;
            napi_create_object(env, &item);

            SetNumber(env, item, "id", id);
            SetString(env, item, "name", info.name);
            SetNumber(env, item, "outputChannels", info.outputChannels);
            SetBoolean(env, item, "isDefaultOutput", info.isDefaultOutput);

            napi_value rates;
            napi_create_array(env, &rates);

            for (uint32_t i = 0; i < info.sampleRates.size(); ++i) {
                napi_value rate;
                napi_create_uint32(env, info.sampleRates[i], &rate);
                napi_set_element(env, rates, i, rate);
            }

            napi_set_named_property(env, item, "sampleRates", rates);
            napi_set_element(env, array, index++, item);
        }

        return array;
    } catch (const std::exception& e) {
        return ThrowError(env, e.what());
    } catch (...) {
        return ThrowError(env, "WASAPI device enumeration failed.");
    }
}

static napi_value OpenDevice(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];

    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

    if (argc < 1)
        return ThrowError(env, "Device configuration is required.");

    unsigned int deviceId = 0;
    unsigned int sampleRate = 48000;
    unsigned int channels = 2;

    if (!ReadUint(env, argv[0], "deviceId", deviceId))
        return ThrowError(env, "A valid deviceId is required.");

    ReadUint(env, argv[0], "sampleRate", sampleRate);
    ReadUint(env, argv[0], "channels", channels);

    if (sampleRate < 8000 || sampleRate > 192000)
        return ThrowError(env, "Unsupported sample rate value.");

    if (channels < 1 || channels > 8)
        return ThrowError(env, "Invalid output channel count.");

    try {
        if (g_audio) {
            if (g_audio->isStreamRunning())
                g_audio->stopStream();
            if (g_audio->isStreamOpen())
                g_audio->closeStream();
            g_audio.reset();
        }

        g_audio = std::make_unique<RtAudio>(
            RtAudio::WINDOWS_WASAPI
        );

        RtAudio::DeviceInfo deviceInfo =
            g_audio->getDeviceInfo(deviceId);

        if (!deviceInfo.outputChannels)
            return ThrowError(env, "Selected device has no output channels.");

        if (channels > deviceInfo.outputChannels)
            return ThrowError(env, "Requested channels exceed device capacity.");

        g_channels = channels;
        g_sampleRate = sampleRate;


        // Reset only while the previous stream is stopped.
        g_ringBuffer.assign(RING_CAPACITY, 0.0f);
        g_readPosition.store(0, std::memory_order_relaxed);
        g_writePosition.store(0, std::memory_order_relaxed);
        g_underrunFrames.store(0, std::memory_order_relaxed);
RtAudio::StreamParameters output;
        output.deviceId = deviceId;
        output.nChannels = channels;
        output.firstChannel = 0;

        RtAudio::StreamOptions options;
        options.flags = RTAUDIO_MINIMIZE_LATENCY;

        unsigned int bufferFrames = 256;

        RtAudioErrorType result = g_audio->openStream(
            &output,
            nullptr,
            RTAUDIO_FLOAT32,
            sampleRate,
            &bufferFrames,
            AudioCallback,
            nullptr,
            &options
        );

        if (result != RTAUDIO_NO_ERROR || !g_audio->isStreamOpen()) {
            g_audio.reset();
            return ThrowError(env, "WASAPI stream could not be opened.");
        }

        result = g_audio->startStream();

        if (result != RTAUDIO_NO_ERROR || !g_audio->isStreamRunning()) {
            g_audio->closeStream();
            g_audio.reset();
            return ThrowError(env, "WASAPI stream could not be started.");
        }

        napi_value response;
        napi_create_object(env, &response);

        SetBoolean(env, response, "success", true);
        SetNumber(env, response, "deviceId", deviceId);
        SetNumber(env, response, "sampleRate",
                  g_audio->getStreamSampleRate());
        SetNumber(env, response, "channels", channels);
        SetNumber(env, response, "bufferFrames", bufferFrames);
        SetNumber(env, response, "ringCapacityFrames",
                  RING_CAPACITY / channels);
        SetNumber(env, response, "latency", g_audio->getStreamLatency());

        return response;

    } catch (const std::exception& e) {
        g_audio.reset();
        return ThrowError(env, e.what());
    } catch (...) {
        g_audio.reset();
        return ThrowError(env, "WASAPI device opening failed.");
    }
}

static napi_value CloseDevice(napi_env env, napi_callback_info) {
    try {
        if (g_audio) {
            if (g_audio->isStreamRunning())
                g_audio->stopStream();

            if (g_audio->isStreamOpen())
                g_audio->closeStream();

            g_audio.reset();
        }

        napi_value result;
        napi_get_boolean(env, true, &result);
        return result;

    } catch (...) {
        g_audio.reset();
        return ThrowError(env, "WASAPI stream closing failed.");
    }
}

static napi_value GetStatus(napi_env env, napi_callback_info) {
    napi_value result;
    napi_create_object(env, &result);

    bool opened = g_audio && g_audio->isStreamOpen();
    bool running = opened && g_audio->isStreamRunning();

    SetBoolean(env, result, "available", true);
    SetBoolean(env, result, "opened", opened);
    SetBoolean(env, result, "running", running);
    SetNumber(env, result, "sampleRate", g_sampleRate);
    SetNumber(env, result, "channels", g_channels);


    // Read first, then write: the callback cannot consume beyond
    // the write position observed after this read snapshot.
    const uint64_t readSnapshot =
        g_readPosition.load(std::memory_order_acquire);
    const uint64_t writeSnapshot =
        g_writePosition.load(std::memory_order_acquire);

    SetNumber(env, result, "queuedFrames",
              g_channels && writeSnapshot >= readSnapshot
                ? (writeSnapshot - readSnapshot) / g_channels
                : 0);
    SetNumber(env, result, "underrunFrames",
              g_underrunFrames.load(std::memory_order_relaxed));

    if (opened) {
        SetNumber(env, result, "actualSampleRate",
                  g_audio->getStreamSampleRate());
        SetNumber(env, result, "latency",
                  g_audio->getStreamLatency());
    }

    return result;
}

static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor properties[] = {
        {"getDevices", nullptr, GetDevices, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"open", nullptr, OpenDevice, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"write", nullptr, WriteAudio, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"close", nullptr, CloseDevice, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"getStatus", nullptr, GetStatus, nullptr, nullptr, nullptr,
         napi_default, nullptr}
    };

    napi_define_properties(
        env,
        exports,
        sizeof(properties) / sizeof(properties[0]),
        properties
    );

    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
