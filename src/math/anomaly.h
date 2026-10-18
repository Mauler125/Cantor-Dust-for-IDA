#ifndef MATH_ANOMALY_H
#define MATH_ANOMALY_H

#include "cantordust/scalar_types.h"
#include "cantordust/types.h"

#include <cstddef>
#include <vector>

namespace cantordust
{

enum class AnomalyKind_e
{
    HighEntropy,
    EntropyChange,
    Padding,
    DominantByte,
    RepeatedPattern
};

// Rendering-only position with the detector category preserved so 2D views
// can select a color from their own palette (Detail and Overview may differ).
struct AnomalyMarker_s
{
    usize ordinal = 0;
    AnomalyKind_e kind = AnomalyKind_e::HighEntropy;
};

struct AnomalyFinding_s
{
    u64 offset = 0;         // Logical offset in selected IDA ranges.
    u64 length = 0;
    AnomalyKind_e kind = AnomalyKind_e::HighEntropy;
    f64 entropy = 0.0;
    f64 strength = 0.0;     // 0..1 for presentation/sorting.
    u8 value = 0;           // Padding/dominant byte, or repeat period.
};

// Thresholds and scan bounds have the same defaults as the original
// Anomaly Explorer. Percentages are stored as fractions of 1.0.
struct AnomalyOptions_s
{
    bool highEntropy = true;
    bool entropyChange = true;
    bool padding = true;
    bool dominantByte = true;
    bool repeatedPattern = true;
    f64 highEntropyBits = 7.55;
    f64 entropyChangeBits = 1.5;
    f64 paddingFraction = 0.93;
    f64 dominantFraction = 0.70;
    f64 patternFraction = 0.94;
    usize windowBytes = 4096;
    usize scanBudgetMiB = 16;
    usize maxFindingsPerKind = 100;
};

// Process fixed, *contiguous* blocks of bytes. Call Break() at range joins,
// skipped windows, or invalid data, so entropy changes never cross gaps.
class AnomalyDetector
{
public:
    explicit AnomalyDetector( const AnomalyOptions_s& options = {} )
        : m_options( options ) {}
    void Break();
    void Analyze( const std::vector< ByteCell_s >& cells, u64 logicalOffset );
    const std::vector< AnomalyFinding_s >& Findings() const { return m_findings; }

private:
    void Record( const AnomalyFinding_s& finding );
    AnomalyOptions_s m_options;
    std::vector< AnomalyFinding_s > m_findings;
    f64 m_previousEntropy = 0.0;
    u64 m_previousEnd = 0;
    bool m_hasPrevious = false;
};

const char* Anomaly_KindName( AnomalyKind_e kind );

} // namespace cantordust

#endif // MATH_ANOMALY_H
