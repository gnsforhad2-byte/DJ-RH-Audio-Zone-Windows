#include "processor.h"
#include "controller.h"
#include "pluginterfaces/base/fstrdefs.h"
#include <algorithm>
#include <cmath>

namespace Steinberg {
namespace Vst {

static const FUID kControllerUID(0x8A2B31D4, 0x62F74C19, 0xA6D3E850, 0x1B9472C6);

DJRHProcessor::DJRHProcessor() {
    setControllerClass(kControllerUID);
}

tresult PLUGIN_API DJRHProcessor::initialize(FUnknown* context) {
    tresult result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    addAudioOutput(STR16("Stereo Output"), SpeakerArr::kStereo);
    addEventInput(STR16("MIDI Input"), 16);
    return kResultOk;
}

tresult PLUGIN_API DJRHProcessor::setActive(TBool state) {
    if (!state) {
        for (auto& voice : voices)
            voice = Voice{};
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API DJRHProcessor::setupProcessing(ProcessSetup& setup) {
    sampleRate = setup.sampleRate > 0.0 ? setup.sampleRate : 44100.0;
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API DJRHProcessor::setBusArrangements(
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts) {
    (void)inputs;
    if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

tresult PLUGIN_API DJRHProcessor::canProcessSampleSize(int32 size) {
    return (size == kSample32 || size == kSample64)
        ? kResultTrue : kResultFalse;
}

void DJRHProcessor::handleEvent(const Event& event) {
    if (event.type == Event::kNoteOnEvent && event.noteOn.velocity > 0.0f) {
        Voice& voice = voices[nextVoice++ % voices.size()];
        voice = Voice{};
        voice.active = true;
        voice.note = event.noteOn.pitch;
        voice.channel = event.noteOn.channel;
        voice.velocity = std::clamp(event.noteOn.velocity, 0.0f, 1.0f);
        voice.envelope = 1.0f;
    } else if (event.type == Event::kNoteOffEvent ||
               (event.type == Event::kNoteOnEvent && event.noteOn.velocity <= 0.0f)) {
        const int16 note = event.type == Event::kNoteOffEvent
            ? event.noteOff.pitch : event.noteOn.pitch;
        const int16 channel = event.type == Event::kNoteOffEvent
            ? event.noteOff.channel : event.noteOn.channel;

        for (auto& voice : voices) {
            if (voice.active && voice.note == note && voice.channel == channel)
                voice.releasing = true;
        }
    }
}

template <typename Sample>
void DJRHProcessor::render(Sample** output, int32 channels, int32 samples,
                           IEventList* events) {
    if (!output || channels <= 0 || samples <= 0)
        return;

    for (int32 ch = 0; ch < channels; ++ch)
        std::fill(output[ch], output[ch] + samples, Sample(0));

    const int32 eventCount = events ? events->getEventCount() : 0;
    int32 eventIndex = 0;
    Event nextEvent{};

    bool hasEvent = eventIndex < eventCount &&
                    events->getEvent(eventIndex, nextEvent) == kResultOk;

    const double twoPi = 6.28318530717958647692;
    const double releaseStep = std::exp(-1.0 / (sampleRate * 0.18));

    for (int32 frame = 0; frame < samples; ++frame) {
        while (hasEvent && nextEvent.sampleOffset <= frame) {
            handleEvent(nextEvent);
            ++eventIndex;
            hasEvent = eventIndex < eventCount &&
                       events->getEvent(eventIndex, nextEvent) == kResultOk;
        }

        double mixed = 0.0;
        for (auto& voice : voices) {
            if (!voice.active)
                continue;

            const double frequency =
                440.0 * std::pow(2.0, (static_cast<double>(voice.note) - 69.0) / 12.0);

            mixed += std::sin(voice.phase) *
                     static_cast<double>(voice.velocity * voice.envelope) * 0.08;

            voice.phase += twoPi * frequency / sampleRate;
            if (voice.phase >= twoPi)
                voice.phase = std::fmod(voice.phase, twoPi);

            if (voice.releasing) {
                voice.envelope = static_cast<float>(voice.envelope * releaseStep);
                if (voice.envelope < 0.0001f)
                    voice = Voice{};
            }
        }

        const Sample value = static_cast<Sample>(std::clamp(mixed, -1.0, 1.0));
        for (int32 ch = 0; ch < channels; ++ch)
            output[ch][frame] = value;
    }
}

tresult PLUGIN_API DJRHProcessor::process(ProcessData& data) {
    if (data.numOutputs < 1 || !data.outputs)
        return kResultOk;

    AudioBusBuffers& bus = data.outputs[0];
    if (data.numSamples <= 0 || bus.numChannels <= 0)
        return kResultOk;

    if (data.symbolicSampleSize == kSample32) {
        render(bus.channelBuffers32, bus.numChannels, data.numSamples, data.inputEvents);
    } else if (data.symbolicSampleSize == kSample64) {
        render(bus.channelBuffers64, bus.numChannels, data.numSamples, data.inputEvents);
    } else {
        return kResultFalse;
    }

    bus.silenceFlags = 0;
    return kResultOk;
}

} // namespace Vst
} // namespace Steinberg
