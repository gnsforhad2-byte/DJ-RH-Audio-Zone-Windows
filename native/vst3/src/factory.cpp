#include "processor.h"
#include "controller.h"
#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

static const FUID kProcessorUID(0xC71A4D92, 0x3F6B48E1, 0x9D20A5F7, 0x6B8134CE);
static const FUID kControllerUID(0x8A2B31D4, 0x62F74C19, 0xA6D3E850, 0x1B9472C6);

#define DJRH_PLUGIN_NAME "DJ RH Audio Zone"

BEGIN_FACTORY_DEF("DJ RH Audio Zone",
                  "https://example.invalid",
                  "mailto:info@example.invalid")

DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUID),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           DJRH_PLUGIN_NAME,
           Vst::kDistributable,
           "Instrument|Synth",
           "1.0.0",
           kVstVersionString,
           DJRHProcessor::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUID),
           PClassInfo::kManyInstances,
           kVstComponentControllerClass,
           DJRH_PLUGIN_NAME " Controller",
           0,
           "",
           "1.0.0",
           kVstVersionString,
           DJRHController::createInstance)

END_FACTORY
