#include "ida/cantor_panel.h"

#include <QtWidgets>
#include <QPointer>
#include <QIcon>

#include <ida.hpp>
#include <idp.hpp>
#include <kernwin.hpp>
#include <loader.hpp>

namespace
{

static const char s_widgetTitle[] = "Cantor Dust";

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

    CantorDustPlugin_s()
    {
        hook_event_listener( HT_UI, this );
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
