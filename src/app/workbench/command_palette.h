/***************************************************************************
 * command_palette.h — keyboard-first searchable capability surface (5.0 D)
 *
 * Projects CommandRegistry entries into a fuzzy, keyboard-first palette.
 * The palette NEVER executes anything by itself: activation goes through
 * CommandRegistry::trigger(), so a palette entry and a ribbon button share
 * the same handler and availability contract. Unavailable commands stay
 * visible but show *why* they are disabled (goal: explain, not hide).
 *
 * Bridge surfaces (toolbox algorithms / workflow templates) are added as
 * delegated entries in later milestones — the palette remains a pure view.
 ***************************************************************************/
#pragma once

#include <QDialog>
#include <QStringList>

#include "command_registry.h"

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QLabel;

namespace sicnu::app
{

class CommandPalette : public QDialog
{
    Q_OBJECT
  public:
    /// Max rows kept visible while filtering (bounded — palette is a popup).
    static constexpr int kMaxRows = 60;

    explicit CommandPalette( CommandRegistry *registry, QWidget *parent = nullptr );

    /// Opens centered on @p anchor with an empty query (or preselected text).
    void openPalette();

  public slots:
    /// Executes the current row (the Enter path; public for keyboard-driven
    /// hosts and tests). No-op when the row is unavailable.
    void runCurrent();

  protected:
    bool eventFilter( QObject *watched, QEvent *event ) override;

  private slots:
    void reapplyFilter();

  private:
    struct Entry
    {
        QString id;
        QString title;
        QString category;
        QStringList keywords;
        int lastUsedRank = -1; // recency index (0 = most recent)
    };

    void rebuildEntries();
    void rebuildRecency();
    QListWidgetItem *makeItem( const Entry &entry, const QString &reason );

    CommandRegistry *m_registry = nullptr;
    QLineEdit *m_input = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_hint = nullptr;
    QVector<Entry> m_entries;
    QStringList m_recent; // most-recent-first command ids
};

} // namespace sicnu::app
