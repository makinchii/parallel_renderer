#include "gui_app.h"

namespace pr {

bool gui_support_available() {
  return false;
}

int run_gui_app(int, char**) {
  return 1;
}

}  // namespace pr
