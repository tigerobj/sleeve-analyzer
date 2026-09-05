#include "SleeveSideGeometry.h"

#include "SleeveGeometry.h"

#include <QtGlobal>
#include <limits>

namespace {

using namespace SleeveGeometry;

bool startTangent(const SleeveAnalysis& analysis,
                  const QPointF& start,
                  QPointF* tangent)
{
    if (!tangent || analysis.sampledOutline.size() < 4
        || analysis.sampledSleeveCap.size() < 2) {
        return false;
    }

    int outlineCount = analysis.sampledOutline.size();
    if (distance(analysis.sampledOutline.first(),
                 analysis.sampledOutline.last()) <= kEpsilon) {
        --outlineCount;
    }
    if (outlineCount < 3)
        return false;

    int startIndex = -1;
    double closestDistance = std::numeric_limits<double>::max();
    for (int i = 0; i < outlineCount; ++i) {
        const double candidateDistance = distance(start,
                                                   analysis.sampledOutline.at(i));
        if (candidateDistance < closestDistance) {
            closestDistance = candidateDistance;
            startIndex = i;
        }
    }
    if (startIndex < 0 || closestDistance > 1.0e-7)
        return false;

    QPointF capDirection;
    if (distance(start, analysis.sampledSleeveCap.first()) <= 1.0e-7) {
        capDirection = normalized(analysis.sampledSleeveCap.at(1) - start);
    } else if (distance(start, analysis.sampledSleeveCap.last()) <= 1.0e-7) {
        capDirection = normalized(
            analysis.sampledSleeveCap.at(analysis.sampledSleeveCap.size() - 2)
            - start);
    } else {
        return false;
    }

    const QPointF previous = analysis.sampledOutline.at(
        (startIndex + outlineCount - 1) % outlineCount);
    const QPointF next = analysis.sampledOutline.at((startIndex + 1) % outlineCount);
    const QPointF previousDirection = normalized(previous - start);
    const QPointF nextDirection = normalized(next - start);

    // Of the two outline neighbours, the one least aligned with the known cap
    // direction is the original short-sleeve underarm side.
    const QPointF sideDirection = dot(previousDirection, capDirection)
                                      < dot(nextDirection, capDirection)
                                  ? previousDirection
                                  : nextDirection;
    if (length(sideDirection) <= kEpsilon)
        return false;
    *tangent = sideDirection;
    return true;
}

QVector<QPointF> buildSide(const QPointF& start,
                           const QPointF& end,
                           const QPointF& startDirection,
                           const QPointF& axis,
                           const QPointF& transverse,
                           int segmentCount,
                           double curveStrength)
{
    const QPointF displacement = end - start;
    const double axialDistance = dot(displacement, axis);
    const double startAxial = dot(startDirection, axis);
    if (axialDistance <= kEpsilon || startAxial <= kEpsilon)
        return QVector<QPointF>();

    // The generated sleeve sides are contractual straight segments: keep the
    // exact P1/P2 and target-cuff endpoint coordinates and do not interpolate
    // additional vertices between them.  The remaining arguments are kept in
    // this helper's interface so the validation/orchestration path is unchanged.
    Q_UNUSED(transverse);
    Q_UNUSED(segmentCount);
    Q_UNUSED(curveStrength);

    QVector<QPointF> points;
    points.reserve(2);
    points.append(start);
    points.append(end);
    return points;
}

} // namespace

SleeveSidePreview SleeveSideGeometry::calculate(
    const SleeveAnalysis& analysis,
    const TargetCuffGeometry& targetCuff,
    int segmentCount,
    double curveStrength)
{
    SleeveSidePreview result;
    if (!analysis.valid) {
        result.failureReason = QStringLiteral("Sleeve analysis is not valid.");
        return result;
    }
    if (!targetCuff.valid || !targetCuff.validCuff) {
        result.failureReason = QStringLiteral("Target cuff is not valid.");
        return result;
    }
    if (segmentCount < 2) {
        result.failureReason = QStringLiteral("At least two side segments are required.");
        return result;
    }

    QPointF axis = normalized(analysis.sleeveAxis);
    if (dot(targetCuff.center - analysis.point3, axis) < 0.0)
        axis = -axis;
    const QPointF transverse = normalized(targetCuff.upper - targetCuff.lower);
    if (length(axis) <= kEpsilon || length(transverse) <= kEpsilon) {
        result.failureReason = QStringLiteral("Sleeve axis or cuff direction is invalid.");
        return result;
    }

    if (!startTangent(analysis, analysis.point1, &result.upperStartTangent)
        || !startTangent(analysis, analysis.point2, &result.lowerStartTangent)) {
        result.failureReason = QStringLiteral(
            "Original short-sleeve side tangents could not be determined.");
        return result;
    }

    result.upperPoints = buildSide(analysis.point1, targetCuff.upper,
                                   result.upperStartTangent, axis, transverse,
                                   segmentCount, curveStrength);
    result.lowerPoints = buildSide(analysis.point2, targetCuff.lower,
                                   result.lowerStartTangent, axis, transverse,
                                   segmentCount, curveStrength);
    if (result.upperPoints.size() != 2
        || result.lowerPoints.size() != 2) {
        result.upperPoints.clear();
        result.lowerPoints.clear();
        result.failureReason = QStringLiteral(
            "Target cuff is not forward of the sleeve-cap endpoints.");
        return result;
    }

    result.valid = true;
    return result;
}
