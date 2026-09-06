#include "SleeveAnalyzer.h"

#include "SleeveGeometry.h"

#include <QPair>
#include <QtMath>
#include <algorithm>
#include <limits>

namespace {

using namespace SleeveGeometry;

struct SampledOutline
{
    QVector<QPointF> points;
    QVector<int> sourceSegment;
    QVector<double> sourceVertexDistance;
    double perimeter = 0.0;
};

struct CornerCandidate
{
    CornerCandidate() = default;
    CornerCandidate(int sourceVertex, double atDistance, double turning)
        : vertex(sourceVertex), distanceAlong(atDistance), turn(turning) {}

    int vertex = -1;
    double distanceAlong = 0.0;
    double turn = 0.0;
};

struct BoundaryPath
{
    int startVertex = -1;
    int endVertex = -1;
    QVector<QPointF> sampledPoints;
    double exactLength = 0.0;
    double chord = 0.0;
    double maximumDeviation = 0.0;
    double curvatureScore = 0.0;
    int segmentCount = 0;
};

double segmentLength(const SleeveVertex& start, const SleeveVertex& end)
{
    const double chord = distance(start.point, end.point);
    if (qAbs(start.bulge) <= kEpsilon)
        return chord;
    const double includedAngle = 4.0 * qAtan(start.bulge);
    const double sine = qSin(qAbs(includedAngle) * 0.5);
    return sine > kEpsilon ? chord * qAbs(includedAngle) / (2.0 * sine) : chord;
}

QVector<QPointF> sampleSegment(const SleeveVertex& start,
                               const SleeveVertex& end)
{
    QVector<QPointF> result;
    result.append(start.point);
    if (qAbs(start.bulge) <= kEpsilon) {
        result.append(end.point);
        return result;
    }

    const QPointF chordVector = end.point - start.point;
    const double chord = length(chordVector);
    const double angle = 4.0 * qAtan(start.bulge);
    const double sine = qSin(qAbs(angle) * 0.5);
    if (chord <= kEpsilon || sine <= kEpsilon) {
        result.append(end.point);
        return result;
    }

    const double radius = chord / (2.0 * sine);
    const QPointF midpoint = (start.point + end.point) * 0.5;
    const QPointF normal = normalized(perpendicularLeft(chordVector));
    const double centerOffset = chord / (2.0 * qTan(angle * 0.5));
    const QPointF center = midpoint + normal * centerOffset;
    const double startAngle = qAtan2(start.point.y() - center.y(),
                                     start.point.x() - center.x());
    const int divisions = qMax(4, qMin(180,
        static_cast<int>(qCeil(qAbs(angle) / qDegreesToRadians(3.0)))));
    for (int i = 1; i < divisions; ++i) {
        const double current = startAngle + angle * i / divisions;
        result.append(center + QPointF(qCos(current), qSin(current)) * radius);
    }
    result.append(end.point);
    return result;
}

SampledOutline buildSampledOutline(const QVector<SleeveVertex>& vertices)
{
    SampledOutline result;
    const int count = vertices.size();
    result.sourceVertexDistance.resize(count);
    double distanceAlong = 0.0;

    for (int i = 0; i < count; ++i) {
        result.sourceVertexDistance[i] = distanceAlong;
        const int next = (i + 1) % count;
        const QVector<QPointF> segment = sampleSegment(vertices.at(i), vertices.at(next));
        if (result.points.isEmpty())
            result.points.append(segment.first());
        for (int j = 1; j < segment.size(); ++j) {
            distanceAlong += distance(result.points.last(), segment.at(j));
            result.points.append(segment.at(j));
            result.sourceSegment.append(i);
        }
    }
    result.perimeter = distanceAlong;
    return result;
}

QPointF pointAtDistance(const SampledOutline& outline, double wanted)
{
    if (outline.points.isEmpty() || outline.perimeter <= kEpsilon)
        return QPointF();
    wanted = std::fmod(wanted, outline.perimeter);
    if (wanted < 0.0)
        wanted += outline.perimeter;

    double accumulated = 0.0;
    for (int i = 1; i < outline.points.size(); ++i) {
        const double part = distance(outline.points.at(i - 1), outline.points.at(i));
        if (accumulated + part >= wanted && part > kEpsilon) {
            const double ratio = (wanted - accumulated) / part;
            return outline.points.at(i - 1)
                   + ratio * (outline.points.at(i) - outline.points.at(i - 1));
        }
        accumulated += part;
    }
    return outline.points.first();
}

double cyclicDistance(double first, double second, double perimeter)
{
    const double direct = qAbs(first - second);
    return qMin(direct, perimeter - direct);
}

QVector<int> detectMajorCorners(const QVector<SleeveVertex>& vertices,
                                const SampledOutline& outline)
{
    QVector<CornerCandidate> candidates;
    const double window = outline.perimeter * 0.045;
    for (int i = 0; i < vertices.size(); ++i) {
        const double at = outline.sourceVertexDistance.at(i);
        const QPointF before = pointAtDistance(outline, at - window);
        const QPointF after = pointAtDistance(outline, at + window);
        const QPointF center = vertices.at(i).point;
        const double turn = M_PI - angleBetween(before - center, after - center);
        candidates.append({i, at, qAbs(turn)});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const CornerCandidate& a, const CornerCandidate& b) {
                  return a.turn > b.turn;
              });

