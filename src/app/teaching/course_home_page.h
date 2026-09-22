#pragma once

#include "teaching/course_home_view_model.h"

#include <QWidget>

class QListWidget;
class QLabel;
class QPushButton;
class QComboBox;

namespace sicnu::app::teaching {

class CourseHomePage : public QWidget
{
  Q_OBJECT
public:
  explicit CourseHomePage( QWidget *parent = nullptr );

  void setViewModel( const sicnu::teaching::CourseHomeViewModel &vm );
  sicnu::teaching::CourseHomeViewModel viewModel() const { return m_vm; }

signals:
  void labSelected( const QString &moduleId, const QString &labId );
  void continueRequested( const QString &moduleId, const QString &labId );
  void modeChanged( const QString &modeWire );

private:
  void rebuild();

  sicnu::teaching::CourseHomeViewModel m_vm;
  QLabel *m_title = nullptr;
  QLabel *m_progress = nullptr;
  QLabel *m_audience = nullptr;
  QListWidget *m_moduleList = nullptr;
  QPushButton *m_continueBtn = nullptr;
  QComboBox *m_modeCombo = nullptr;
};

} // namespace sicnu::app::teaching
