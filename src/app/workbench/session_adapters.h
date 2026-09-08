/***************************************************************************
 * session_adapters.h — InteractiveSession adapters over session windows
 *
 * Batch 1: classification lab. The adapter translates the shared
 * InteractiveSession contract onto the existing window accessors; the
 * window keeps owning its session state and TaskCenter seam.
 ***************************************************************************/
#pragma once

#include "interactive_session.h"

#include <QPointer>
#include <functional>

class QgsClassificationMainWindow;

namespace sicnu::app
{

class ClassifySessionAdapter : public InteractiveSession
{
    Q_OBJECT
  public:
    using Opener = std::function<QgsClassificationMainWindow *()>;

    /// @p opener lazily constructs/raises the classification lab (the same
    /// lazy-open slot the workbench adapter uses).
    explicit ClassifySessionAdapter( Opener opener, QObject *parent = nullptr );

    QString sessionId() const override { return QStringLiteral( "classify" ); }
    bool isDirty() const override;
    void clearDirty() override;
    bool hasInFlightCompute() const override;
    bool requestCancel() override;
    bool requestClose() override;

  private:
    /// Ensures the lab exists; returns nullptr only if the opener fails.
    QgsClassificationMainWindow *ensureWindow() const;

    Opener m_opener;
    mutable QPointer<QgsClassificationMainWindow> m_window;
};

} // namespace sicnu::app
