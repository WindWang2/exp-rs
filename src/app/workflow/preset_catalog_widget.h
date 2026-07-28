// src/app/workflow/preset_catalog_widget.h
#pragma once

#include <QWidget>
#include <QListWidget>
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

signals:
  void presetSelected( const sicnu::workflow::WorkflowDefinition &def );

private slots:
  void onItemDoubleClicked( QListWidgetItem *item );
  void onItemSelectionChanged();
  void onLoadButtonClicked();

private:
  void populatePresets();

  QListWidget *mListWidget = nullptr;
  QLabel *mDescLabel = nullptr;
  QPushButton *mLoadBtn = nullptr;
  std::vector<PresetItemInfo> mPresets;
};

} // namespace sicnu::workflow::gui
