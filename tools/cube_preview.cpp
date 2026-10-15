#include "cantordust/cube.h"
#include "cantordust/palette.h"
#include "cantordust/types.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace cantordust;

namespace
{

struct CubePreviewMode_s
{
    CubeGeometry_e geometry;
    const char* suffix;
};

bool WriteImage( const std::string& path, const Image_s& image )
{
    std::ofstream output( path, std::ios::binary );

    if ( !output )
        return false;

    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";

    for ( const std::uint32_t pixel : image.argb )
    {
        const char rgb[] = {
            static_cast< char >( ( pixel >> 16 ) & 255u ),
            static_cast< char >( ( pixel >> 8 ) & 255u ),
            static_cast< char >( pixel & 255u )
        };
        output.write( rgb, sizeof( rgb ) );
    }

    return static_cast< bool >( output );
}

bool LoadBytes( const char* const path, std::vector< ByteCell_s >& bytes )
{
    std::ifstream input( path, std::ios::binary );

    if ( !input )
        return false;

    input.seekg( 0, std::ios::end );
    const std::streamoff length = input.tellg();

    if ( length <= 0 )
        return false;

    input.seekg( 0, std::ios::beg );

    bytes.reserve( static_cast< std::size_t >( length ) );

    char value = 0;

    while ( input.get( value ) )
    {
        bytes.push_back({
            static_cast< std::uint8_t >( static_cast< unsigned char >( value ) ),
            true
        });
    }

    return !bytes.empty();
}

} // namespace

int main( const int argc, const char* const* const argv )
{
    if ( argc < 3 || argc > 6 )
    {
        std::cerr << "Usage: cantordust_cube_preview <binary-file> "
                  << "<output-prefix> [palette-index 0-15] "
                  << "[brightness-percent] [size-percent]\n";
        return 1;
    }

    int selected = 0;
    float brightnessPercent = 160.0f;
    float sizePercent = 100.0f;

    if ( argc >= 4 )
    {
        char* end = nullptr;
        const long value = std::strtol( argv[ 3 ], &end, 10 );

        if ( end == argv[ 3 ] || *end != '\0' || value < 0 ||
             static_cast< unsigned long >( value ) >= Palette_Count() )
        {
            std::cerr << "Invalid palette index\n";
            return 1;
        }

        selected = static_cast< int >( value );
    }

    if ( argc >= 5 )
    {
        char* end = nullptr;
        const float value = std::strtof( argv[ 4 ], &end );

        if ( end == argv[ 4 ] || *end != '\0' || value <= 0.0f )
        {
            std::cerr << "Invalid brightness percent\n";
            return 1;
        }

        brightnessPercent = value;
    }

    if ( argc >= 6 )
    {
        char* end = nullptr;
        const float value = std::strtof( argv[ 5 ], &end );

        if ( end == argv[ 5 ] || *end != '\0' || value <= 0.0f )
        {
            std::cerr << "Invalid size percent\n";
            return 1;
        }

        sizePercent = value;
    }

    std::vector< ByteCell_s > bytes;
    if ( !LoadBytes( argv[ 1 ], bytes ) )
    {
        std::cerr << "Cannot open input file or file is empty\n";
        return 1;
    }

    const Palette_e palette = static_cast< Palette_e >( selected );
    const CubeCloud_s cloud = Cube_BuildCloud( bytes, palette );

    if ( cloud.points.empty() )
    {
        std::cerr << "No cloud points were generated\n";
        return 1;
    }

    const CubePreviewMode_s modes[] = {
        { CCG_CUBE, "cube_xyz" },
        { CCG_SPHERE, "sphere" },
        { CCG_CYLINDER, "cylinder" },
        { CCG_TETRAHEDRON, "tetrahedron" }
    };

    CubeCamera_s camera;

    for ( const CubePreviewMode_s& mode : modes )
    {
        CubeRenderOptions_s options;
        options.geometry = mode.geometry;
        options.pointSize = sizePercent / 100.0f;
        options.brightness = brightnessPercent / 100.0f;

        const Image_s image = Cube_Render( cloud, camera,
            1024u, 1024u, palette, options );

        const std::string path =
            std::string( argv[ 2 ] ) + "_" + mode.suffix + ".ppm";

        if ( !WriteImage( path, image ) )
        {
            std::cerr << "Cannot write " << path << '\n';
            return 1;
        }

        std::cout << path << '\n';
    }

    return 0;
}
