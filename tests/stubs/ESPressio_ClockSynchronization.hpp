#pragma once

namespace ESPressio::Timing {

enum class ClockSynchronizationAdjustmentMode {
    SlewOnly,
    StepIfUnsynchronized,
    StepAlways
};

}
