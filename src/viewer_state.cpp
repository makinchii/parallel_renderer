#include "viewer_state.h"

namespace pr {

ViewerState::ViewerState() : framebuffer() {}

ViewerState::ViewerState(int w, int h) : framebuffer(w, h) {}

Framebuffer& ViewerState::framebuffer_ref() { return framebuffer; }

const Framebuffer& ViewerState::framebuffer_ref() const { return framebuffer; }

}  // namespace pr
