// Phase 10A Task 10.6 — JM separability matrix dock widget.
//
// Paints an N x N heat matrix from a hash of class-id pairs -> JM distance.
// Cells are colored by JM range: red (<1.0), yellow (1.0-1.5), yellow-green
// (1.5-1.9), green (>=1.9). Diagonal cells render an em-dash on light gray.
// Pairs missing from the hash render as blank near-white cells, so the widget
// can be populated incrementally (or partially) by Task 10.8.
#pragma once

#include <QColor>
#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

class RsJmMatrixWidget : public QWidget
{
    Q_OBJECT

  public:
    /** One class entry; matches RsClassDef but kept local to avoid coupling. */
    struct ClassEntry
    {
        int id;
        QString name;
        QColor color;
    };

    explicit RsJmMatrixWidget( QWidget *parent = nullptr );

    /**
     * Replace the displayed matrix.
     * \a jm uses unordered (smaller-id, larger-id) keys so callers don't
     * need to populate both upper- and lower-triangle entries.
     */
    void setData( const QVector<ClassEntry> &classes,
                  const QHash<QPair<int, int>, double> &jm );

  protected:
    void paintEvent( QPaintEvent *event ) override;

  private:
    QVector<ClassEntry> mClasses;
    QHash<QPair<int, int>, double> mJm;
};
