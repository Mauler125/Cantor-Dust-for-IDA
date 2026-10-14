#include "cantordust/renderer.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

struct PreviewMode_s
{
    cantordust::View_e view;

    const char* name;
};

static bool Preview_WriteImage(
    const std::string& path,
    const cantordust::Image_s& image )
{
    std::ofstream output( path, std::ios::binary );

    if ( !output )
    {
        return false;
    }

    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";

    for ( const std::uint32_t pixel : image.argb )
    {
        const char rgb[] = {
            static_cast< char >( ( pixel >> 16 ) & 255 ),
            static_cast< char >( ( pixel >> 8 ) & 255 ),
            static_cast< char >( pixel & 255 )
        };
        output.write( rgb, sizeof( rgb ) );
    }

    return static_cast< bool >( output );
}

int main( const int argc, const char* const* const argv )
{
    if ( argc != 3 && argc != 4 )
    {
        std::cerr << "Usage: cantordust_preview <binary-file> "
                  << "<output-prefix> [palette-index 0-15]\n";

        return 1;
    }

    int selected = 0;

    if ( argc == 4 )
    {
        char* end = nullptr;

        const long value = std::strtol( argv[ 3 ], &end, 10 );

        if ( end == argv[ 3 ] || *end != '\0' || value < 0 ||
             static_cast< unsigned long >( value ) >=
                 cantordust::Palette_Count() )
        {
            std::cerr << "Invalid palette index\n";
            return 1;
        }

        selected = static_cast< int >( value );
    }

    std::ifstream input( argv[ 1 ], std::ios::binary );

    if ( !input )
    {
        std::cerr << "Cannot open input file\n";
        return 1;
    }

    std::vector< cantordust::ByteCell_s > bytes;
    bytes.reserve( CANTORDUST_WINDOW_SIZE );

    char value = 0;

    while ( bytes.size() < CANTORDUST_WINDOW_SIZE && input.get( value ) )
    {
        bytes.push_back({
            static_cast< std::uint8_t >( static_cast< unsigned char >( value ) ),
            true
        });
    }

    const PreviewMode_s modes[] = {
        { cantordust::CDV_LINEAR, "linear" },
        { cantordust::CDV_DIGRAPH, "digraph" },
        { cantordust::CDV_HISTOGRAM, "histogram" },
        { cantordust::CDV_HILBERT, "hilbert" }
    };

    for ( const PreviewMode_s& mode : modes )
    {
        const cantordust::Image_s image = cantordust::Render(
            mode.view, bytes, CANTORDUST_SIDE,
            static_cast< cantordust::Palette_e >( selected ) );

        const std::string path = std::string( argv[ 2 ] ) +
            "_" + mode.name + ".ppm";

        if ( !Preview_WriteImage( path, image ) )
        {
            std::cerr << "Cannot write " << path << '\n';
            return 1;
        }

        std::cout << path << '\n';
    }

    return 0;
}