    QVector<CornerCandidate> selected;
    const double minimumSpacing = outline.perimeter * 0.08;
    for (const CornerCandidate& candidate : candidates) {
        if (candidate.turn < qDegreesToRadians(22.0))
            break;
        bool separated = true;
        for (const CornerCandidate& existing : selected) {
            if (cyclicDistance(candidate.distanceAlong,
                               existing.distanceAlong,
                               outline.perimeter) < minimumSpacing) {
                separated = false;
                break;
            }
        }
        if (separated)
            selected.append(candidate);
        if (selected.size() == 4)
            break;
    }

    QVector<int> indices;
    for (const CornerCandidate& candidate : selected)
        indices.append(candidate.vertex);
    std::sort(indices.begin(), indices.end());
    return indices;
}

BoundaryPath makeBoundaryPath(const QVector<SleeveVertex>& vertices,
                              int startVertex,
                              int endVertex)
{
    BoundaryPath path;
    path.startVertex = startVertex;
    path.endVertex = endVertex;
    path.sampledPoints.append(vertices.at(startVertex).point);

    int current = startVertex;
    while (current != endVertex) {
        const int next = (current + 1) % vertices.size();
        const QVector<QPointF> samples = sampleSegment(vertices.at(current), vertices.at(next));
        for (int j = 1; j < samples.size(); ++j)
            path.sampledPoints.append(samples.at(j));
        path.exactLength += segmentLength(vertices.at(current), vertices.at(next));
        ++path.segmentCount;
        current = next;
        if (path.segmentCount > vertices.size())
            break;
    }

    const QPointF first = vertices.at(startVertex).point;
    const QPointF last = vertices.at(endVertex).point;
    path.chord = distance(first, last);
    for (const QPointF& point : path.sampledPoints)
        path.maximumDeviation = qMax(path.maximumDeviation,
                                     pointLineDistance(point, first, last));
    if (path.chord > kEpsilon) {
        const double excess = path.exactLength / path.chord - 1.0;
        path.curvatureScore = qMax(0.0, excess)
                              + path.maximumDeviation / path.chord;
    } else {
        path.curvatureScore = std::numeric_limits<double>::infinity();
    }
    return path;
}

QPointF deterministicTransverse(const QPointF& axis)
{
    QPointF transverse = perpendicularLeft(axis);
    if (transverse.y() < -kEpsilon
        || (qAbs(transverse.y()) <= kEpsilon && transverse.x() < 0.0)) {
        transverse = -transverse;
    }
    return transverse;
}

