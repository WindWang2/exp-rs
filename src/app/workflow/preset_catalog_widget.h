#pragma once

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <vector>

#include "workflow_definition.h"

namespace sicnu::workflow::gui {

struct PresetItemInfo
{
  QString id;
  QString title;
  QString category;
  QString description;
  WorkflowDefinition definition;
};

class PresetCatalogWidget : public QWidget
{
  Q_OBJECT

public:
  explicit PresetCatalogWidget( QWidget *parent = nullptr );
  ~PresetCatalogWidget() override = default;

  static std::vector<PresetItemInfo> builtinPresets();
  int visiblePresetCount() const;

signals:
  void presetSelected( const sicnu::workflow::WorkflowDefinition &def );

private slots:
  void onItemDoubleClicked( QListWidgetItem *item );
  void onItemSelectionChanged();
  void onLoadButtonClicked();
  void onSearchTextChanged( const QString &text );

private:
  void populatePresets( const QString &filter = QString() );

  QLineEdit *mSearchEdit = nullptr;
  QListWidget *mListWidget = nullptr;
  QLabel *mDescLabel = nullptr;
  QPushButton *mLoadBtn = nullptr;
  std::vector<PresetItemInfo> mPresets;
};

} // namespace sicnu::workflow::gui
