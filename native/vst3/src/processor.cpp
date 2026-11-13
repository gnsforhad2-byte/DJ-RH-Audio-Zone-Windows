#include "processor.h"
#include "controller.h"
#include "tone_files.h"
#include "tone_roots.h"
#include "pluginterfaces/base/fstrdefs.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Steinberg {
namespace Vst {
namespace {
constexpr ParamID kToneParamID = 1000;

int g_moduleAnchor = 0;

std::filesystem::path moduleDirectory() {
#ifdef _WIN32
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&g_moduleAnchor), &module))
        return {};
    std::wstring path(32768, L'\0');
    DWORD n = GetModuleFileNameW(module, path.data(), DWORD(path.size()));
    if (!n || n >= path.size()) return {};
    path.resize(n);
    return std::filesystem::path(path).parent_path();
#else
    Dl_info info{};
    if (!dladdr(&g_moduleAnchor, &info) || !info.dli_fname) return {};
    return std::filesystem::path(info.dli_fname).parent_path();
#endif
}

int toneIndex(ParamValue value) {
    return int(std::lround(std::clamp(value, 0.0, 1.0) * 133.0));
}
}

static const FUID kControllerUID(
    0x8A2B31D4, 0x62F74C19, 0xA6D3E850, 0x1B9472C6);

DJRHProcessor::DJRHProcessor() {
    setControllerClass(kControllerUID);
}

DJRHProcessor::~DJRHProcessor() {
    stopSampleLoader();
}

void DJRHProcessor::stopSampleLoader() {
    stopLoader.store(true, std::memory_order_release);
    loaderWake.notify_all();
    if (loaderThread.joinable())
        loaderThread.join();
}

tresult PLUGIN_API DJRHProcessor::terminate() {
    stopSampleLoader();
    std::atomic_store_explicit(
        &currentSample,
        std::shared_ptr<const SampleData>{},
        std::memory_order_release);
    return AudioEffect::terminate();
}

tresult PLUGIN_API DJRHProcessor::initialize(FUnknown* context) {
    auto result = AudioEffect::initialize(context);
    if (result != kResultOk) return result;

    addAudioOutput(STR16("Stereo Output"), SpeakerArr::kStereo);
    addEventInput(STR16("MIDI Input"), 16);

    toneDirectory = findToneDirectory();
    stopLoader.store(false, std::memory_order_release);
    loaderThread = std::thread(&DJRHProcessor::sampleLoaderLoop, this);
    requestTone(0);
    return kResultOk;
}

