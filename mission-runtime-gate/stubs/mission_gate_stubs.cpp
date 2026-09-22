// mission-runtime-gate/stubs/mission_gate_stubs.cpp
//
// Harness-only implementations of the three QGIS-dependent symbols that the
// mission context chain references (mission_context.cpp ->
// objectKindToken / ContextRules::primaryObject / ContextRules::selectedLayerIds).
// The real definitions live in object_identity.cpp and selection_context.cpp,
// which need qgis_core and cannot compile in this out-of-tree harness.
//
// test_mission_runtime_parity.cpp parses the REAL sources and asserts these
// stubs match them, so a change to the real implementations cannot drift
// silently past this harness.

#include "app/workbench/object_identity.h"
#include "app/workbench/selection_context.h"

#include <QString>

namespace sicnu::app
{

// Exact copy of object_identity.cpp:18-40 (verified by the parity gate).
QString objectKindToken( ObjectKind kind )
{
    switch ( kind )
    {
        case ObjectKind::Layer:
            return QStringLiteral( "layer" );
        case ObjectKind::Asset:
            return QStringLiteral( "asset" );
        case ObjectKind::Result:
            return QStringLiteral( "result" );
        case ObjectKind::Dataset:
            return QStringLiteral( "dataset" );
        case ObjectKind::ExperimentRun:
            return QStringLiteral( "experiment_run" );
        case ObjectKind::Model:
            return QStringLiteral( "model" );
        case ObjectKind::WorkflowRun:
            return QStringLiteral( "workflow_run" );
        case ObjectKind::None:
            break;
    }
    return QStringLiteral( "none" );
}

namespace ContextRules
{

// Id-list branches copied from object_identity.cpp:45-68. The layer-pointer
// branches need QgsMapLayer (unavailable here); the harness never constructs
// snapshots with live layer pointers, so they return a null ref.
WorkbenchObjectRef primaryObject( const SelectionContextSnapshot &snapshot )
{
    if ( !snapshot.selectedWorkflowRunIds.isEmpty() )
        return { ObjectKind::WorkflowRun, snapshot.selectedWorkflowRunIds.first(), QString() };
    if ( !snapshot.selectedExperimentIds.isEmpty() )
        return { ObjectKind::ExperimentRun, snapshot.selectedExperimentIds.first(), QString() };
    if ( !snapshot.selectedDatasetIds.isEmpty() )
        return { ObjectKind::Dataset, snapshot.selectedDatasetIds.first(), QString() };
    if ( !snapshot.selectedModelIds.isEmpty() )
        return { ObjectKind::Model, snapshot.selectedModelIds.first(), QString() };
    if ( !snapshot.selectedResultIds.isEmpty() )
        return { ObjectKind::Result, snapshot.selectedResultIds.first(), QString() };
    if ( !snapshot.selectedAssetIds.isEmpty() )
        return { ObjectKind::Asset, snapshot.selectedAssetIds.first(), QString() };
    return {};
}

// No live QgsMapLayer exists in the harness: an empty list is the honest
// projection of "no layer pointers" (selection_context.cpp:203-214).
QStringList selectedLayerIds( const SelectionContextSnapshot & )
{
    return {};
}

} // namespace ContextRules

} // namespace sicnu::app

// selection_context.cpp references WorkbenchHost (workbench_host.h, Q_OBJECT)
// but workbench_host.cpp is NOT part of the harness source list — the shell
// TU is not compilable here. Define exactly the symbols the harness's TUs
// reference (the same repair class as the macro comment in tests/CMakeLists.txt):
// the staticMetaObject for the FunctionPointer connect<> overload, the two
// accessors, and the manually-defined signal.
const QMetaObject sicnu::app::WorkbenchHost::staticMetaObject{};

const QMetaObject *sicnu::app::WorkbenchHost::metaObject() const
{
    return &staticMetaObject;
}

void *sicnu::app::WorkbenchHost::qt_metacast( const char * )
{
    return nullptr;
}

int sicnu::app::WorkbenchHost::qt_metacall( QMetaObject::Call, int, void ** )
{
    return -1;
}

QString sicnu::app::WorkbenchHost::activeWorkbenchId() const
{
    return QString();
}

sicnu::app::IWorkbench *sicnu::app::WorkbenchHost::activeWorkbench() const
{
    return nullptr;
}

void sicnu::app::WorkbenchHost::activeWorkbenchChanged( const QString &, const QString & ) {}

// Remaining out-of-line members the harness never calls (defined so the
// vtable/emission of any additional reference cannot come back as a new
// link failure):
sicnu::app::WorkbenchHost::WorkbenchHost( QObject *parent )
    : QObject( parent )
{
}

bool sicnu::app::WorkbenchHost::registerWorkbench( IWorkbench * )
{
    return false;
}

sicnu::app::IWorkbench *sicnu::app::WorkbenchHost::workbench( const QString & ) const
{
    return nullptr;
}

QStringList sicnu::app::WorkbenchHost::workbenchIds() const
{
    return {};
}

bool sicnu::app::WorkbenchHost::activate( const QString & )
{
    return false;
}

void sicnu::app::WorkbenchHost::workbenchRegistered( const QString & ) {}
