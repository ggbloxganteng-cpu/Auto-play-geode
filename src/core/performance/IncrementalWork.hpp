#pragma once

#include "AdaptiveScheduler.hpp"

#include <cstddef>
#include <utility>

namespace at::performance {

// Runs a bounded number of tiny steps. The step callback must do one small,
// deterministic unit of work and return true while more work remains.
// No thread is created and no game object is accessed off the game thread.
template <class StepFn>
std::size_t runIncrementalSlice(AdaptiveScheduler& scheduler,
                                AdaptiveScheduler::WorkClass workClass,
                                std::size_t maxSteps,
                                StepFn&& step) {
    if (!scheduler.mayRun(workClass)) return 0;

    std::size_t completed = 0;
    while (completed < maxSteps && !scheduler.shouldYield(workClass)) {
        if (!step()) break;
        ++completed;
    }
    return completed;
}

} // namespace at::performance
