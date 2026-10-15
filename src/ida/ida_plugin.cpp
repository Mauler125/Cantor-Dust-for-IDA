#include "ida/cantor_panel.h"

#include <QtWidgets>
#include <QPointer>
#include <QIcon>

#include <vector>

#include <ida.hpp>
#include <idp.hpp>
#include <kernwin.hpp>
#include <loader.hpp>

namespace
{

static const char s_widgetTitle[] = "Cantor Dust";
static const char s_pathStartAction[] = "cantordust:set_path_start";
static const char s_pathEndAction[] = "cantordust:set_path_end";

struct CantorDustPlugin_s;

// Native IDA actions work while the disassembly has focus and appear in
// Options > Shortcuts. IDA manages user overrides and collision warnings.
struct CallPathActionHandler final : action_handler_t
{
    CantorDustPlugin_s* plugin;
    bool isStart;

    CallPathActionHandler( CantorDustPlugin_s* const owner,
        const bool start ) : plugin( owner ), isStart( start ) {}

    int idaapi activate( action_activation_ctx_t* ) override;
    action_state_t idaapi update( action_update_ctx_t* ) override
    {
        return AST_ENABLE_FOR_IDB;
    }
};

static QIcon CantorDustWidgetIcon()
{
    // Load the original vector artwork from the plugin's embedded Qt
    // resources. Qt's SVG icon engine scales it to the requested dock size.
    // Requires the host Qt installation to provide SVG icon support.
    return QIcon( QStringLiteral( ":/cantordust/cantor_dust.svg" ) );
}

struct CantorDustPlugin_s final : plugmod_t, event_listener_t
{
    TWidget* widget = nullptr;
    QPointer< cantordust::CantorPanel > panel;
    // A shortcut may open a hidden dock. Defer endpoint selection until the
    // panel receives ui_widget_visible, without losing the current IDA cursor.
    std::vector< bool > pendingPathEndpoints;
    CallPathActionHandler pathStartAction{ this, true };
    CallPathActionHandler pathEndAction{ this, false };
    bool startActionRegistered = false;
    bool endActionRegistered = false;

    CantorDustPlugin_s()
    {
        hook_event_listener( HT_UI, this );

        // Register global actions, not local QShortcuts. Their default keys
        // were checked against IDA's supplied idagui.cfg, and end users can
        // reassign or remove them in IDA's conflict-aware shortcut editor.
        const action_desc_t startDesc = ACTION_DESC_LITERAL_PLUGMOD(
            s_pathStartAction, "Cantor Dust: Set Path Start",
            &pathStartAction, this, "Ctrl+Alt+Shift+S",
            "Use the IDA cursor or selected Cantor Dust search result as path start", -1 );
        startActionRegistered = register_action( startDesc );

        const action_desc_t endDesc = ACTION_DESC_LITERAL_PLUGMOD(
            s_pathEndAction, "Cantor Dust: Set Path End",
            &pathEndAction, this, "Ctrl+Alt+Shift+E",
            "Use the IDA cursor or selected Cantor Dust search result as path end", -1 );
        endActionRegistered = register_action( endDesc );
    }

    ~CantorDustPlugin_s() override
    {
        // Handlers are members; unregister before they are destroyed.
        if ( endActionRegistered )
            unregister_action( s_pathEndAction );
        if ( startActionRegistered )
            unregister_action( s_pathStartAction );
    }

    // IDA requires the run/on_event virtual method names.
    ssize_t idaapi on_event( ssize_t code, va_list args ) override
    {
        if ( code == ui_widget_visible )
        {
            TWidget* const shown = va_arg( args, TWidget* );

            if ( shown == widget )
            {
                QWidget* const parent = reinterpret_cast< QWidget* >( widget );
                parent->setWindowIcon( CantorDustWidgetIcon() );

                QWidget* const existing = parent->findChild< QWidget* >(
                    QStringLiteral( "CantorDustContent" ) );

                if ( existing == nullptr )
                {
                    QLayout* layout = parent->layout();

                    if ( layout == nullptr )
                    {
                        QVBoxLayout* const fresh = new QVBoxLayout( parent );
                        fresh->setContentsMargins( 0, 0, 0, 0 );
                        layout = fresh;
                    }

                    cantordust::CantorPanel* const created =
                        new cantordust::CantorPanel( parent );

                    created->setObjectName( QStringLiteral( "CantorDustContent" ) );
                    created->setWindowIcon( CantorDustWidgetIcon() );
                    layout->addWidget( created );
                    panel = created;
                }
                else
                {
                    // The dock may be hidden and shown without destroying
                    // its Qt children. Resume forwarding to that same panel.
                    panel = static_cast< cantordust::CantorPanel* >( existing );
                    panel->OnIdaCursorChanged( get_screen_ea() );
                }

                if ( panel != nullptr )
                {
                    for ( const bool isStart : pendingPathEndpoints )
                        panel->SetCallPathEndpointFromAction( isStart );
                    pendingPathEndpoints.clear();
                }
            }
        }
        else if ( code == ui_screen_ea_changed )
        {
            const ea_t address = va_arg( args, ea_t );
            (void) va_arg( args, ea_t ); // previous address
            if ( panel != nullptr )
                panel->OnIdaCursorChanged( address );
        }
        else if ( code == ui_widget_invisible )
        {
            TWidget* const hidden = va_arg( args, TWidget* );

            if ( hidden == widget )
            {
                widget = nullptr;
                panel = nullptr;
            }
        }

        return 0;
    }

    bool idaapi run( size_t ) override
    {
        if ( !is_idaq() )
        {
            return false;
        }

        if ( widget == nullptr )
        {
            widget = find_widget( s_widgetTitle );
        }

        if ( widget == nullptr )
        {
            widget = create_empty_widget( s_widgetTitle );

            if ( widget == nullptr )
            {
                return false;
            }
        }

        reinterpret_cast< QWidget* >( widget )->setWindowIcon(
            CantorDustWidgetIcon() );

        display_widget( widget, WOPN_DP_TAB | WOPN_RESTORE );
        return true;
    }
};

int idaapi CallPathActionHandler::activate( action_activation_ctx_t* )
{
    // The native action also works while the disassembly has focus. If the
    // dock is closed, queue the endpoint and apply it when IDA shows the UI.
    if ( plugin->panel == nullptr )
    {
        plugin->pendingPathEndpoints.push_back( isStart );
        if ( !plugin->run( 0 ) )
        {
            plugin->pendingPathEndpoints.pop_back();
            return 0;
        }

        return 1;
    }

    plugin->panel->SetCallPathEndpointFromAction( isStart );
    return 1;
}

static plugmod_t* idaapi Plugin_Initialize()
{
    return is_idaq() ? new CantorDustPlugin_s : nullptr;
}

} // namespace

plugin_t PLUGIN = {
    IDP_INTERFACE_VERSION,
    PLUGIN_MULTI,
    Plugin_Initialize,
    nullptr,
    nullptr,
    "Cantor Dust for IDA: interactive binary visualization",
    "Explore byte rasters, byte-pair densities, histograms, and Hilbert maps.",
    "Cantor Dust",
    "Alt-C",
};
