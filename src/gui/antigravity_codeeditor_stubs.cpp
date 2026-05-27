// ANTIGRAVITY: Minimal stub implementations for removed codeeditor classes.
// These exist only to satisfy qgsgui.cpp's use of QgsCodeEditorColorSchemeRegistry.
// None of the code editor functionality is available in this build.
#include "codeeditors/qgscodeeditorcolorscheme.h"
#include "codeeditors/qgscodeeditorcolorschemeregistry.h"

QgsCodeEditorColorScheme::QgsCodeEditorColorScheme(const QString &id, const QString &name)
    : mId(id), mThemeName(name) {}

QColor QgsCodeEditorColorScheme::color(ColorRole) const { return QColor(); }
void QgsCodeEditorColorScheme::setColor(ColorRole, const QColor &) {}
void QgsCodeEditorColorScheme::setColors(const QMap<ColorRole, QColor> &) {}

QgsCodeEditorColorSchemeRegistry::QgsCodeEditorColorSchemeRegistry() = default;
bool QgsCodeEditorColorSchemeRegistry::addColorScheme(const QgsCodeEditorColorScheme &) { return false; }
bool QgsCodeEditorColorSchemeRegistry::removeColorScheme(const QString &) { return false; }
QStringList QgsCodeEditorColorSchemeRegistry::schemes() const { return {}; }
QgsCodeEditorColorScheme QgsCodeEditorColorSchemeRegistry::scheme(const QString &) const { return {}; }
