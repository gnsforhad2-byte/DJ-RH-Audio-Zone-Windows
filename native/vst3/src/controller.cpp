#include "controller.h"
#include "tone_catalog.h"

#include "pluginterfaces/base/fstrdefs.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "base/source/fstreamer.h"
#include "vstgui/lib/controls/cbuttons.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Steinberg {
namespace Vst {

namespace {
constexpr ParamID kToneParamID = 1000;
constexpr ParamID kVolumeParamID = 1010;
constexpr ParamID kReleaseParamID = 1011;
constexpr ParamID kSemitoneParamID = 1012;


constexpr int32 kButtonToneDown = 2001;
constexpr int32 kButtonToneUp = 2002;
constexpr int32 kButtonReleaseDown = 2003;
constexpr int32 kButtonReleaseUp = 2004;
constexpr int32 kButtonVolumeDown = 2005;
constexpr int32 kButtonVolumeUp = 2006;
constexpr int32 kButtonSemitoneDown = 2007;
constexpr int32 kButtonSemitoneUp = 2008;
}

tresult PLUGIN_API DJRHController::initialize(FUnknown* context) {
    tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    auto* tone = new StringListParameter(
        STR16("Tone"), kToneParamID);

    const auto toneCatalog = DJRH::buildToneCatalog({});
    toneCount = static_cast<int32>(toneCatalog.size());

    int32 toneNumber = 0;
    for (const auto& entry : toneCatalog) {
        char displayName[256] {};
        std::snprintf(
            displayName, sizeof(displayName),
            "%03d  %s", int(++toneNumber),
            entry.displayName.c_str());

        String128 label {};
        StringConvert::convert(displayName, label, 128);
        tone->appendString(label);
    }

    parameters.addParameter(tone);

    parameters.addParameter(new RangeParameter(
        STR16("Volume"), kVolumeParamID, STR16("%"),
        0.0, 100.0, 85.0, 100));

    parameters.addParameter(new RangeParameter(
        STR16("Release"), kReleaseParamID, STR16("%"),
        0.0, 100.0, 16.0, 100));

    parameters.addParameter(new RangeParameter(
        STR16("Semitone"), kSemitoneParamID, STR16("st"),
        -42.0, 42.0, 0.0, 84));

    return kResultOk;
}

void DJRHController::setPlainParameter(
    ParamID id, ParamValue plainValue, bool notifyHost) {
    Parameter* parameter = getParameterObject(id);
    if (!parameter)
        return;

    const ParamValue normalized =
        parameter->toNormalized(plainValue);

    if (notifyHost)
        beginEdit(id);

    setParamNormalized(id, normalized);

    if (notifyHost) {
        performEdit(id, normalized);
        endEdit(id);
    }
}

void DJRHController::setToneIndex(int index, bool notifyHost) {
    const int32 count = std::max<int32>(1, toneCount);
    index = (index % count + count) % count;

    const ParamValue normalized =
        count > 1 ? ParamValue(index) / ParamValue(count - 1) : 0.0;

    setParamNormalized(kToneParamID, normalized);

    if (notifyHost) {
        beginEdit(kToneParamID);
        performEdit(kToneParamID, normalized);
        endEdit(kToneParamID);
    }
}

tresult PLUGIN_API DJRHController::setComponentState(IBStream* state) {
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    int32 savedTone = 0;
    if (!streamer.readInt32(savedTone))
        return kResultFalse;

    // Old preset compatibility: optional values default safely.
    int32 savedVolume = 85;
    int32 savedRelease = 16;
    int32 savedSemitone = 0;

    streamer.readInt32(savedVolume);
    streamer.readInt32(savedRelease);
    streamer.readInt32(savedSemitone);

    setToneIndex(std::clamp<int32>(savedTone, 0, std::max<int32>(0, toneCount - 1)), false);
    setPlainParameter(
        kVolumeParamID,
        std::clamp<int32>(savedVolume, 0, 100), false);
    setPlainParameter(
        kReleaseParamID,
        std::clamp<int32>(savedRelease, 0, 100), false);
    setPlainParameter(
        kSemitoneParamID,
        std::clamp<int32>(savedSemitone, -42, 42), false);

    return kResultOk;
}

VSTGUI::CView* DJRHController::verifyView(
    VSTGUI::CView* view,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* description,
    VSTGUI::VST3Editor* editor) {

    VSTGUI::CView* verified =
        VSTGUI::VST3EditorDelegate::verifyView(
            view, attributes, description, editor);

    if (!verified)
        return nullptr;

    if (auto* button =
            dynamic_cast<VSTGUI::CTextButton*>(verified)) {
        const int32 tag = button->getTag();
        if (tag >= kButtonToneDown && tag <= kButtonSemitoneUp)
            button->setListener(this);
    }

    return verified;
}

void DJRHController::valueChanged(VSTGUI::CControl* control) {
    if (!control || control->getValue() <= 0.5f)
        return;

    const int32 tag = control->getTag();

    if (tag == kButtonToneDown || tag == kButtonToneUp) {
        const int32 count = std::max<int32>(1, toneCount);
        const int current = int(std::lround(
            getParamNormalized(kToneParamID) * (count - 1)));
        setToneIndex(
            current + (tag == kButtonToneDown ? -1 : 1), true);
        return;
    }

    ParamID parameterID = 0;
    ParamValue delta = 0.0;

    switch (tag) {
        case kButtonReleaseDown:
            parameterID = kReleaseParamID;
            delta = -1.0;
            break;
        case kButtonReleaseUp:
            parameterID = kReleaseParamID;
            delta = 1.0;
            break;
        case kButtonVolumeDown:
            parameterID = kVolumeParamID;
            delta = -10.0;
            break;
        case kButtonVolumeUp:
            parameterID = kVolumeParamID;
            delta = 10.0;
            break;
        case kButtonSemitoneDown:
            parameterID = kSemitoneParamID;
            delta = -1.0;
            break;
        case kButtonSemitoneUp:
            parameterID = kSemitoneParamID;
            delta = 1.0;
            break;
        default:
            return;
    }

    Parameter* parameter = getParameterObject(parameterID);
    if (!parameter)
        return;

    const ParamValue current =
        parameter->toPlain(getParamNormalized(parameterID));
    const ParamValue next = std::clamp(
        current + delta,
        parameterID == kSemitoneParamID ? -42.0 : 0.0,
        parameterID == kSemitoneParamID ? 42.0 : 100.0);

    setPlainParameter(parameterID, next, true);
}

IPlugView* PLUGIN_API DJRHController::createView(const char* name) {
    if (name && std::strcmp(name, ViewType::kEditor) == 0)
        return new VSTGUI::VST3Editor(
            this, "view", "editor.uidesc");

    return nullptr;
}

tresult PLUGIN_API DJRHController::queryInterface(
    const TUID iid, void** obj) {
    return EditControllerEx1::queryInterface(iid, obj);
}

} // namespace Vst
} // namespace Steinberg
