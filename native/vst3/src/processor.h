#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Steinberg {
namespace Vst {

class DJRHProcessor final : public AudioEffect {
public:
    DJRHProcessor();
    ~DJRHProcessor() override;

    static FUnknown* createInstance(void*) {
        return static_cast<IAudioProcessor*>(new DJRHProcessor());
    }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE;
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) SMTG_OVERRIDE;
    tresult PLUGIN_API setBusArrangements(
        SpeakerArrangement* inputs, int32 numIns,
        SpeakerArrangement* outputs, int32 numOuts) SMTG_OVERRIDE;
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE;
    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE;
    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;

private:
    struct Voice {
        bool active {false};
        bool releasing {false};
        int16 note {0};
        int16 channel {0};
        double cursor {0.0};
        float velocity {0.0f};
        float envelope {0.0f};
    };

    struct SampleData {
        std::vector<float> samples;
        uint32 channels {0};
        uint32 sourceSampleRate {0};
        int rootMidiNote {60};
        uint64 generation {0};

        uint64 frameCount() const {
            return channels
                ? static_cast<uint64>(samples.size() / channels)
                : 0;
        }
    };

    void stopSampleLoader();
    void handleEvent(const Event& event);
    void requestTone(int index);
    void sampleLoaderLoop();

    std::shared_ptr<SampleData> loadTone(int index) const;
    std::filesystem::path findToneDirectory() const;

    std::array<Voice, 128> voices {};
    uint32 nextVoice {0};
    double sampleRate {44100.0};

    std::filesystem::path toneDirectory;

    std::atomic<int> selectedToneIndex {0};
    std::atomic<float> volume {0.85f};
    std::atomic<int> releasePercent {16};
    std::atomic<int> semitoneShift {0};
    std::atomic<int> requestedTone {-1};
    std::atomic<bool> stopLoader {false};
    std::mutex loaderWaitMutex;
    std::condition_variable loaderWake;
    std::thread loaderThread;

    std::shared_ptr<const SampleData> currentSample;
    std::atomic<uint64> sampleGeneration {0};
    uint64 renderedSampleGeneration {static_cast<uint64>(-1)};

    template <typename Sample>
    void render(
        Sample** output,
        int32 channels,
        int32 numSamples,
        IEventList* events,
        const std::shared_ptr<const SampleData>& sample);
};

} // namespace Vst
} // namespace Steinberg
