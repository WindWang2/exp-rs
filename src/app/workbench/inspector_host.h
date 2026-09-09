/***************************************************************************
 * inspector_host.h — unified inspector surface (Workbench 5.0, Milestone F)
 *
 * One sectioned host replaces "one dock per feature" inspector drift. The
 * host follows the SelectionContext: sections declare which selections they
 * support; expensive sections populate lazily (on first show) and stale
 * async population is cancelled when the selection changes mid-flight.
 *
 * Sections read authoritative services only (layer tree / DataManager /
 * WorkspaceService) — never another panel's widgets.
 ***************************************************************************/
#pragma once

#include <QWidget>

#include "selection_context.h"

class QLabel;
class QTabWidget;
class QStackedWidget;

namespace sicnu::app
{

/**
 * Base class for one inspector section (a page widget). Subclasses populate
 * from a snapshot; population may be deferred (lazy) until the section is
 * first shown. `cancelPending()` is called whenever the selection changes so
 * background loads can drop stale results.
 */
class InspectorSection : public QWidget
{
    Q_OBJECT
  public:
    explicit InspectorSection( QWidget *parent = nullptr ) : QWidget( parent ) {}

    /// Stable section id (e.g. "general", "metadata", "provenance").
    virtual QString sectionId() const = 0;
    /// Tab title (中文).
    virtual QString title() const = 0;
    /// Order hint (lower first). Host sorts by this, then id.
    virtual int order() const { return 100; }
    /// Whether the section has anything to show for @p snapshot.
    virtual bool supports( const SelectionContextSnapshot &snapshot ) const = 0;
    /// (Re)populate from the snapshot. Called when shown and on context
    /// change while visible. Cheap sections may populate eagerly.
    virtual void populate( const SelectionContextSnapshot &snapshot ) = 0;
    /// Called on selection change when the section is NOT visible — drop any
    /// in-flight work for the previous selection.
    virtual void cancelPending() {}
  signals:
    /// Emit when async population finished (host re-checks placeholder state).
    void populationFinished();
};

/**
 * Tabbed host owning registered InspectorSections. Behavior contract:
 *  - selection change → visible sections repopulate; hidden lazy sections
 *    are invalidated (populated on first show after the change);
 *  - empty/unsupported selection → a placeholder page (no stale content);
 *  - multi-layer selection → sections receive the same snapshot (they decide
 *    how to summarize N > 1);
 *  - tab order follows InspectorSection::order().
 */
class InspectorHost : public QWidget
{
    Q_OBJECT
  public:
    explicit InspectorHost( QWidget *parent = nullptr );

    /// The host does not take ownership of sections beyond parenting.
    void registerSection( InspectorSection *section );
    QList<InspectorSection *> sections() const { return m_sections; }

    /// Attach the context source; the host follows its debounced changes.
    void attachSelectionContext( SelectionContext *context );

    /// For tests / shell: force a snapshot (bypasses attached context).
    void setSnapshot( const SelectionContextSnapshot &snapshot );

    /// The placeholder text used for empty selections.
    void setPlaceholderText( const QString &text );

  private slots:
    void onContextChanged( const sicnu::app::SelectionContextSnapshot &snapshot );
    void onTabChanged( int index );

  private:
    void rebuildTabs();
    InspectorSection *currentSection() const;
    /// Detaches every registered section from @p tabs (reparented to the host,
    /// hidden) so the tab widget can be destroyed without destroying sections
    /// the host still tracks (#777).
    void rescueSectionsFrom( QTabWidget *tabs );

    QStackedWidget *m_stack = nullptr;
    QLabel *m_placeholder = nullptr;
    QList<InspectorSection *> m_sections;
    SelectionContextSnapshot m_snapshot;
    bool m_hasSnapshot = false;
};

} // namespace sicnu::app
