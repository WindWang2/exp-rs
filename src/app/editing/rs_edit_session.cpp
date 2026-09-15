// rs_edit_session.cpp — see rs_edit_session.h for the authority contract.
#include "rs_edit_session.h"

#include <qgsmaplayer.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>

#include <QUndoStack>

namespace
{

RsLayerEditState snapshotLayer( QgsVectorLayer *layer, const RsLayerEditState &cached )
{
    RsLayerEditState s = cached;
    if ( !layer )
        return s;
    s.editing = layer->isEditable();
    s.modified = layer->isModified();
    if ( QUndoStack *stack = layer->undoStack() )
    {
        s.undoDepth = stack->index();
        s.redoDepth = stack->count() - stack->index();
    }
    s.selectedCount = layer->selectedFeatureCount();
    return s;
}

} // namespace

RsEditSession::RsEditSession( QObject *parent )
  : QObject( parent )
{
    // Layer removal cannot be blocked; drop tracking (and our connections)
    // BEFORE the layer dies so no slot ever fires on a dangling object.
    connect( QgsProject::instance(), qOverload<const QString &>( &QgsProject::layerWillBeRemoved ),
             this, &RsEditSession::handleProjectLayerRemoved );
}

RsEditSession::~RsEditSession()
{
    // Drop project connection first: detachLayer below may roll layers back,
    // which must not re-enter handleProjectLayerRemoved mid-destruction.
    disconnect( QgsProject::instance(), qOverload<const QString &>( &QgsProject::layerWillBeRemoved ),
                this, &RsEditSession::handleProjectLayerRemoved );
    for ( auto it = mEntries.constBegin(); it != mEntries.constEnd(); ++it )
    {
        for ( const QMetaObject::Connection &c : it->connections )
            disconnect( c );
    }
}

QString RsEditSession::attachLayer( QgsVectorLayer *layer, bool startEditing )
{
    if ( !layer )
        return QStringLiteral( "null layer" );

    const QString layerId = layer->id();
    if ( mEntries.contains( layerId ) )
        return QStringLiteral( "layer already attached: %1" ).arg( layer->name() );

    if ( startEditing && !layer->isEditable() )
    {
        // Adopts an in-progress editing session; fails closed when the layer
        // itself cannot edit (invalid, read-only provider).
        if ( !layer->startEditing() )
            return QStringLiteral( "layer cannot start editing: %1" ).arg( layer->name() );
    }

    Entry entry;
    entry.layer = layer;
    mEntries.insert( layerId, entry );

    RsLayerEditState s;
    s.layerId = layerId;
    // Count once at attach; afterwards featureAdded/Deleted keep it O(1).
    s.featureCount = layer->featureCount();
    mStates.insert( layerId, s );

    connectLayer( layerId, layer );
    refresh( layerId );
    emit stateChanged();
    return QString();
}

QString RsEditSession::detachLayer( const QString &layerId, bool rollbackDirty )
{
    auto it = mEntries.constFind( layerId );
    if ( it == mEntries.constEnd() )
        return QString();
    if ( QgsVectorLayer *layer = it->layer.data() )
    {
        if ( rollbackDirty && layer->isEditable() && layer->isModified() )
        {
            // Default deleteBuffer=true: discards changes AND exits edit mode,
            // leaving the layer in a consistent non-editing state.
            layer->rollBack();
        }
    }
    dropLayer( layerId );
    emit stateChanged();
    return QString();
}

QStringList RsEditSession::attachedLayerIds() const
{
    return QStringList( mStates.keyBegin(), mStates.keyEnd() );
}

bool RsEditSession::isAttached( const QString &layerId ) const
{
    return mStates.contains( layerId );
}

QgsVectorLayer *RsEditSession::layer( const QString &layerId ) const
{
    auto it = mEntries.constFind( layerId );
    return it == mEntries.constEnd() ? nullptr : it->layer.data();
}

RsLayerEditState RsEditSession::state( const QString &layerId ) const
{
    return mStates.value( layerId );
}

bool RsEditSession::isDirty() const
{
    for ( auto it = mStates.constBegin(); it != mStates.constEnd(); ++it )
    {
        if ( it->modified )
            return true;
    }
    return false;
}

bool RsEditSession::setLocked( const QString &layerId, bool locked )
{
    if ( !mStates.contains( layerId ) )
        return false;
    if ( mStates[layerId].locked == locked && mEntries[layerId].locked == locked )
        return true;
    mStates[layerId].locked = locked;
    mEntries[layerId].locked = locked;
    emit layerStateChanged( layerId );
    emit stateChanged();
    return true;
}

bool RsEditSession::isLocked( const QString &layerId ) const
{
    return mStates.value( layerId ).locked;
}

bool RsEditSession::undo( const QString &layerId )
{
    auto it = mEntries.constFind( layerId );
    if ( it == mEntries.constEnd() || it->locked )
        return false;
    QgsVectorLayer *layer = it->layer.data();
    if ( !layer || !layer->undoStack() || layer->undoStack()->index() == 0 )
        return false;
    layer->undoStack()->undo();
    return true;
}

bool RsEditSession::redo( const QString &layerId )
{
    auto it = mEntries.constFind( layerId );
    if ( it == mEntries.constEnd() || it->locked )
        return false;
    QgsVectorLayer *layer = it->layer.data();
    if ( !layer || !layer->undoStack() )
        return false;
    if ( layer->undoStack()->index() >= layer->undoStack()->count() )
        return false;
    layer->undoStack()->redo();
    return true;
}

int RsEditSession::undoAll()
{
    int moved = 0;
    const QStringList ids = attachedLayerIds();
    for ( const QString &id : ids )
    {
        if ( undo( id ) )
            ++moved;
    }
    return moved;
}

