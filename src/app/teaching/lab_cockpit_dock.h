#pragma once

#include "teaching/lab_session_state.h"

#include <json/json.h>

#include <QWidget>

class QStackedWidget;
class QgisDesktopWindow;

namespace sicnu::app::teaching {

class CourseHomePage;
class GuidedLabWorkspace;

/// Top-level Undergraduate Lab Cockpit page host.
/// Flow: Course Home → readiness → steps → validate → feedback → export.
class LabCockpitDock : public QWidget
{
  Q_OBJECT
public:
  explicit LabCockpitDock( QgisDesktopWindow *mainWindow, QWidget *parent = nullptr );

  void loadCourseFromRepo();
  void restoreSession();
  bool saveSession();

  QString sessionPath() const;

public slots:
  void showCourseHome();
  void openLab( const QString &moduleId, const QString &labId );

private:
  void wireSignals();
  Json::Value loadJsonFile( const QString &path ) const;
  Json::Value findAvailabilityLabSlice( const Json::Value &availability,
                                        const std::string &labId ) const;

  QgisDesktopWindow *m_mainWindow = nullptr;
  QStackedWidget *m_stack = nullptr;
  CourseHomePage *m_home = nullptr;
  GuidedLabWorkspace *m_workspace = nullptr;
  sicnu::teaching::LabSessionState m_session;
  Json::Value m_manifest;
  Json::Value m_availability;
  Json::Value m_progressSummary;
  bool m_offline = true;
};

} // namespace sicnu::app::teaching
