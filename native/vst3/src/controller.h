#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/base/ibstream.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/controls/icontrollistener.h"

namespace Steinberg {
namespace Vst {

class DJRHController final
    : public EditControllerEx1,
      public VSTGUI::VST3EditorDelegate,
      public VSTGUI::IControlListener {
public:
    static FUnknown* createInstance(void*) {
        return static_cast<IEditController*>(new DJRHController());
    }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API setComponentState(IBStream* state) SMTG_OVERRIDE;
    IPlugView* PLUGIN_API createView(const char* name) SMTG_OVERRIDE;
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) SMTG_OVERRIDE;

    VSTGUI::CView* verifyView(
        VSTGUI::CView* view,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;

    void valueChanged(VSTGUI::CControl* control) override;

    DELEGATE_REFCOUNT(EditController)

private:
    void setPlainParameter(ParamID id, ParamValue plainValue,
                           bool notifyHost);
    void setToneIndex(int index, bool notifyHost);
    int32 toneCount {134};
};

} // namespace Vst
} // namespace Steinberg