tresult PLUGIN_API DJRHProcessor::setActive(TBool state) {
    if (!state)
        for (auto& voice : voices) voice = Voice{};
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API DJRHProcessor::setupProcessing(ProcessSetup& setup) {
    sampleRate = setup.sampleRate > 0 ? setup.sampleRate : 44100.0;
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API DJRHProcessor::setBusArrangements(
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts) {
    (void)inputs;
    if (numIns == 0 && numOuts == 1 && outputs &&
        outputs[0] == SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

tresult PLUGIN_API DJRHProcessor::canProcessSampleSize(int32 size) {
    return size == kSample32 || size == kSample64
        ? kResultTrue : kResultFalse;
}

std::filesystem::path DJRHProcessor::findToneDirectory() const {
    auto dir = moduleDirectory();
    if (dir.empty()) return {};
    return (dir / ".." / "Resources" / "tones").lexically_normal();
}

void DJRHProcessor::requestTone(int index) {
    if (index < 0 || index >= 134) return;
    selectedToneIndex.store(index, std::memory_order_release);
    requestedTone.store(index, std::memory_order_release);
    loaderWake.notify_one();
}

std::shared_ptr<DJRHProcessor::SampleData>
DJRHProcessor::loadTone(int index) const {
    if (index < 0 || index >= 134 || toneDirectory.empty()) return {};

    auto path = toneDirectory / DJRH::ToneFiles[size_t(index)];
    auto sample = std::make_shared<SampleData>();
    sample->rootMidiNote = DJRH::ToneRootMidi[size_t(index)];
    auto ext = path.extension().string();

    if (ext == ".wav" || ext == ".WAV") {
        unsigned int channels = 0, rate = 0;
        drwav_uint64 frames = 0;
        float* decoded = nullptr;
#ifdef _WIN32
        decoded = drwav_open_file_and_read_pcm_frames_f32_w(
            path.c_str(), &channels, &rate, &frames, nullptr);
#else
        auto nativePath = path.string();
        decoded = drwav_open_file_and_read_pcm_frames_f32(
            nativePath.c_str(), &channels, &rate, &frames, nullptr);
#endif
        if (!decoded || !channels || !rate || !frames) {
            if (decoded) drwav_free(decoded, nullptr);
            return {};
        }
        sample->channels = channels;
        sample->sourceSampleRate = rate;
        auto count = size_t(frames) * channels;
        sample->samples.assign(decoded, decoded + count);
        drwav_free(decoded, nullptr);
    } else if (ext == ".mp3" || ext == ".MP3") {
        mp3dec_t decoder{};
        mp3dec_file_info_t info{};
#ifdef _WIN32
        int status = mp3dec_load_w(&decoder, path.c_str(), &info, nullptr, nullptr);
#else
        auto nativePath = path.string();
        int status = mp3dec_load(&decoder, nativePath.c_str(), &info, nullptr, nullptr);
#endif
        if (status || !info.buffer || info.channels <= 0 ||
            info.hz <= 0 || !info.samples) {
            std::free(info.buffer);
            return {};
        }
        sample->channels = uint32(info.channels);
        sample->sourceSampleRate = uint32(info.hz);
        sample->samples.resize(info.samples);
        for (size_t i = 0; i < info.samples; ++i)
            sample->samples[i] = float(info.buffer[i]) / 32768.0f;
        std::free(info.buffer);
    } else {
        return {};
    }

    if (sample->channels > 2) {
        auto frames = sample->frameCount();
        std::vector<float> stereo(size_t(frames) * 2);
        for (uint64 f = 0; f < frames; ++f) {
            stereo[size_t(f) * 2] =
                sample->samples[size_t(f) * sample->channels];
            stereo[size_t(f) * 2 + 1] =
                sample->samples[size_t(f) * sample->channels + 1];
        }
        sample->samples.swap(stereo);
        sample->channels = 2;
    }
    return sample;
}

void DJRHProcessor::sampleLoaderLoop() {
    while (!stopLoader.load(std::memory_order_acquire)) {
        int index = requestedTone.exchange(-1, std::memory_order_acq_rel);
        if (index < 0) {
            std::unique_lock<std::mutex> lock(loaderWaitMutex);
            loaderWake.wait(lock, [this] {
                return stopLoader.load(std::memory_order_acquire) ||
                    requestedTone.load(std::memory_order_acquire) >= 0;
            });
            continue;
        }

        auto loaded = loadTone(index);
        if (loaded && !stopLoader.load(std::memory_order_acquire)) {
            loaded->generation =
                sampleGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
            std::shared_ptr<const SampleData> published = std::move(loaded);
            std::atomic_store_explicit(
                &currentSample, std::move(published), std::memory_order_release);
        }
    }
}

void DJRHProcessor::handleEvent(const Event& event) {
    if (event.type == Event::kNoteOnEvent && event.noteOn.velocity > 0) {
        auto& voice = voices[nextVoice++ % voices.size()];
        voice = Voice{};
        voice.active = true;
        voice.note = event.noteOn.pitch;
        voice.channel = event.noteOn.channel;
        voice.velocity = std::clamp(event.noteOn.velocity, 0.0f, 1.0f);
        voice.envelope = 1.0f;
    } else if (event.type == Event::kNoteOffEvent ||
        (event.type == Event::kNoteOnEvent && event.noteOn.velocity <= 0)) {
        int16 note = event.type == Event::kNoteOffEvent
            ? event.noteOff.pitch : event.noteOn.pitch;
        int16 channel = event.type == Event::kNoteOffEvent
            ? event.noteOff.channel : event.noteOn.channel;
        for (auto& voice : voices)
            if (voice.active && voice.note == note &&
                voice.channel == channel)
                voice.releasing = true;
    }
}

template <typename Sample>
void DJRHProcessor::render(
    Sample** output, int32 channels, int32 numSamples,
    IEventList* events, const std::shared_ptr<const SampleData>& sample) {
    if (!output || channels <= 0 || numSamples <= 0) return;
    for (int32 c = 0; c < channels; ++c)
        std::fill(output[c], output[c] + numSamples, Sample(0));
    if (!sample || !sample->channels || !sample->frameCount()) return;

    if (sample->generation != renderedSampleGeneration) {
        for (auto& voice : voices) voice = Voice{};
        renderedSampleGeneration = sample->generation;
    }

    int32 eventCount = events ? events->getEventCount() : 0;
    int32 eventIndex = 0;
    Event nextEvent{};
    bool hasEvent = eventIndex < eventCount &&
        events->getEvent(eventIndex, nextEvent) == kResultOk;
    const int releaseValue = std::clamp(
        releasePercent.load(std::memory_order_relaxed), 0, 100);
    const double releaseSeconds =
        0.02 + (double(releaseValue) / 100.0) * 0.98;
    const double releaseStep =
        std::exp(-1.0 / (sampleRate * releaseSeconds));
    const float volumeLevel = std::clamp(
        volume.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const int pitchShift =
        semitoneShift.load(std::memory_order_relaxed);
    const uint64 frames = sample->frameCount();
    const uint32 srcChannels = sample->channels;

    for (int32 frame = 0; frame < numSamples; ++frame) {
        while (hasEvent && nextEvent.sampleOffset <= frame) {
            handleEvent(nextEvent);
            ++eventIndex;
            hasEvent = eventIndex < eventCount &&
                events->getEvent(eventIndex, nextEvent) == kResultOk;
        }

        double left = 0, right = 0;
        for (auto& voice : voices) {
            if (!voice.active) continue;
            if (voice.cursor >= double(frames)) {
                voice = Voice{};
                continue;
            }

            uint64 aFrame = uint64(voice.cursor);
            uint64 bFrame = std::min(aFrame + 1, frames - 1);
            float frac = float(voice.cursor - double(aFrame));
            size_t a = size_t(aFrame) * srcChannels;
            size_t b = size_t(bFrame) * srcChannels;

            float l0 = sample->samples[a];
            float r0 = srcChannels > 1 ? sample->samples[a + 1] : l0;
            float l1 = sample->samples[b];
            float r1 = srcChannels > 1 ? sample->samples[b + 1] : l1;
            float gain = voice.velocity * voice.envelope * volumeLevel;
            left += (l0 + (l1 - l0) * frac) * gain;
            right += (r0 + (r1 - r0) * frac) * gain;

            double ratio = std::pow(
                2.0,
                double(int(voice.note) + pitchShift - sample->rootMidiNote) / 12.0);
            voice.cursor += ratio * double(sample->sourceSampleRate) / sampleRate;

            if (voice.releasing) {
                voice.envelope = float(voice.envelope * releaseStep);
                if (voice.envelope < 0.0001f) voice = Voice{};
            }
        }

        if (channels == 1) {
            output[0][frame] = Sample(std::clamp((left + right) * 0.5, -1.0, 1.0));
        } else {
            output[0][frame] = Sample(std::clamp(left, -1.0, 1.0));
            output[1][frame] = Sample(std::clamp(right, -1.0, 1.0));
            for (int32 c = 2; c < channels; ++c) output[c][frame] = Sample(0);
        }
    }
}

tresult PLUGIN_API DJRHProcessor::process(ProcessData& data) {
    if (data.numOutputs < 1 || !data.outputs)
        return kResultOk;

    auto& bus = data.outputs[0];
    if (data.numSamples <= 0 || bus.numChannels <= 0)
        return kResultOk;

    constexpr ParamID kToneParamID = 1000;
    constexpr ParamID kVolumeParamID = 1010;
    constexpr ParamID kReleaseParamID = 1011;
    constexpr ParamID kSemitoneParamID = 1012;

    if (data.inputParameterChanges) {
        int32 count = data.inputParameterChanges->getParameterCount();

        for (int32 q = 0; q < count; ++q) {
            auto* queue =
                data.inputParameterChanges->getParameterData(q);
            if (!queue)
                continue;

            const ParamID id = queue->getParameterId();
            if (id != kToneParamID &&
                id != kVolumeParamID &&
                id != kReleaseParamID &&
                id != kSemitoneParamID)
                continue;

            const int32 points = queue->getPointCount();
            if (points <= 0)
                continue;

            int32 offset = 0;
            ParamValue value = 0.0;
            if (queue->getPoint(points - 1, offset, value) != kResultOk)
                continue;

            value = std::clamp(value, 0.0, 1.0);

            switch (id) {
                case kToneParamID:
                    requestTone(toneIndex(value));
                    break;

                case kVolumeParamID:
                    volume.store(
                        float(value), std::memory_order_relaxed);
                    break;

                case kReleaseParamID:
                    releasePercent.store(
                        int(std::lround(value * 100.0)),
                        std::memory_order_relaxed);
                    break;

                case kSemitoneParamID:
                    semitoneShift.store(
                        int(std::lround(-42.0 + value * 84.0)),
                        std::memory_order_relaxed);
                    break;
            }
        }
    }

    auto sample = std::atomic_load_explicit(
        &currentSample, std::memory_order_acquire);

    if (data.symbolicSampleSize == kSample32) {
        render(
            bus.channelBuffers32, bus.numChannels, data.numSamples,
            data.inputEvents, sample);
    } else if (data.symbolicSampleSize == kSample64) {
        render(
            bus.channelBuffers64, bus.numChannels, data.numSamples,
            data.inputEvents, sample);
    } else {
        return kResultFalse;
    }

    bus.silenceFlags = 0;
    return kResultOk;
}

tresult PLUGIN_API DJRHProcessor::getState(IBStream* state) {
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    const int32 tone =
        selectedToneIndex.load(std::memory_order_acquire);
    const int32 volumeValue = int32(std::lround(
        std::clamp(volume.load(std::memory_order_relaxed), 0.0f, 1.0f)
        * 100.0f));
    const int32 releaseValue =
        releasePercent.load(std::memory_order_relaxed);
    const int32 pitchValue =
        semitoneShift.load(std::memory_order_relaxed);

    if (!streamer.writeInt32(tone)) return kResultFalse;
    if (!streamer.writeInt32(volumeValue)) return kResultFalse;
    if (!streamer.writeInt32(releaseValue)) return kResultFalse;
    if (!streamer.writeInt32(pitchValue)) return kResultFalse;

    return kResultOk;
}

tresult PLUGIN_API DJRHProcessor::setState(IBStream* state) {
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    int32 savedTone = 0;
    if (!streamer.readInt32(savedTone))
        return kResultFalse;

    // Legacy presets contain only the tone index.
    // Missing fields keep the backward-compatible defaults.
    int32 savedVolume = 85;
    int32 savedRelease = 16;
    int32 savedSemitone = 0;

    streamer.readInt32(savedVolume);
    streamer.readInt32(savedRelease);
    streamer.readInt32(savedSemitone);

    requestTone(std::clamp<int32>(savedTone, 0, 133));

    volume.store(
        float(std::clamp<int32>(savedVolume, 0, 100)) / 100.0f,
        std::memory_order_relaxed);
    releasePercent.store(
        std::clamp<int32>(savedRelease, 0, 100),
        std::memory_order_relaxed);
    semitoneShift.store(
        std::clamp<int32>(savedSemitone, -42, 42),
        std::memory_order_relaxed);

    return kResultOk;
}

} // namespace Vst
} // namespace Steinberg
