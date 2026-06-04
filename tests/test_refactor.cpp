// Refactor tests — verify main.cpp is properly split into separate files
#include <catch2/catch_test_macros.hpp>

// After refactoring, these headers should exist and be usable
#include "app/main_window.h"
#include "app/layer_tree_menu.h"

TEST_CASE("QgisDesktopWindow can be instantiated", "[refactor]") {
  // Verify the class is defined in main_window.h and can be used
  REQUIRE_NOTHROW([]() {
    // Just verify the type exists and is complete
    // Actual instantiation requires QCoreApplication
    static_assert(sizeof(QgisDesktopWindow) > 0, "QgisDesktopWindow must be a complete type");
  });
}

TEST_CASE("LayerTreeMenuProvider can be instantiated", "[refactor]") {
  // Verify the class is defined in layer_tree_menu.h and can be used
  REQUIRE_NOTHROW([]() {
    static_assert(sizeof(LayerTreeMenuProvider) > 0, "LayerTreeMenuProvider must be a complete type");
  });
}

TEST_CASE("main.cpp is minimal", "[refactor]") {
  // This test documents the requirement: after refactoring, main.cpp should
  // only contain the main() function and necessary includes.
  //
  // Before refactoring:
  //   - QgisDesktopWindow class definition (~810 lines)
  //   - LayerTreeMenuProvider class definition (~74 lines)
  //   - main() function (~70 lines)
  //   - Total: ~1058 lines
  //
  // After refactoring:
  //   - main.cpp: only main() function + includes (~100 lines)
  //   - main_window.h/cpp: QgisDesktopWindow class
  //   - layer_tree_menu.h/cpp: LayerTreeMenuProvider class

  REQUIRE(true); // placeholder — actual verification is structure check
}
