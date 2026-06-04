// Colormap & Colorbar tests — Task 5B.0
// Verifies that predefined color ramps are loaded from symbology-style.xml
// and that color ramp shader works correctly.
// Custom main required: QgsApplication must be initialized with correct prefix path.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include <qgsapplication.h>
#include <qgsstyle.h>
#include <qgscolorramp.h>
#include <qgscolorrampimpl.h>
#include <qgscolorrampshader.h>
#include <qgsrastershader.h>
#include <qgssinglebandpseudocolorrenderer.h>
#include <qgsrasterlayer.h>
#include <qgsproject.h>
#include <symbology/qgscolorbrewerpalette.h>

#include "app/app_paths.h"
#include <QDir>
#include <QFileInfo>

int main(int argc, char *argv[]) {
    QgsApplication app(argc, argv, true);
    app.setApplicationName("SICNU GEO RS Test");
    QgsApplication::setPrefixPath(AppPaths::prefixPath(), true);
    QgsApplication::initQgis();

    // Import symbology-style.xml if default style has no color ramps
    QgsStyle *style = QgsStyle::defaultStyle();
    if (style->colorRampNames().isEmpty()) {
        QString xmlPath = AppPaths::resolveDataPath("qgis_ref/resources/symbology-style.xml");
        if (QFileInfo::exists(xmlPath)) {
            style->importXml(xmlPath);
        }
    }

    int result = Catch::Session().run(argc, argv);
    return result;
}

TEST_CASE("Color ramps loaded from default style", "[colormap]")
{
    QgsStyle *style = QgsStyle::defaultStyle();
    REQUIRE(style != nullptr);

    SECTION("Default style has color ramps")
    {
        QStringList rampNames = style->colorRampNames();
        INFO("Color ramp count: " << rampNames.size());
        INFO("Ramps: " << rampNames.join(", ").toStdString());
        CHECK(rampNames.size() > 0);
    }

    SECTION("Viridis color ramp exists")
    {
        QStringList rampNames = style->colorRampNames();
        CHECK(rampNames.contains("Viridis"));
    }

    SECTION("Spectral color ramp exists")
    {
        QStringList rampNames = style->colorRampNames();
        CHECK(rampNames.contains("Spectral"));
    }

    SECTION("Magma color ramp exists")
    {
        QStringList rampNames = style->colorRampNames();
        CHECK(rampNames.contains("Magma"));
    }

    SECTION("Plasma color ramp exists")
    {
        QStringList rampNames = style->colorRampNames();
        CHECK(rampNames.contains("Plasma"));
    }

    SECTION("Inferno color ramp exists")
    {
        QStringList rampNames = style->colorRampNames();
        CHECK(rampNames.contains("Inferno"));
    }
}

TEST_CASE("Color ramp can be created from style", "[colormap]")
{
    QgsStyle *style = QgsStyle::defaultStyle();

    SECTION("Can create Viridis ramp")
    {
        std::unique_ptr<QgsColorRamp> ramp(style->colorRamp("Viridis"));
        REQUIRE(ramp != nullptr);
        CHECK(ramp->type() == QgsGradientColorRamp::typeString());
    }

    SECTION("Can create Spectral ramp")
    {
        std::unique_ptr<QgsColorRamp> ramp(style->colorRamp("Spectral"));
        REQUIRE(ramp != nullptr);
    }

    SECTION("Ramp has valid color range")
    {
        std::unique_ptr<QgsColorRamp> ramp(style->colorRamp("Viridis"));
        REQUIRE(ramp != nullptr);
        QColor start = ramp->color(0.0);
        QColor end = ramp->color(1.0);
        CHECK(start.isValid());
        CHECK(end.isValid());
        CHECK(start != end);
    }
}

TEST_CASE("Color ramp shader integration", "[colormap]")
{
    SECTION("Shader can be created with manual items")
    {
        QgsColorRampShader shader(0.0, 255.0);
        shader.setClassificationMode(Qgis::ShaderClassificationMethod::Continuous);

        // Manually create items for testing
        QList<QgsColorRampShader::ColorRampItem> items;
        items.append(QgsColorRampShader::ColorRampItem(0.0, QColor(0, 0, 0)));
        items.append(QgsColorRampShader::ColorRampItem(127.5, QColor(128, 128, 128)));
        items.append(QgsColorRampShader::ColorRampItem(255.0, QColor(255, 255, 255)));
        shader.setColorRampItemList(items);

        QList<QgsColorRampShader::ColorRampItem> result = shader.colorRampItemList();
        CHECK(result.size() == 3);
    }
}

TEST_CASE("ColorBrewer palettes available", "[colormap]")
{
    SECTION("ColorBrewer palette list is not empty")
    {
        QStringList schemes = QgsColorBrewerPalette::listSchemes();
        INFO("ColorBrewer schemes: " << schemes.join(", ").toStdString());
        CHECK(schemes.size() > 0);
    }

    SECTION("Spectral is a valid ColorBrewer scheme")
    {
        QStringList schemes = QgsColorBrewerPalette::listSchemes();
        CHECK(schemes.contains("Spectral"));
    }

    SECTION("Can create ColorBrewer ramp and get scheme colors")
    {
        QList<QColor> colorList = QgsColorBrewerPalette::listSchemeColors("Spectral", 9);
        CHECK(colorList.size() == 9);
        // Verify colors are valid
        for (const QColor &c : colorList) {
            CHECK(c.isValid());
        }
    }
}
