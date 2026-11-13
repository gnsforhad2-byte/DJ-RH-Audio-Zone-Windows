#pragma once
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/base/ibstream.h"
#include "vstgui/plugin-bindings/vst3editor.h"

namespace Steinberg {
namespace Vst {

class DJRHController final : public EditControllerEx1,
                             public VSTGUI::VST3EditorDelegate {
public:
    static FUnknown* createInstance(void*) {
        return static_cast<IEditController*>(new DJRHController());
    }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API setComponentState(IBStream* state) SMTG_OVERRIDE;
    IPlugView* PLUGIN_API createView(const char* name) SMTG_OVERRIDE;
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) SMTG_OVERRIDE;

    DELEGATE_REFCOUNT(EditController)
};

}
}
