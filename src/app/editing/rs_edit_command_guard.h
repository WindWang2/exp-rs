// rs_edit_command_guard.h — RAII wrapper over QgsVectorLayer edit commands.
//
// beginEditCommand(text) on construction (when the session allows it);
// endEditCommand() on normal destruction — one guard = one undoable command,
// so an entire brush stroke or multi-feature operation collapses to a single
// undo step. cancel() destroys the command instead (all changes made inside
// are discarded by the layer's edit buffer).
//
// A guard created for a locked layer (or unknown/destroyed layer) is
// invalid(): callers must check isValid() before writing anything.
#pragma once

#include <QPointer>
#include <QString>

#include <qgsvectorlayer.h>

#include "rs_edit_session.h"

class RsEditCommandGuard
{
  public:
    RsEditCommandGuard( RsEditSession *session, QgsVectorLayer *layer, const QString &text )
      : mLayer( layer )
    {
        if ( !layer )
            return;
        if ( session )
        {
            const QString id = layer->id();
            if ( !session->isAttached( id ) || session->isLocked( id ) )
                return;
        }
        layer->beginEditCommand( text );
        mActive = true;
    }

    ~RsEditCommandGuard()
    {
        if ( mActive && mLayer )
            mLayer->endEditCommand();
    }

    RsEditCommandGuard( const RsEditCommandGuard & ) = delete;
    RsEditCommandGuard &operator=( const RsEditCommandGuard & ) = delete;

    bool isValid() const { return mActive; }

    /// Discard everything done inside this command (layer destroys it).
    void cancel()
    {
        if ( mActive && mLayer )
        {
            mLayer->destroyEditCommand();
            mActive = false;
        }
    }

  private:
    QPointer<QgsVectorLayer> mLayer;
    bool mActive = false;
};