int RsEditSession::redoAll()
{
    int moved = 0;
    const QStringList ids = attachedLayerIds();
    for ( const QString &id : ids )
    {
        if ( redo( id ) )
            ++moved;
    }
    return moved;
}

bool RsEditSession::commit( const QString &layerId, QString *error )
{
    auto it = mEntries.constFind( layerId );
    if ( it == mEntries.constEnd() )
    {
        if ( error )
            *error = QStringLiteral( "layer not attached: %1" ).arg( layerId );
        return false;
    }
    QgsVectorLayer *layer = it->layer.data();
    if ( !layer )
    {
        if ( error )
            *error = QStringLiteral( "layer destroyed: %1" ).arg( layerId );
        return false;
    }
    if ( !layer->isEditable() )
    {
        if ( error )
            *error = QStringLiteral( "layer not in edit mode: %1" ).arg( layer->name() );
        emit commitFinished( layerId, false, error ? *error : QString() );
        return false;
    }

    const bool ok = layer->commitChanges();
    if ( !ok )
    {
        QStringList errs = layer->commitErrors();
        if ( errs.isEmpty() )
            errs << QStringLiteral( "commit rejected by layer without a reported reason" );
        if ( error )
            *error = errs.join( QLatin1String( "; " ) );
    }
    refresh( layerId );
    emit commitFinished( layerId, ok, ok ? QString() : ( error ? *error : QString() ) );
    emit stateChanged();
    return ok;
}

int RsEditSession::commitAll( QStringList *errors )
{
    int committed = 0;
    const QStringList ids = attachedLayerIds();
    for ( const QString &id : ids )
    {
        if ( !mStates.value( id ).modified )
            continue;
        QString err;
        if ( commit( id, &err ) )
        {
            ++committed;
        }
        else if ( errors )
        {
            *errors << QStringLiteral( "%1: %2" ).arg( id, err );
        }
    }
    return committed;
}

bool RsEditSession::rollback( const QString &layerId, QString *error )
{
    auto it = mEntries.constFind( layerId );
    if ( it == mEntries.constEnd() )
    {
        if ( error )
            *error = QStringLiteral( "layer not attached: %1" ).arg( layerId );
        return false;
    }
    QgsVectorLayer *layer = it->layer.data();
    if ( !layer )
    {
        if ( error )
            *error = QStringLiteral( "layer destroyed: %1" ).arg( layerId );
        return false;
    }
    if ( !layer->isEditable() )
    {
        // Nothing to discard; treat as success (idempotent).
        return true;
    }
    const bool ok = layer->rollBack(); // discards changes and stops editing
    refresh( layerId );
    emit stateChanged();
    return ok;
}

int RsEditSession::rollbackAll()
{
    int rolled = 0;
    const QStringList ids = attachedLayerIds();
    for ( const QString &id : ids )
    {
        if ( rollback( id ) )
            ++rolled;
    }
    return rolled;
}

void RsEditSession::connectLayer( const QString &layerId, QgsVectorLayer *layer )
{
    Entry &entry = mEntries[layerId];
    auto changed = [this, layerId]() { refresh( layerId ); };
    entry.connections << connect( layer, &QgsVectorLayer::layerModified, this, changed );
    entry.connections << connect( layer, &QgsVectorLayer::editingStarted, this, changed );
    entry.connections << connect( layer, &QgsVectorLayer::editingStopped, this, changed );
    entry.connections << connect( layer, &QgsVectorLayer::selectionChanged, this, changed );
    entry.connections << connect( layer, &QgsVectorLayer::geometryChanged, this, changed );
    entry.connections << connect( layer, &QgsVectorLayer::featureAdded, this,
                                  [this, layerId]( QgsFeatureId )
    {
        if ( mStates.contains( layerId ) && mStates[layerId].featureCount >= 0 )
            ++mStates[layerId].featureCount;
        refresh( layerId );
    } );
    entry.connections << connect( layer, &QgsVectorLayer::featureDeleted, this,
                                  [this, layerId]( QgsFeatureId )
    {
        if ( mStates.contains( layerId ) && mStates[layerId].featureCount > 0 )
            --mStates[layerId].featureCount;
        refresh( layerId );
    } );
    if ( QUndoStack *stack = layer->undoStack() )
    {
        entry.connections << connect( stack, &QUndoStack::indexChanged, this, changed );
    }
}

void RsEditSession::refresh( const QString &layerId )
{
    auto stateIt = mStates.find( layerId );
    auto entryIt = mEntries.constFind( layerId );
    if ( stateIt == mStates.end() || entryIt == mEntries.constEnd() )
        return;
    const RsLayerEditState before = *stateIt;
    if ( QgsVectorLayer *layer = entryIt->layer.data() )
    {
        *stateIt = snapshotLayer( layer, *stateIt );
    }
    else
    {
        stateIt->editing = false;
        stateIt->modified = false;
        stateIt->undoDepth = 0;
        stateIt->redoDepth = 0;
        stateIt->selectedCount = 0;
    }
    if ( *stateIt != before )
    {
        emit layerStateChanged( layerId );
        emit stateChanged();
    }
}

void RsEditSession::dropLayer( const QString &layerId )
{
    auto it = mEntries.find( layerId );
    if ( it == mEntries.end() )
        return;
    for ( const QMetaObject::Connection &c : it->connections )
        disconnect( c );
    mEntries.erase( it );
    mStates.remove( layerId );
}

void RsEditSession::handleProjectLayerRemoved( const QString &layerId )
{
    if ( !mEntries.contains( layerId ) )
        return;
    dropLayer( layerId );
    emit layerStateChanged( layerId );
    emit stateChanged();
}
