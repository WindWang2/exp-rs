// rs_class_def.h — Phase 10A: classification class definition (id + name + color)
#pragma once
#include "qgis_analysis_export.h"
#include <QString>
#include <QColor>

class QGIS_ANALYSIS_EXPORT RsClassDef
{
  public:
    RsClassDef() = default;
    RsClassDef( int id, const QString &name, const QColor &color )
      : mId( id ), mName( name ), mColor( color ) {}

    int id() const { return mId; }
    QString name() const { return mName; }
    QColor color() const { return mColor; }

    void setName( const QString &n ) { mName = n; }
    void setColor( const QColor &c ) { mColor = c; }

  private:
    int mId = 0;
    QString mName;
    QColor mColor = Qt::gray;
};
