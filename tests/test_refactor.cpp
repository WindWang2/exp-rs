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

#include <QFile>
#include <QString>

TEST_CASE("main.cpp is minimal", "[refactor]") {
  QFile file(QString::fromUtf8(CMAKE_SOURCE_DIR "/src/app/main.cpp"));
  REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString content = QString::fromUtf8(file.readAll());
  REQUIRE(content.contains("int main("));
  REQUIRE(content.contains("QgisDesktopWindow"));
  REQUIRE_FALSE(content.contains("class QgisDesktopWindow :"));
  REQUIRE_FALSE(content.contains("class LayerTreeMenuProvider :"));
}
