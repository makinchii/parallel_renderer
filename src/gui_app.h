#pragma once

// Thin GUI app entrypoint. Depends on the shared render/job layers, but not on render core internals.
namespace pr {

bool gui_support_available();
int run_gui_app(int argc, char** argv);

}  // namespace pr
