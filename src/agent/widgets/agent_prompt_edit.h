#pragma once

#include <QAbstractTextDocumentLayout>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QTextEdit>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace sicnu::agent
{

/// Presentation-only composer. The owner decides whether sending is allowed;
/// this widget never starts a conversation, cancels a run or submits a task.
class AgentPromptEdit final : public QTextEdit
{
  public:
    explicit AgentPromptEdit( QWidget *parent = nullptr ) : QTextEdit( parent )
    {
      setAcceptRichText( false );
      setTabChangesFocus( true );
      connect( document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
               this, [this] { updateHeight(); } );
      updateHeight();
    }

    void setSubmitHandler( std::function<void()> handler ) { m_submit = std::move( handler ); }

  protected:
    void keyPressEvent( QKeyEvent *event ) override
    {
      // Return stays a newline (including IME input); submission is explicit.
      // Ignore repeat so a held shortcut cannot submit successive turns.
      const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
      if ( ( event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter )
           && modifiers == Qt::ControlModifier )
      {
        event->accept();
        if ( !event->isAutoRepeat() && m_submit ) m_submit();
        return;
      }
      QTextEdit::keyPressEvent( event );
    }

    void resizeEvent( QResizeEvent *event ) override
    {
      QTextEdit::resizeEvent( event );
      updateHeight();
    }

    void changeEvent( QEvent *event ) override
    {
      QTextEdit::changeEvent( event );
      if ( event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange ) updateHeight();
    }

  private:
    void updateHeight()
    {
      const int line = fontMetrics().lineSpacing();
      const int chrome = 2 * frameWidth() + static_cast<int>( std::ceil( 2 * document()->documentMargin() ) );
      const qreal content = document()->documentLayout()->documentSize().height() + 2 * frameWidth();
      const int height = static_cast<int>( std::ceil( std::clamp( content,
        static_cast<qreal>( 2 * line + chrome ), static_cast<qreal>( 6 * line + chrome ) ) ) );
      if ( minimumHeight() != height || maximumHeight() != height ) setFixedHeight( height );
    }

    std::function<void()> m_submit;
};

} // namespace sicnu::agent