QString orientationName(const QPointF& axis)
{
    double degrees = qRadiansToDegrees(qAtan2(axis.y(), axis.x()));
    if (degrees < 0.0)
        degrees += 360.0;
    QString direction;
    if (degrees < 22.5 || degrees >= 337.5)
        direction = QStringLiteral("right");
    else if (degrees < 67.5)
        direction = QStringLiteral("up-right");
    else if (degrees < 112.5)
        direction = QStringLiteral("up");
    else if (degrees < 157.5)
        direction = QStringLiteral("up-left");
    else if (degrees < 202.5)
        direction = QStringLiteral("left");
    else if (degrees < 247.5)
        direction = QStringLiteral("down-left");
    else if (degrees < 292.5)
        direction = QStringLiteral("down");
    else
        direction = QStringLiteral("down-right");
    return QStringLiteral("cap-to-cuff %1 (%2 deg)")
        .arg(direction).arg(degrees, 0, 'f', 2);
}

} // namespace

SleeveAnalyzer::SleeveAnalyzer(SleeveDrawingUnit drawingUnit)
    : m_drawingUnit(drawingUnit)
{
}

SleeveAnalysis SleeveAnalyzer::analyze(const QVector<SleeveVertex>& input,
                                       bool closed) const
{
    SleeveAnalysis result;
    result.drawingUnit = m_drawingUnit;
    const bool repeatedClosure = input.size() > 1
        && distance(input.first().point, input.last().point) <= 1.0e-7;
    if (!closed && !repeatedClosure) {
        result.failureReason = QStringLiteral("Selected polyline is not closed.");
        return result;
    }

    QVector<SleeveVertex> vertices = input;
    if (repeatedClosure) {
        vertices.removeLast();
    }
    if (vertices.size() < 8) {
        result.failureReason = QStringLiteral("Sleeve outline has too few vertices.");
        return result;
    }
    if (m_drawingUnit == SleeveDrawingUnit::Unknown) {
        result.failureReason = QStringLiteral("Drawing unit is unknown; millimetre output is unsafe.");
        return result;
    }

    const SampledOutline sampled = buildSampledOutline(vertices);
    result.sampledOutline = sampled.points;
    if (sampled.perimeter <= kEpsilon) {
        result.failureReason = QStringLiteral("Sleeve outline has zero perimeter.");
        return result;
    }

    const QVector<int> corners = detectMajorCorners(vertices, sampled);
    if (corners.size() != 4) {
        result.failureReason = QStringLiteral("Ambiguous sleeve orientation: four major corners were not found.");
        return result;
    }

    QVector<BoundaryPath> paths;
    for (int i = 0; i < corners.size(); ++i)
        paths.append(makeBoundaryPath(vertices, corners.at(i),
                                      corners.at((i + 1) % corners.size())));

    int capIndex = 0;
    for (int i = 1; i < paths.size(); ++i) {
        if (paths.at(i).curvatureScore > paths.at(capIndex).curvatureScore)
            capIndex = i;
    }
    const int cuffIndex = (capIndex + 2) % 4;
    const BoundaryPath& cap = paths.at(capIndex);
    const BoundaryPath& cuff = paths.at(cuffIndex);

    double secondHighest = 0.0;
    for (int i = 0; i < paths.size(); ++i) {
        if (i != capIndex)
            secondHighest = qMax(secondHighest, paths.at(i).curvatureScore);
    }
    if (!qIsFinite(cap.curvatureScore)
        || cap.curvatureScore < 0.035
        || cap.curvatureScore < secondHighest * 1.35) {
        result.failureReason = QStringLiteral("Unable to detect sleeve cap reliably.");
        return result;
    }
    if (cuff.chord <= kEpsilon
        || cuff.exactLength / cuff.chord > 1.12
        || cuff.maximumDeviation / cuff.chord > 0.10) {
        result.failureReason = QStringLiteral("Unable to detect cuff reliably.");
        return result;
    }

    const QPointF capStart = vertices.at(cap.startVertex).point;
    const QPointF capEnd = vertices.at(cap.endVertex).point;
    const QPointF cuffStart = vertices.at(cuff.startVertex).point;
    const QPointF cuffEnd = vertices.at(cuff.endVertex).point;
    const QPointF capBaselineCenter = (capStart + capEnd) * 0.5;
    result.cuffCenter = (cuffStart + cuffEnd) * 0.5;
    const QPointF towardCap = normalized(capBaselineCenter - result.cuffCenter);
    if (length(towardCap) <= kEpsilon) {
        result.failureReason = QStringLiteral("Ambiguous sleeve orientation.");
        return result;
    }

    bool foundIntersection = false;
    double bestParameter = -std::numeric_limits<double>::infinity();
    QPointF capIntersection;
    for (int i = 1; i < cap.sampledPoints.size(); ++i) {
        QPointF candidate;
        double parameter = 0.0;
        if (infiniteLineSegmentIntersection(result.cuffCenter,
                                            towardCap,
                                            cap.sampledPoints.at(i - 1),
                                            cap.sampledPoints.at(i),
                                            &candidate,
                                            &parameter)
            && parameter > bestParameter) {
            bestParameter = parameter;
            capIntersection = candidate;
            foundIntersection = true;
        }
    }
    if (!foundIntersection || bestParameter <= 0.0) {
        result.failureReason = QStringLiteral("Sleeve axis does not intersect the sleeve cap.");
        return result;
    }

    result.point3 = capIntersection;
    result.sleeveAxis = normalized(result.cuffCenter - result.point3);
    const QPointF transverse = deterministicTransverse(result.sleeveAxis);
    if (dot(capStart - capBaselineCenter, transverse)
        >= dot(capEnd - capBaselineCenter, transverse)) {
        result.point1 = capStart;
        result.point2 = capEnd;
    } else {
        result.point1 = capEnd;
        result.point2 = capStart;
    }
    if (dot(cuffStart - result.cuffCenter, transverse)
        >= dot(cuffEnd - result.cuffCenter, transverse)) {
        result.cuffUpper = cuffStart;
        result.cuffLower = cuffEnd;
    } else {
        result.cuffUpper = cuffEnd;
        result.cuffLower = cuffStart;
    }

    result.sleeveLengthMM = drawingUnitToMM(
        dot(result.cuffCenter - result.point3, result.sleeveAxis));
    result.cuffWidthMM = drawingUnitToMM(cuff.chord);
    result.sleeveCapLengthMM = drawingUnitToMM(cap.exactLength);
    QVector<SleeveVertex> capVertices;
    int capVertex = cap.startVertex;
    while (true) {
        capVertices.append(vertices.at(capVertex));
        if (capVertex == cap.endVertex)
            break;
        capVertex = (capVertex + 1) % vertices.size();
    }
    result.sleeveCapMinimumAxialMM = drawingUnitToMM(
        minimumAxialProjection(capVertices, result.point3, result.sleeveAxis));
    result.sleeveCapSegmentCount = cap.segmentCount;
    result.sampledSleeveCap = cap.sampledPoints;
    result.sampledCuff = cuff.sampledPoints;
    result.orientation = orientationName(result.sleeveAxis);
    result.cuffDirection = normalized(result.cuffUpper - result.cuffLower);
    result.valid = true;
    return result;
}

double SleeveAnalyzer::mmToDrawingUnit(double mm) const
{
    return m_drawingUnit == SleeveDrawingUnit::Inch ? mm / 25.4 : mm;
}

double SleeveAnalyzer::drawingUnitToMM(double value) const
{
    return m_drawingUnit == SleeveDrawingUnit::Inch ? value * 25.4 : value;
}

QString SleeveAnalyzer::drawingUnitName(SleeveDrawingUnit unit)
{
    switch (unit) {
    case SleeveDrawingUnit::Inch:
        return QStringLiteral("inch");
    case SleeveDrawingUnit::Millimeter:
        return QStringLiteral("mm");
    default:
        return QStringLiteral("unknown");
    }
}
