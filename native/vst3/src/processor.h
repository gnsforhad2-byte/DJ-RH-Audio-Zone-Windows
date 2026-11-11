#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <array>

namespace Steinberg {
namespace Vst {

class DJRHProcessor final : public AudioEffect {
public:
    DJRHProcessor();

    static FUnknown* createInstance(void*) {
        return static_cast<IAudioProcessor*>(new DJRHProcessor());
    }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE;
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) SMTG_OVERRIDE;
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                          SpeakerArrangement* outputs, int32 numOuts) SMTG_OVERRIDE;
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE;
    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE;

private:
    struct Voice {
        bool active {false};
        bool releasing {false};
        int16 note {0};
        int16 channel {0};
        double phase {0.0};
        float velocity {0.0f};
        float envelope {0.0f};
    };

    void handleEvent(const Event& event);

    std::array<Voice, 128> voices {};
    uint32 nextVoice {0};
    double sampleRate {44100.0};

    template <typename Sample>
    void render(Sample** output, int32 channels, int32 samples, IEventList* events);
};

} // namespace Vst
} // namespace Steinberg
