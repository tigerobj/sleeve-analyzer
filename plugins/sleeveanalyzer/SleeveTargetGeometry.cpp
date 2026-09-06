#include "SleeveTargetGeometry.h"

#include "SleeveGeometry.h"

#include <QtGlobal>
#include <cmath>

TargetCuffGeometry SleeveTargetGeometry::calculate(
    const SleeveAnalysis& analysis,
    double targetSleeveLengthMM,
    double targetCuffWidthMM)
{
    TargetCuffGeometry result;
    result.targetSleeveLengthMM = targetSleeveLengthMM;
    result.targetCuffWidthMM = targetCuffWidthMM;

    if (!analysis.valid) {
        result.failureReason = QStringLiteral("Sleeve analysis is not valid.");
        return result;
    }
    if (!std::isfinite(targetSleeveLengthMM) || targetSleeveLengthMM < 0.0
        || !std::isfinite(targetCuffWidthMM)) {
        result.failureReason = QStringLiteral("Target sleeve dimensions are invalid.");
        return result;
    }

    QPointF axis = SleeveGeometry::normalized(analysis.sleeveAxis);
    if (SleeveGeometry::length(axis) <= SleeveGeometry::kEpsilon) {
        result.failureReason = QStringLiteral("Sleeve axis is invalid.");
        return result;
    }

    // Keep the Analyzer's cap-to-cuff orientation even if data from an older
    // analysis result contains an axis with the opposite sign.
    if (SleeveGeometry::dot(analysis.cuffCenter - analysis.point3, axis) < 0.0)
        axis = -axis;

    QPointF transverse = SleeveGeometry::normalized(
        SleeveGeometry::perpendicularLeft(axis));
    if (SleeveGeometry::dot(analysis.cuffUpper - analysis.cuffCenter,
                            transverse) < 0.0) {
        transverse = -transverse;
    }

    const SleeveAnalyzer converter(analysis.drawingUnit);
    const double targetLengthDrawingUnit =
        converter.mmToDrawingUnit(targetSleeveLengthMM);
    const double capMinimumAxialDrawingUnit =
        converter.mmToDrawingUnit(analysis.sleeveCapMinimumAxialMM);
    // Sleeve length is the actual-size contour span from the farthest cap
    // projection to the cuff centre.  P3 is an analysis reference point and
    // may sit inside the cap, so placing the cuff exactly targetLength from
    // P3 would make the generated outline too long by that cap overhang.
    const double cuffAxialDistance =
        targetLengthDrawingUnit + capMinimumAxialDrawingUnit;
    result.center = analysis.point3 + axis * cuffAxialDistance;
    result.measuredSleeveLengthMM = converter.drawingUnitToMM(
        cuffAxialDistance - capMinimumAxialDrawingUnit);
    result.valid = true;

    if (targetCuffWidthMM <= 0.0)
        return result;

    const double halfWidthDrawingUnit =
        converter.mmToDrawingUnit(targetCuffWidthMM) * 0.5;
    result.upper = result.center + transverse * halfWidthDrawingUnit;
    result.lower = result.center - transverse * halfWidthDrawingUnit;
    result.measuredCuffWidthMM = converter.drawingUnitToMM(
        SleeveGeometry::distance(result.upper, result.lower));
    result.validCuff = true;
    return result;
}
