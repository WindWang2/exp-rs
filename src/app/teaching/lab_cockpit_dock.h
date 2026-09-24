#pragma once

#include "teaching/lab_session_state.h"

#include <json/json.h>

#include <QWidget>

#include <functional>

class QStackedWidget;
class QgisDesktopWindow;

namespace sicnu::app::teaching {

class CourseHomePage;
class GuidedLabWorkspace;

/// Where this lab's runs are recorded, produced by the app shell from the
/// open project. An empty experimentDbPath means "no lab recording context"
/// — capsule export must then fail honestly instead of fabricating a ref.
struct LabCapsuleSource
{
  QString experimentDbPath;
  QString datasetDbPath;
  QString experimentId;
  QString workspaceRoot; // producing workspace (project dir), may be empty
};

/// Hands an operator id to the EXISTING Processing surface (production wires
/// QgisDesktopWindow::openProcessingAlgorithm). The dock never clones the
/// operator UI; without a shell it degrades to an honest message.
using OperatorLauncher = std::function<void( const QString &operatorId )>;
using CapsuleSourceProvider = std::function<LabCapsuleSource()>;

/// Top-level Undergraduate Lab Cockpit page host.
/// Flow: Course Home → readiness → steps → validate → feedback → export.
class LabCockpitDock : public QWidget
{
  Q_OBJECT
public:
  explicit LabCockpitDock( QgisDesktopWindow *mainWindow, QWidget *parent = nullptr );

  /// Shell-provided seams (see main_window_docks.cpp). Calling either twice
  /// replaces the previous provider; empty/null resets to the honest default.
  void setOperatorLauncher( OperatorLauncher launcher );
  void setCapsuleSourceProvider( CapsuleSourceProvider provider );

  void loadCourseFromRepo();
  void restoreSession();
  bool saveSession();

  QString sessionPath() const;

public slots:
  void showCourseHome();
  void openLab( const QString &moduleId, const QString &labId );

private:
  void wireSignals();
  void launchOperator( const QString &operatorId );
  void exportCapsule();
  void appendExportNote( const QString &noteZh );
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
  Json::Value m_labDoc; // current lab document (grading_rules ref for validation)
  bool m_offline = true;
  OperatorLauncher m_launchOperator;
  CapsuleSourceProvider m_capsuleSource;
};

} // namespace sicnu::app::teaching
