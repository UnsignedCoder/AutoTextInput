/**
 * @file Platform_win.cpp
 * @brief Windows implementation of Platform.h.
 *
 * Windows supports everything AutoTextInput does out of the box: `RegisterHotKey` works for any desktop app,
 * and `SendInput` can type into any window running at the same or a lower privilege level. So there is nothing
 * to prepare and nothing to warn about.
 */

#include "Platform.h"

namespace Platform {

void PrepareEnvironment() {}

QString SessionWarning() {
    return {};
}

} // namespace Platform
