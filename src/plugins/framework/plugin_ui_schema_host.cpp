/***************************************************************************
 * src/plugins/framework/plugin_ui_schema_host.cpp
 ***************************************************************************/
#include "plugin_ui_schema_host.h"

#include "exprs/plugin_ui_schema.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include <chrono>
#include <cmath>

using namespace exprs;

namespace sicnu::plugins {

namespace {

/// Bounded JSON -> QString helper (values are pre-validated by the schema).
QString jsonString( const Json::Value &value, const char *key )
{
    const Json::Value &field = value.get( key, Json::Value() );
    return field.isString() ? QString::fromStdString( field.asString() ) : QString();
}

/// QMetaObject::invokeMethod needs a QObject; route cross-thread response
/// application through the renderer itself.
constexpr int kDeliveryPollMs = 50;

/// Defense-in-depth render bounds (issue #1039): the schema is validated
/// before it reaches the renderer, but a caller that bypasses validation
/// must still not exhaust the GUI thread (stack via group recursion, or
/// memory via unbounded widget construction). Mirrors
/// exprs::PluginUiSchemaLimits::maxGroupDepth.
constexpr int kMaxRenderedGroupDepth = 4;
constexpr size_t kMaxRenderedControls = 4096;

} // namespace

PluginUiSchemaRenderer *PluginUiSchemaRenderer::instance()
{
    static PluginUiSchemaRenderer renderer;
    return &renderer;
}

PluginUiSchemaRenderer::PluginUiSchemaRenderer()
{
    mDelivery = std::thread( [this] { deliveryLoop(); } );
}

PluginUiSchemaRenderer::~PluginUiSchemaRenderer()
{
    mStopping = true;
    mEventCv.notify_all();
    if ( mDelivery.joinable() )
        mDelivery.join();
}

void PluginUiSchemaRenderer::deliveryLoop()
{
    for ( ;; )
    {
        PendingEvent pending;
        {
            std::unique_lock<std::mutex> lock( mEventMutex );
            mEventCv.wait( lock, [this] { return mStopping || !mQueue.empty(); } );
            if ( mStopping )
                return;
            pending = std::move( mQueue.front() );
            mQueue.pop_front();
        }
        // Copy the delegate under the lock (release clears the field);
        // deliver OUTSIDE any lock: the delegate does bounded IPC.
        std::shared_ptr<UiInvokeDelegate> delegate;
        {
            std::lock_guard<std::mutex> lock( mEventMutex );
            delegate = pending.record->delegate;
        }
        Json::Value response;
        if ( delegate )
            response = delegate->invoke( pending.event );
        // The production delegate returns the runtime's result envelope
        // ({ok, response:{state?...}}); the state lives one level down
        // (issue #1040 — reading result["state"] silently dropped every
        // production state update). A payload-shaped answer from a simple
        // delegate is still honored.
        Json::Value payload = response;
        if ( response.isObject() && response.isMember( "response" )
             && response["response"].isObject() )
            payload = response["response"];
        // Hop back to the GUI thread for widget mutation. Re-copy the
        // delegate: non-null means the record is still attached.
        QMetaObject::invokeMethod(
            this,
            [this, record = pending.record, payload]() {
                std::shared_ptr<UiInvokeDelegate> delegate;
                {
                    std::lock_guard<std::mutex> lock( mEventMutex );
                    delegate = record->delegate;
                }
                if ( delegate )
                    applyState( *record, payload.get( "state", Json::Value() ) );
            },
            Qt::QueuedConnection );
    }
}

void PluginUiSchemaRenderer::enqueueEvent( const QString &pluginId, const Json::Value &event )
{
    std::shared_ptr<RenderedRecord> record;
    for ( auto &candidate : mRecords )
        if ( candidate->pluginId == pluginId )
            record = candidate;
    if ( !record )
        return;
    {
        std::lock_guard<std::mutex> lock( mEventMutex );
        if ( mQueue.size() >= mQueueCap )
        {
            mQueue.pop_front(); // drop the OLDEST (newest interaction wins)
            mDroppedEvents.fetch_add( 1 );
        }
        mQueue.push_back( PendingEvent { record, event } );
    }
    mEventCv.notify_one();
}

void PluginUiSchemaRenderer::buildControls( QWidget *parent, const Json::Value &controls,
                                            const QString &contributionId, const QString &pluginId,
                                            RenderedRecord &record, int depth,
                                            size_t &renderedControls )
{
    QFormLayout *layout = new QFormLayout( parent );
    parent->setLayout( layout );

    // Unvalidated input must not recurse without bound (issue #1039).
    if ( !controls.isArray() || depth > kMaxRenderedGroupDepth )
        return;

    for ( const Json::Value &control : controls )
    {
        if ( renderedControls >= kMaxRenderedControls )
            return; // widget-flood bound
        ++renderedControls;
        const QString id = jsonString( control, "id" );
        const QString type = jsonString( control, "type" );
        const QString label = jsonString( control, "label" );
        const Json::Value defaultValue = control.get( "defaultValue", Json::Value() );

        ControlBinding binding;
        binding.contributionId = contributionId;
        binding.controlId = id;

        if ( type == "label" )
        {
            auto *widget = new QLabel( label, parent );
            layout->addRow( widget );
            continue; // read-only: no event, no binding
        }
        if ( type == "group" )
        {
            auto *box = new QGroupBox( label, parent );
            buildControls( box, control.get( "controls", Json::Value() ), contributionId, pluginId,
                           record, depth + 1, renderedControls );
            layout->addRow( box );
            continue;
        }

        QWidget *field = nullptr;
        if ( type == "text" )
        {
            // Type-guarded reads: an unvalidated hostile schema must fail
            // closed, never throw Json::LogicError through the GUI thread
            // (issue #1039).
            const bool multiline = control.isMember( "multiline" )
                                   && control[ "multiline" ].isBool()
                                   && control[ "multiline" ].asBool();
            if ( multiline )
            {
                auto *edit = new QPlainTextEdit( parent );
                edit->setMaximumHeight( 120 ); // bounded rendering
                field = edit;
                if ( defaultValue.isString() )
                    edit->setPlainText( QString::fromStdString( defaultValue.asString() ) );
                binding.readValue = [edit]() { return Json::Value( edit->toPlainText().toStdString() ); };
                binding.applyValue = [edit]( const Json::Value &value ) {
                    if ( value.isString() )
                    {
                        // setPlainText emits textChanged even for identical
                        // text: without the blocker a plugin echoing state
                        // would create an infinite invoke feedback loop.
                        QSignalBlocker blocker( edit );
                        edit->setPlainText( QString::fromStdString( value.asString() ) );
                    }
                };
            }
            else
            {
                auto *edit = new QLineEdit( parent );
                field = edit;
                if ( defaultValue.isString() )
                    edit->setText( QString::fromStdString( defaultValue.asString() ) );
                binding.readValue = [edit]() { return Json::Value( edit->text().toStdString() ); };
                binding.applyValue = [edit]( const Json::Value &value ) {
                    if ( value.isString() )
                    {
                        QSignalBlocker blocker( edit );
                        edit->setText( QString::fromStdString( value.asString() ) );
                    }
                };
            }
        }
        else if ( type == "number" || type == "slider" )
        {
            // Type-guarded reads (issue #1039): wrong-typed range fields fall
            // back to the documented defaults instead of throwing.
            const auto numberField = [&control]( const char *key, double fallback ) {
                const Json::Value &value = control.get( key, Json::Value() );
                return value.isNumeric() ? value.asDouble() : fallback;
            };
            const double minimum = numberField( "minimum", 0.0 );
            const double maximum = numberField( "maximum", 100.0 );
            const double step = numberField( "step", 1.0 );
            if ( type == "number" )
            {
                auto *spin = new QDoubleSpinBox( parent );
                spin->setRange( minimum, maximum );
                spin->setSingleStep( step > 0 ? step : 1.0 );
                if ( defaultValue.isNumeric() )
                    spin->setValue( defaultValue.asDouble() );
                field = spin;
                binding.readValue = [spin]() { return Json::Value( spin->value() ); };
                binding.applyValue = [spin]( const Json::Value &value ) {
                    if ( value.isNumeric() )
                    {
                        QSignalBlocker blocker( spin );
                        spin->setValue( value.asDouble() );
                    }
                };
            }
            else
            {
                // Slider: bounded integer mapping over the step grid.
                auto *slider = new QSlider( Qt::Horizontal, parent );
                const int steps = std::max( 1, std::min( 10000,
                    static_cast<int>( std::lround( ( maximum - minimum )
                                                   / ( step > 0 ? step : 1.0 ) ) ) ) );
                slider->setRange( 0, steps );
                auto toDouble = [minimum, step]( int position ) {
                    return minimum + position * step;
                };
                if ( defaultValue.isNumeric() )
                {
                    const double value = defaultValue.asDouble();
                    slider->setValue( static_cast<int>( std::lround( ( value - minimum )
                                                                     / ( step > 0 ? step : 1.0 ) ) ) );
                }
                field = slider;
                binding.readValue = [toDouble, slider]() {
                    return Json::Value( toDouble( slider->value() ) );
                };
                binding.applyValue = [minimum, step, slider]( const Json::Value &value ) {
                    if ( value.isNumeric() )
                    {
                        QSignalBlocker blocker( slider );
                        slider->setValue( static_cast<int>( std::lround(
                            ( value.asDouble() - minimum ) / ( step > 0 ? step : 1.0 ) ) ) );
                    }
                };
            }
        }
        else if ( type == "checkbox" )
        {
            auto *check = new QCheckBox( label, parent );
            field = check;
            if ( defaultValue.isBool() )
                check->setChecked( defaultValue.asBool() );
            binding.readValue = [check]() { return Json::Value( check->isChecked() ); };
            binding.applyValue = [check]( const Json::Value &value ) {
                if ( value.isBool() )
                {
                    QSignalBlocker blocker( check );
                    check->setChecked( value.asBool() );
                }
            };
        }
        else if ( type == "combo" )
        {
            auto *combo = new QComboBox( parent );
            const Json::Value &options = control.get( "options", Json::Value() );
            int defaultIndex = 0;
            if ( options.isArray() )
            {
                for ( int index = 0; index < static_cast<int>( options.size() ); ++index )
                {
                    // jsonString() is type-guarded (issue #1039): a non-string
                    // option label/value used to throw through the GUI thread.
                    const Json::Value &option = options[ index ];
                    combo->addItem( jsonString( option, "label" ), jsonString( option, "value" ) );
                    if ( defaultValue.isString()
                         && defaultValue.asString() == jsonString( option, "value" ).toStdString() )
                        defaultIndex = index;
                }
            }
            combo->setCurrentIndex( defaultIndex );
            field = combo;
            binding.readValue = [combo]() {
                return Json::Value( combo->currentData().toString().toStdString() );
            };
            binding.applyValue = [combo]( const Json::Value &value ) {
                // A state value of the wrong type must be ignored, never
                // throw asString() from the queued GUI lambda (issue #1039).
                if ( !value.isString() )
                    return;
                const int index = combo->findData( QString::fromStdString( value.asString() ) );
                if ( index >= 0 )
                {
                    QSignalBlocker blocker( combo );
                    combo->setCurrentIndex( index );
                }
            };
        }
        else if ( type == "button" )
        {
            auto *button = new QPushButton( label, parent );
            field = button;
            binding.readValue = []() { return Json::Value(); };
            binding.applyValue = []( const Json::Value & ) {};
            // Clicked events carry no value.
            connect( button, &QPushButton::clicked, this, [this, binding, pluginId]() {
                Json::Value event( Json::objectValue );
                event["contributionId"] = binding.contributionId.toStdString();
                event["controlId"] = binding.controlId.toStdString();
                event["eventType"] = "clicked";
                enqueueEvent( pluginId, event );
            } );
        }

        if ( !field )
            continue;
        layout->addRow( label, field );
        binding.widget = field;

        // Value-bearing controls report "changed" events.
        if ( type == "text" )
        {
            if ( auto *edit = qobject_cast<QLineEdit *>( field ) )
                connect( edit, &QLineEdit::editingFinished, this, [this, binding, pluginId]() {
                    Json::Value event( Json::objectValue );
                    event["contributionId"] = binding.contributionId.toStdString();
                    event["controlId"] = binding.controlId.toStdString();
                    event["eventType"] = "changed";
                    event["value"] = binding.readValue();
                    enqueueEvent( pluginId, event );
                } );
            else if ( auto *edit = qobject_cast<QPlainTextEdit *>( field ) )
                connect( edit, &QPlainTextEdit::textChanged, this, [this, binding, pluginId]() {
                    Json::Value event( Json::objectValue );
                    event["contributionId"] = binding.contributionId.toStdString();
                    event["controlId"] = binding.controlId.toStdString();
                    event["eventType"] = "changed";
                    event["value"] = binding.readValue();
                    enqueueEvent( pluginId, event );
                } );
        }
        else if ( type == "number" )
        {
            auto *spin = qobject_cast<QDoubleSpinBox *>( field );
            connect( spin, &QDoubleSpinBox::valueChanged, this, [this, binding, pluginId]( double ) {
                Json::Value event( Json::objectValue );
                event["contributionId"] = binding.contributionId.toStdString();
                event["controlId"] = binding.controlId.toStdString();
                event["eventType"] = "changed";
                event["value"] = binding.readValue();
                enqueueEvent( pluginId, event );
            } );
        }
        else if ( type == "slider" )
        {
            auto *slider = qobject_cast<QSlider *>( field );
            connect( slider, &QSlider::valueChanged, this, [this, binding, pluginId]( int ) {
                Json::Value event( Json::objectValue );
                event["contributionId"] = binding.contributionId.toStdString();
                event["controlId"] = binding.controlId.toStdString();
                event["eventType"] = "changed";
                event["value"] = binding.readValue();
                enqueueEvent( pluginId, event );
            } );
        }
        else if ( type == "checkbox" )
        {
            auto *check = qobject_cast<QCheckBox *>( field );
            connect( check, &QCheckBox::toggled, this, [this, binding, pluginId]( bool ) {
                Json::Value event( Json::objectValue );
                event["contributionId"] = binding.contributionId.toStdString();
                event["controlId"] = binding.controlId.toStdString();
                event["eventType"] = "changed";
                event["value"] = binding.readValue();
                enqueueEvent( pluginId, event );
            } );
        }
        else if ( type == "combo" )
        {
            auto *combo = qobject_cast<QComboBox *>( field );
            connect( combo, &QComboBox::currentIndexChanged, this, [this, binding, pluginId]( int ) {
                Json::Value event( Json::objectValue );
                event["contributionId"] = binding.contributionId.toStdString();
                event["controlId"] = binding.controlId.toStdString();
                event["eventType"] = "changed";
                event["value"] = binding.readValue();
                enqueueEvent( pluginId, event );
            } );
        }

        record.bindings.push_back( std::move( binding ) );
    }
}

bool PluginUiSchemaRenderer::attachPluginSchema( const QString &pluginId, const Json::Value &schema,
                                                 std::unique_ptr<UiInvokeDelegate> delegate,
                                                 QString &error )
{
    if ( !mShellSink )
    {
        error = "no shell sink installed; declarative UI would never appear";
        return false;
    }
    if ( QThread::currentThread() != thread() )
    {
        // Widgets and queued applyState lambdas are bound to the thread
        // that created the renderer singleton (the GUI thread): attaching
        // from elsewhere would mutate widgets off-GUI.
        error = "attachPluginSchema must be called on the renderer's thread";
        return false;
    }
    // Host-side re-validation (issue #1039): the schema arrives from the
    // untrusted worker, so the host does not trust the worker's own
    // validation. A hostile or buggy schema must fail typed HERE, never
    // throw or flood the GUI thread inside buildControls.
    exprs::PluginUiSchemaParseResult validated;
    try
    {
        validated = exprs::validatePluginUiSchema( schema );
    }
    catch ( const std::exception &exception )
    {
        error = QStringLiteral( "schema validation threw: %1" )
                    .arg( QString::fromUtf8( exception.what() ) );
        return false;
    }
    if ( !validated.ok() )
    {
        error = QStringLiteral( "schema failed host-side validation: %1" )
                    .arg( QString::fromStdString( validated.errors.front() ) );
        return false;
    }
    // Build from the NORMALIZED copy so no unvalidated subtree reaches the
    // renderer even if validation were ever loosened.
    const Json::Value &normalized = validated.normalized;
    // Re-attachment replaces (reload path): release first.
    releasePluginUi( pluginId );

    auto record = std::make_shared<RenderedRecord>();
    record->pluginId = pluginId;
    record->schema = normalized;
    record->delegate = std::move( delegate );

    // Commands become host-owned actions; menuItems attach them.
    struct CommandAction
    {
        QString commandId;
        QAction *action = nullptr;
    };
    std::vector<CommandAction> commandActions;
    const Json::Value &commands = normalized.get( "commands", Json::Value( Json::nullValue ) );
    if ( commands.isArray() )
    {
        for ( const Json::Value &command : commands )
        {
            auto *action = new QAction( jsonString( command, "title" ), this );
            const QString commandId = jsonString( command, "id" );
            connect( action, &QAction::triggered, this, [this, pluginId, commandId]() {
                Json::Value event( Json::objectValue );
                event["contributionId"] = commandId.toStdString();
                event["controlId"] = commandId.toStdString();
                event["eventType"] = "command";
                enqueueEvent( pluginId, event );
            } );
            commandActions.push_back( { commandId, action } );
            record->menuActions.append( action );
        }
    }

    // Settings pages and dock panels are host-owned widgets.
    size_t renderedControls = 0; // shared render bound across every surface
    for ( const char *surface : { "settingsPages", "dockPanels" } )
    {
        const Json::Value &pages = normalized.get( surface, Json::Value( Json::nullValue ) );
        if ( !pages.isArray() )
            continue;
        for ( const Json::Value &page : pages )
        {
            auto *widget = new QWidget();
            buildControls( widget, page.get( "controls", Json::Value() ), jsonString( page, "id" ),
                           pluginId, *record, 1, renderedControls );
            record->surfaces.append( { jsonString( page, "id" ), widget,
                                       jsonString( page, "title" ) } );
        }
    }

    if ( record->surfaces.isEmpty() && record->menuActions.isEmpty() )
    {
        error = "schema declares no attachable surface";
        return false;
    }

    // Attach through the reverse-ownership shell sink; surface kinds come
    // from the schema (settings vs dock), not from widget guesses.
    const Json::Value &settingsPages = normalized.get( "settingsPages", Json::Value( Json::nullValue ) );
    if ( settingsPages.isArray() )
    {
        for ( const Json::Value &page : settingsPages )
        {
            const QString id = jsonString( page, "id" );
            for ( const RenderedRecord::Surface &surface : record->surfaces )
                if ( surface.contributionId == id )
                {
                    mShellSink->attachSettingsPage( pluginId, surface.title, surface.widget );
                    break;
                }
        }
    }
    const Json::Value &dockPanels = normalized.get( "dockPanels", Json::Value( Json::nullValue ) );
    if ( dockPanels.isArray() )
    {
        for ( const Json::Value &panel : dockPanels )
        {
            const QString id = jsonString( panel, "id" );
            for ( const RenderedRecord::Surface &surface : record->surfaces )
                if ( surface.contributionId == id )
                {
                    mShellSink->attachDock( pluginId, surface.title, surface.widget );
                    break;
                }
        }
    }
    if ( !record->menuActions.isEmpty() )
        mShellSink->attachMenuActions( pluginId, record->menuActions );

    mRecords.push_back( std::move( record ) );
    emit contributionChanged();
    return true;
}

void PluginUiSchemaRenderer::releasePluginUi( const QString &pluginId )
{
    bool changed = false;
    for ( auto iterator = mRecords.begin(); iterator != mRecords.end(); )
    {
        if ( ( *iterator )->pluginId != pluginId )
        {
            ++iterator;
            continue;
        }
        // Block further deliveries first (events already in flight hold a
        // shared_ptr; the cleared delegate makes their late response a
        // no-op). The shell sink runs OUTSIDE the event mutex: it may
        // re-enter the renderer, and the mutex is non-recursive.
        {
            std::lock_guard<std::mutex> lock( mEventMutex );
            ( *iterator )->delegate.reset();
        }
        if ( mShellSink )
            mShellSink->releaseUi( pluginId );
        iterator = mRecords.erase( iterator );
        changed = true;
    }
    if ( changed )
        emit contributionChanged();
}

bool PluginUiSchemaRenderer::hasPluginUi( const QString &pluginId ) const
{
    for ( const auto &record : mRecords )
        if ( record->pluginId == pluginId )
            return true;
    return false;
}

void PluginUiSchemaRenderer::applyState( RenderedRecord &record, const Json::Value &state )
{
    if ( !state.isObject() )
        return;
    for ( const auto &controlId : state.getMemberNames() )
    {
        for ( ControlBinding &binding : record.bindings )
            if ( binding.controlId == QString::fromStdString( controlId ) )
            {
                binding.applyValue( state[ controlId ] );
                break;
            }
    }
    emit eventApplied( record.pluginId,
                       record.bindings.empty() ? QString() : record.bindings.front().contributionId );
}

} // namespace sicnu::plugins
