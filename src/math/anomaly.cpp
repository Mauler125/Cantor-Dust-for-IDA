#include "math/anomaly.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cantordust
{

namespace
{
constexpr f64 kMinimumCoverage = 0.95;
}

void AnomalyDetector::Break()
{
    m_hasPrevious = false;
}

void AnomalyDetector::Record( const AnomalyFinding_s& finding )
{
    // Coalesce adjacent windows of the same kind, even when another kind
    // was also reported in between. Keeps long padding/encrypted runs usable.
    for ( auto it = m_findings.rbegin(); it != m_findings.rend(); ++it )
    {
        if ( it->kind != finding.kind )
            continue;
        if ( it->offset + it->length == finding.offset &&
             ( it->kind != AnomalyKind_e::Padding || it->value == finding.value ) &&
             ( it->kind != AnomalyKind_e::RepeatedPattern ||
               it->value == finding.value ) )
        {
            it->length += finding.length;
            it->strength = std::max( it->strength, finding.strength );
            it->entropy = ( it->entropy + finding.entropy ) * 0.5;
            return;
        }
        // The latest finding of this kind wasn't adjacent. Earlier ones
        // can't be adjacent either, since input windows are monotone.
        break;
    }
    m_findings.push_back( finding );
}

void AnomalyDetector::Analyze(
    const std::vector< ByteCell_s >& cells, const u64 logicalOffset )
{
    if ( cells.size() < 256 )
    {
        Break();
        return;
    }

    std::array< usize, 256 > counts{};
    usize valid = 0;
    for ( const ByteCell_s& cell : cells )
    {
        if ( cell.valid )
        {
            ++valid;
            ++counts[ cell.value ];
        }
    }
    if ( static_cast< f64 >( valid ) / cells.size() < kMinimumCoverage )
    {
        Break();
        return;
    }

    f64 entropy = 0.0;
    usize maximum = 0;
    u8 dominant = 0;
    for ( usize i = 0; i < counts.size(); ++i )
    {
        const usize count = counts[ i ];
        if ( count > maximum )
        {
            maximum = count;
            dominant = static_cast< u8 >( i );
        }
        if ( count != 0 )
        {
            const f64 probability = static_cast< f64 >( count ) / valid;
            entropy -= probability * std::log2( probability );
        }
    }

    const auto add = [&]( const AnomalyKind_e kind,
        const f64 strength, const u8 value = 0 ) {
        Record({ logicalOffset, static_cast< u64 >( cells.size() ),
            kind, entropy, std::clamp( strength, 0.0, 1.0 ), value });
    };

    if ( m_options.highEntropy && entropy >= m_options.highEntropyBits )
        add( AnomalyKind_e::HighEntropy, ( entropy - m_options.highEntropyBits ) /
            std::max( 8.0 - m_options.highEntropyBits, 0.001 ) );

    if ( m_options.entropyChange && m_hasPrevious &&
         m_previousEnd == logicalOffset &&
         std::abs( entropy - m_previousEntropy ) >= m_options.entropyChangeBits )
    {
        add( AnomalyKind_e::EntropyChange,
            std::abs( entropy - m_previousEntropy ) / 8.0 );
    }

    const f64 dominantFraction = static_cast< f64 >( maximum ) / valid;
    if ( m_options.padding && ( dominant == 0x00 || dominant == 0xff ) &&
         dominantFraction >= m_options.paddingFraction )
        add( AnomalyKind_e::Padding, dominantFraction, dominant );
    else if ( m_options.dominantByte &&
              dominantFraction >= m_options.dominantFraction )
        add( AnomalyKind_e::DominantByte, dominantFraction, dominant );

    // Periodicity with a nonconstant motif (constant padding is handled
    // above). A single invalid cell must never be treated as a match.
    if ( m_options.repeatedPattern && dominantFraction <= 0.50 )
    {
        for ( const usize period : { usize( 2 ), usize( 4 ),
                usize( 8 ), usize( 16 ) } )
        {
            usize comparisons = 0;
            usize matches = 0;
            for ( usize i = period; i < cells.size(); ++i )
            {
                if ( !cells[ i ].valid || !cells[ i - period ].valid )
                    continue;
                ++comparisons;
                matches += cells[ i ].value == cells[ i - period ].value;
            }
            if ( comparisons >= 256 &&
                 static_cast< f64 >( matches ) / comparisons >= m_options.patternFraction )
            {
                add( AnomalyKind_e::RepeatedPattern,
                    static_cast< f64 >( matches ) / comparisons,
                    static_cast< u8 >( period ) );
                break;
            }
        }
    }

    m_previousEntropy = entropy;
    m_previousEnd = logicalOffset + cells.size();
    m_hasPrevious = true;
}

const char* Anomaly_KindName( const AnomalyKind_e kind )
{
    switch ( kind )
    {
        case AnomalyKind_e::HighEntropy: return "High entropy";
        case AnomalyKind_e::EntropyChange: return "Entropy change";
        case AnomalyKind_e::Padding: return "Padding (00/FF)";
        case AnomalyKind_e::DominantByte: return "Dominant byte";
        case AnomalyKind_e::RepeatedPattern: return "Repeated pattern";
    }
    return "Unknown";
}

} // namespace cantordust
