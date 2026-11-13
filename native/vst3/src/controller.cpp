#include "controller.h"
#include "tone_names.h"
#include "pluginterfaces/base/fstrdefs.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"

#include <algorithm>

namespace Steinberg {
namespace Vst {

static constexpr ParamID kToneParamID = 1000;

tresult PLUGIN_API DJRHController::initialize(FUnknown* context) {
    tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    auto* tone = new StringListParameter(
        STR16("Tone"), kToneParamID);

    for (const char* name : DJRH::ToneNames) {
        String128 label {};
        size_t i = 0;
        while (name[i] && i < 127) {
            label[i] = static_cast<TChar>(
                static_cast<unsigned char>(name[i]));
            ++i;
        }
        label[i] = 0;
        tone->appendString(label);
    }

    parameters.addParameter(tone);
    return kResultOk;
}


tresult PLUGIN_API DJRHController::setComponentState(IBStream* state) {
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    int32 savedTone = 0;
    if (!streamer.readInt32(savedTone))
        return kResultFalse;

    savedTone = std::clamp<int32>(savedTone, 0, 133);
    return setParamNormalized(
        kToneParamID, static_cast<ParamValue>(savedTone) / 133.0);
}

IPlugView* PLUGIN_API DJRHController::createView(const char* name) {
    if (name && std::strcmp(name, ViewType::kEditor) == 0)
        return new VSTGUI::VST3Editor(this, "view", "editor.uidesc");
    return nullptr;
}

tresult PLUGIN_API DJRHController::queryInterface(const TUID iid, void** obj) {
    return EditControllerEx1::queryInterface(iid, obj);
}

}
}
