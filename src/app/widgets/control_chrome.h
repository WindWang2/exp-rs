#pragma once

#include "app/design_tokens.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace SicnuUi
{

enum class ActionTone { Primary, Secondary, Danger };

inline void styleActionButton( QPushButton &button, ActionTone tone, const QIcon &icon = {} )
{
  button.setProperty( "rsActionButton", true );
  button.setProperty( "primary", tone == ActionTone::Primary );
  button.setProperty( "ghost", tone == ActionTone::Secondary );
  button.setProperty( "danger", tone == ActionTone::Danger );
  if ( !icon.isNull() )
    button.setIcon( icon );
  button.setIconSize( QSize( Tokens::kIconInline, Tokens::kIconInline ) );
  button.setCursor( Qt::PointingHandCursor );
  button.style()->unpolish( &button );
  button.style()->polish( &button );
}

inline QToolButton *makePanelToolButton( QWidget *parent, const QIcon &icon, const QString &label )
{
  auto *button = new QToolButton( parent );
  button->setProperty( "rsPanelToolButton", true );
  button->setToolButtonStyle( Qt::ToolButtonIconOnly );
  button->setIcon( icon );
  button->setIconSize( QSize( Tokens::kIconInline, Tokens::kIconInline ) );
  button->setAutoRaise( true );
  button->setFocusPolicy( Qt::StrongFocus );
  button->setCursor( Qt::PointingHandCursor );
  button->setToolTip( label );
  button->setAccessibleName( label );
  return button;
}

inline QPushButton *makeBrowseButton( QWidget *parent, const QString &label )
{
  auto *button = new QPushButton( label, parent );
  button->setAutoDefault( false );
  styleActionButton( *button, ActionTone::Secondary, QIcon( QStringLiteral( ":/icons/o_en" ) ) );
  return button;
}

inline QVBoxLayout *makePanelLayout( QWidget *panel )
{
  auto *layout = new QVBoxLayout( panel );
  const int inset = Tokens::spacing( 2 );
  layout->setContentsMargins( inset, inset, inset, inset );
  layout->setSpacing( inset );
  return layout;
}

inline QVBoxLayout *makeSectionLayout( QWidget *section )
{
  auto *layout = makePanelLayout( section );
  layout->setContentsMargins( Tokens::spacing( 3 ), Tokens::spacing( 2 ),
                              Tokens::spacing( 3 ), Tokens::spacing( 2 ) );
  return layout;
}

inline QHBoxLayout *makeControlRow()
{
  auto *row = new QHBoxLayout;
  row->setContentsMargins( 0, 0, 0, 0 );
  row->setSpacing( Tokens::spacing( 2 ) );
  return row;
}

inline QHBoxLayout *makeFieldActionRow( QWidget *field, QPushButton *action )
{
  field->setProperty( "rsActionField", true );
  auto *row = makeControlRow();
  row->addWidget( field, 1 );
  row->addWidget( action );
  return row;
}

inline void stylePanelTree( QTreeView &tree )
{
  tree.setProperty( "rsPanelTree", true );
  tree.setIconSize( QSize( Tokens::kIconPanelTree, Tokens::kIconPanelTree ) );
  tree.setIndentation( Tokens::kIconPanelTree + Tokens::spacing( 1 ) );
  tree.setAlternatingRowColors( true );
}

} // namespace SicnuUi
