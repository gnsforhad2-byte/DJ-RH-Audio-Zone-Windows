#include "controller.h"
#include "tone_names.h"
#include "pluginterfaces/base/fstrdefs.h"
#include "public.sdk/source/vst/vstparameters.h"

namespace Steinberg {
namespace Vst {

static constexpr ParamID kToneParamID = 1000;

tresult PLUGIN_API DJRHController::initialize(FUnknown* context) {
    tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    auto* tone = new StringListParameter(
        USTRING("Tone"), kToneParamID);

    for (const char* name : DJRH::ToneNames) {
        String128 label {};
        Steinberg::UString(label, 128).fromAscii(name);
        tone->appendString(label);
    }

    parameters.addParameter(tone);
    return kResultOk;
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
