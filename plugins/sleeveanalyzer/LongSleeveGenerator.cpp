#include "LongSleeveGenerator.h"

#include "SleeveGeometry.h"
#include "document_interface.h"

#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QtMath>
#include <algorithm>
#include <limits>
#include <vector>

namespace {

using namespace SleeveGeometry;

double segmentLength(const SleeveVertex& start, const SleeveVertex& end)
{
    const double chord = distance(start.point, end.point);
    if (qAbs(start.bulge) <= kEpsilon)
        return chord;
    const double angle = 4.0 * qAtan(start.bulge);
    const double sine = qSin(qAbs(angle) * 0.5);
    return sine > kEpsilon ? chord * qAbs(angle) / (2.0 * sine) : chord;
}

QVector<QPointF> sampleBulgedSegment(const SleeveVertex& start,
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

int vertexIndex(const QVector<SleeveVertex>& vertices, const QPointF& point)
{
    int found = -1;
    double best = 1.0e-7;
    for (int i = 0; i < vertices.size(); ++i) {
        const double candidate = distance(vertices.at(i).point, point);
        if (candidate <= best) {
            best = candidate;
            found = i;
        }
    }
    return found;
}

int forwardSegmentCount(int start, int end, int vertexCount)
{
    return (end - start + vertexCount) % vertexCount;
}

void appendStraightPoint(QVector<SleeveVertex>* vertices, const QPointF& point)
{
    if (vertices->isEmpty()
        || distance(vertices->last().point, point) > 1.0e-7) {
        vertices->append(SleeveVertex(point, 0.0));
    }
}

bool samePoint(const QPointF& first, const QPointF& second)
{
    return distance(first, second) <= 1.0e-7;
}

std::vector<Plug_VertexData> plugVertices(const QVector<SleeveVertex>& vertices)
{
    std::vector<Plug_VertexData> result;
    result.reserve(static_cast<size_t>(vertices.size()));
    for (const SleeveVertex& vertex : vertices)
        result.emplace_back(vertex.point, vertex.bulge);
    return result;
}

void addPolylineGroupCompat(
    Document_Interface* document,
    const std::vector<std::vector<Plug_VertexData> >& polylines,
    const std::vector<bool>& closed)
{
    // Do not add a virtual to Document_Interface: installed LibreCAD builds
    // use the original vtable.  New native adapters expose grouping through
    // the separate extension interface; old adapters safely fall back to the
    // existing addPolyline virtual.
    Document_Interface_Extension* extension =
        dynamic_cast<Document_Interface_Extension*>(document);
    if (extension) {
        extension->addPolylineGroup(polylines, closed);
        return;
    }

    const size_t count = std::min(polylines.size(), closed.size());
    for (size_t i = 0; i < count; ++i)
        document->addPolyline(polylines.at(i), closed.at(i));
}

double polygonArea(const QPolygonF& polygon)
{
    if (polygon.size() < 3)
        return 0.0;
    double twiceArea = 0.0;
    for (int i = 0; i < polygon.size(); ++i) {
        const QPointF& first = polygon.at(i);
        const QPointF& second = polygon.at((i + 1) % polygon.size());
        twiceArea += first.x() * second.y() - second.x() * first.y();
    }
    return qAbs(twiceArea * 0.5);
}

QVector<QPointF> sampledContour(const QVector<SleeveVertex>& vertices)
{
    QVector<QPointF> result;
    if (vertices.size() < 3)
        return result;

    result.append(vertices.first().point);
    for (int i = 0; i < vertices.size(); ++i) {
        const SleeveVertex& start = vertices.at(i);
        const SleeveVertex& end = vertices.at((i + 1) % vertices.size());
        const QVector<QPointF> segment = sampleBulgedSegment(start, end);
        for (int j = 1; j < segment.size(); ++j)
            result.append(segment.at(j));
    }
    return result;
}

QVector<SleeveVertex> outwardOffset(const QVector<SleeveVertex>& contour,
                                    double offset)
{
    QVector<QPointF> points = sampledContour(contour);
    if (points.size() > 1 && samePoint(points.first(), points.last()))
        points.removeLast();
    if (points.size() < 3 || offset <= kEpsilon)
        return QVector<SleeveVertex>();

    QPainterPath path;
    path.moveTo(points.first());
    for (int i = 1; i < points.size(); ++i)
        path.lineTo(points.at(i));
    path.closeSubpath();

    // A stroker computes the Minkowski expansion and automatically resolves
    // the small concave notches present in production sleeve caps.  Taking
    // the largest simplified subpath keeps the outward boundary and drops
    // the inward side of the stroke.
    QPainterPathStroker stroker;
    stroker.setWidth(offset * 2.0);
    stroker.setJoinStyle(Qt::RoundJoin);
    stroker.setCapStyle(Qt::RoundCap);
    const QPainterPath expanded = path.united(stroker.createStroke(path))
        .simplified();
    const QList<QPolygonF> polygons = expanded.toFillPolygons();
    QPolygonF outer;
    double largestArea = 0.0;
    for (const QPolygonF& polygon : polygons) {
        const double area = polygonArea(polygon);
        if (area > largestArea) {
            largestArea = area;
            outer = polygon;
        }
    }
    if (outer.size() < 3) {
        return QVector<SleeveVertex>();
    }

    QVector<SleeveVertex> result;
    result.reserve(outer.size());
    for (const QPointF& point : outer) {
        if (result.isEmpty()
            || distance(result.last().point, point) > 1.0e-3) {
            result.append(SleeveVertex(point, 0.0));
        }
    }
    if (result.size() > 1
        && distance(result.first().point, result.last().point) <= 1.0e-4) {
        result.removeLast();
    }
    if (!isSimpleClosedPolyline(result, 1.0e-6)) {
        return QVector<SleeveVertex>();
    }
    return result;
}

double signedPolygonArea(const QVector<QPointF>& points)
{
    if (points.size() < 3)
        return 0.0;
    double twiceArea = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        const QPointF& first = points.at(i);
        const QPointF& second = points.at((i + 1) % points.size());
        twiceArea += first.x() * second.y() - second.x() * first.y();
    }
    return twiceArea * 0.5;
}

QPointF contourOutwardNormal(const QVector<SleeveVertex>& contour,
                             const QPointF& lineStart,
                             const QPointF& lineEnd)
{
    const QPointF direction = normalized(lineEnd - lineStart);
    if (length(direction) <= kEpsilon)
        return QPointF();

    const QVector<QPointF> samples = sampledContour(contour);
    const double area = signedPolygonArea(samples);
    // For a counter-clockwise contour the exterior is on the right.  The
    // result is intentionally derived from the contour winding so rotated or
    // mirrored sleeves do not depend on world X/Y directions.
    return normalized(area >= 0.0
                          ? QPointF(direction.y(), -direction.x())
                          : QPointF(-direction.y(), direction.x()));
}

bool lineLineIntersection(const QPointF& firstPoint,
                          const QPointF& firstDirection,
                          const QPointF& secondPoint,
                          const QPointF& secondDirection,
                          QPointF* intersection,
                          double* firstParameter,
                          double* secondParameter)
{
    const double denominator = cross(firstDirection, secondDirection);
    if (qAbs(denominator) <= kEpsilon)
        return false;

    const QPointF delta = secondPoint - firstPoint;
    const double first = cross(delta, secondDirection) / denominator;
    const double second = cross(delta, firstDirection) / denominator;
    if (intersection)
        *intersection = firstPoint + firstDirection * first;
    if (firstParameter)
        *firstParameter = first;
    if (secondParameter)
        *secondParameter = second;
    return true;
}

QVector<QPointF> routeBetween(const QVector<QPointF>& points,
                              int start,
                              int end,
                              bool forward)
{
    QVector<QPointF> result;
    if (points.size() < 2 || start < 0 || end < 0
        || start >= points.size() || end >= points.size()) {
        return result;
    }

    const int count = points.size();
    int current = start;
    result.append(points.at(current));
    while (current != end) {
        current = forward ? (current + 1) % count
                          : (current + count - 1) % count;
        result.append(points.at(current));
        if (result.size() > count + 1) {
            result.clear();
            return result;
        }
    }
    return result;
}

double pointPathDistance(const QPointF& point,
                         const QVector<QPointF>& path)
{
    if (path.size() < 2)
        return std::numeric_limits<double>::max();

    double best = std::numeric_limits<double>::max();
    for (int i = 1; i < path.size(); ++i) {
        const QPointF direction = path.at(i) - path.at(i - 1);
        const double denominator = dot(direction, direction);
        const double parameter = denominator <= kEpsilon
            ? 0.0
            : clamp(dot(point - path.at(i - 1), direction) / denominator,
                    0.0, 1.0);
        best = qMin(best, distance(point,
                                   path.at(i - 1) + direction * parameter));
    }
    return best;
}

QVector<QPointF> expandedCapRoute(const QVector<SleeveVertex>& expanded,
                                  const SleeveAnalysis& analysis,
                                  const QPointF& capStart,
                                  const QPointF& capEnd)
{
    QVector<QPointF> points = sampledContour(expanded);
    if (points.size() > 1 && samePoint(points.first(), points.last()))
        points.removeLast();
    if (points.size() < 3 || analysis.sampledSleeveCap.size() < 2)
        return QVector<QPointF>();

    int startIndex = -1;
    int endIndex = -1;
    double startDistance = std::numeric_limits<double>::max();
    double endDistance = std::numeric_limits<double>::max();
    for (int i = 0; i < points.size(); ++i) {
        const double startCandidate = distance(points.at(i), capStart);
        if (startCandidate < startDistance) {
            startDistance = startCandidate;
            startIndex = i;
        }
        const double endCandidate = distance(points.at(i), capEnd);
        if (endCandidate < endDistance) {
            endDistance = endCandidate;
            endIndex = i;
        }
    }
    if (startIndex < 0 || endIndex < 0 || startIndex == endIndex)
        return QVector<QPointF>();

    const QVector<QPointF> forward = routeBetween(points, startIndex,
                                                   endIndex, true);
    const QVector<QPointF> backward = routeBetween(points, startIndex,
                                                    endIndex, false);
    if (forward.size() < 2 || backward.size() < 2)
        return QVector<QPointF>();

    const auto routeScore = [&](const QVector<QPointF>& route) {
        double total = 0.0;
        for (const QPointF& point : route)
            total += pointPathDistance(point, analysis.sampledSleeveCap);
        return total / route.size();
    };
    return routeScore(forward) <= routeScore(backward) ? forward : backward;
}

bool linePolylineIntersection(const QVector<QPointF>& polyline,
                              const QPointF& linePoint,
                              const QPointF& lineDirection,
                              const QPointF& expected,
                              QPointF* intersection,
                              int* segmentIndex,
                              double* segmentParameter)
{
    double bestDistance = std::numeric_limits<double>::max();
    bool found = false;
    for (int i = 1; i < polyline.size(); ++i) {
        const QPointF segmentDirection = polyline.at(i) - polyline.at(i - 1);
        QPointF candidate;
        double lineParameter = 0.0;
        double onSegment = 0.0;
        if (!lineLineIntersection(linePoint, lineDirection,
                                   polyline.at(i - 1), segmentDirection,
                                   &candidate, &lineParameter, &onSegment)) {
            continue;
        }
        if (onSegment < -1.0e-6 || onSegment > 1.0 + 1.0e-6)
            continue;
        const double candidateDistance = distance(candidate, expected);
        if (candidateDistance < bestDistance) {
            bestDistance = candidateDistance;
            if (intersection)
                *intersection = candidate;
            if (segmentIndex)
                *segmentIndex = i - 1;
            if (segmentParameter)
                *segmentParameter = clamp(onSegment, 0.0, 1.0);
            found = true;
        }
    }
    return found;
}

QVector<QPointF> clipCapRoute(const QVector<QPointF>& route,
                              const QPointF& start,
                              int startSegment,
                              const QPointF& end,
                              int endSegment)
{
    QVector<QPointF> result;
    if (route.size() < 2 || startSegment < 0 || endSegment < startSegment
        || endSegment >= route.size() - 1) {
        return result;
    }

    result.append(start);
    for (int i = startSegment + 1; i <= endSegment; ++i) {
        if (distance(result.last(), route.at(i)) > 1.0e-7)
            result.append(route.at(i));
    }
    if (result.isEmpty() || distance(result.last(), end) > 1.0e-7)
        result.append(end);
    return result;
}

bool polylineSegmentAtPoint(const QVector<QPointF>& polyline,
                            const QPointF& point,
                            int* segmentIndex,
                            double* segmentParameter)
{
    const double tolerance = 1.0e-5;
    for (int i = 1; i < polyline.size(); ++i) {
        const QPointF direction = polyline.at(i) - polyline.at(i - 1);
        const double denominator = dot(direction, direction);
        if (denominator <= kEpsilon)
            continue;
        const double parameter = dot(point - polyline.at(i - 1), direction)
            / denominator;
        const double clampedParameter = clamp(parameter, 0.0, 1.0);
        const QPointF projection = polyline.at(i - 1)
            + direction * clampedParameter;
        if (parameter >= -tolerance && parameter <= 1.0 + tolerance
            && distance(projection, point) <= tolerance) {
            if (segmentIndex)
                *segmentIndex = i - 1;
            if (segmentParameter)
                *segmentParameter = clampedParameter;
            return true;
        }
    }
    return false;
}

double sourceCuffCutAxial(const SleeveAnalysis& analysis,
                          const QPointF& axis,
                          double fallback)
{
    if (analysis.sampledCuff.size() < 2)
        return fallback;

    double total = 0.0;
    for (const QPointF& point : analysis.sampledCuff)
        total += dot(point - analysis.cuffCenter, axis);
    return total / analysis.sampledCuff.size();
}

double innerCuffFeatureAxial(const SleevePatternAnalysis& pattern,
                             const SleeveAnalysis& analysis,
                             const QPointF& axis,
                             double fallback)
{
    double result = std::numeric_limits<double>::max();
    for (const SleeveCuffFeature& feature : pattern.cuffFeatures) {
        if (feature.source.vertices.isEmpty())
            continue;
        double total = 0.0;
        int count = 0;
        for (const SleeveVertex& vertex : feature.source.vertices) {
            total += dot(vertex.point - analysis.cuffCenter, axis);
            ++count;
        }
        if (count > 0)
            result = qMin(result, total / count);
    }
    return result == std::numeric_limits<double>::max() ? fallback : result;
}

QVector<SleeveVertex> buildExpandedOuter(
    const QVector<SleeveVertex>& inner,
    const SleeveAnalysis& analysis,
    const TargetCuffGeometry& targetCuff,
    const QPointF& capStart,
    const QPointF& capEnd,
    bool capStartsAtPoint1,
    double sideOffset,
    double cuffCutAxial,
    const QVector<SleeveVertex>& expandedSource)
{
    const QVector<QPointF> capRoute = expandedCapRoute(
        expandedSource, analysis, capStart, capEnd);
    if (capRoute.size() < 2) {
        return QVector<SleeveVertex>();
    }

    const QPointF firstCuff = capStartsAtPoint1
        ? targetCuff.lower : targetCuff.upper;
    const QPointF secondCuff = capStartsAtPoint1
        ? targetCuff.upper : targetCuff.lower;
    const QPointF firstSideNormal = contourOutwardNormal(
        inner, capEnd, firstCuff);
    const QPointF secondSideNormal = contourOutwardNormal(
        inner, secondCuff, capStart);
    if (length(firstSideNormal) <= kEpsilon
        || length(secondSideNormal) <= kEpsilon) {
        return QVector<SleeveVertex>();
    }

    const QPointF firstSideStart = capEnd + firstSideNormal * sideOffset;
    const QPointF firstSideEnd = firstCuff + firstSideNormal * sideOffset;
    const QPointF secondSideStart = secondCuff + secondSideNormal * sideOffset;
    const QPointF secondSideEnd = capStart + secondSideNormal * sideOffset;
    const QPointF firstSideDirection = firstSideEnd - firstSideStart;
    const QPointF secondSideDirection = secondSideEnd - secondSideStart;
    if (length(firstSideDirection) <= kEpsilon
        || length(secondSideDirection) <= kEpsilon) {
        return QVector<SleeveVertex>();
    }

    const QPointF cuffDirection = secondCuff - firstCuff;
    const QPointF outerCuffFirst = firstCuff
        + normalized(targetCuff.center - analysis.point3) * cuffCutAxial;
    const QPointF outerCuffSecond = secondCuff
        + normalized(targetCuff.center - analysis.point3) * cuffCutAxial;
    const QPointF outerCuffDirection = outerCuffSecond - outerCuffFirst;
    if (length(cuffDirection) <= kEpsilon
        || length(outerCuffDirection) <= kEpsilon) {
        return QVector<SleeveVertex>();
    }

    QPointF capAtEnd;
    QPointF capAtStart;
    int capEndSegment = -1;
    int capStartSegment = -1;
    const bool capIntersectionsFound =
        linePolylineIntersection(capRoute, firstSideStart,
                                 firstSideDirection, capEnd,
                                 &capAtEnd, &capEndSegment, nullptr)
        && linePolylineIntersection(capRoute, secondSideEnd,
                                    secondSideDirection, capStart,
                                     &capAtStart, &capStartSegment, nullptr);
    QVector<QPointF> clippedCap;
    if (capIntersectionsFound) {
        // The route is oriented capStart -> capEnd.  A valid pair of joins
        // must occur in that order; otherwise the source outer path is not the
        // cap expansion belonging to this sleeve.
        if (capStartSegment > capEndSegment)
            return QVector<SleeveVertex>();
        clippedCap = clipCapRoute(
            capRoute, capAtStart, capStartSegment, capAtEnd, capEndSegment);
    } else {
        // The source cap can end at a corner whose tangent is not the same as
        // the adjacent side's offset tangent.  Extend the offset side line to
        // the corresponding cap-end tangent and use that real intersection;
        // never replace it with the old P1/P2 endpoint.
        QPointF capStartJoin;
        QPointF capEndJoin;
        const bool startJoinFound = lineLineIntersection(
            secondSideEnd, secondSideDirection, capRoute.first(),
            capRoute.at(1) - capRoute.first(), &capStartJoin, nullptr, nullptr);
        const bool endJoinFound = lineLineIntersection(
            firstSideStart, firstSideDirection, capRoute.last(),
            capRoute.last() - capRoute.at(capRoute.size() - 2),
            &capEndJoin, nullptr, nullptr);
        const double maximumJoinExtension = qMax(sideOffset * 4.0, 1.0e-5);
        if (!startJoinFound || !endJoinFound
            || distance(capStartJoin, capStart) > maximumJoinExtension
            || distance(capEndJoin, capEnd) > maximumJoinExtension) {
            return QVector<SleeveVertex>();
        }

        int startJoinSegment = -1;
        int endJoinSegment = -1;
        double startJoinParameter = 0.0;
        double endJoinParameter = 0.0;
        const bool startJoinOnRoute = polylineSegmentAtPoint(
            capRoute, capStartJoin, &startJoinSegment, &startJoinParameter);
        const bool endJoinOnRoute = polylineSegmentAtPoint(
            capRoute, capEndJoin, &endJoinSegment, &endJoinParameter);
        const QPointF startTangent = capRoute.at(1) - capRoute.first();
        const QPointF endTangent = capRoute.last()
            - capRoute.at(capRoute.size() - 2);
        const double startTangentDenominator = dot(startTangent, startTangent);
        const double endTangentDenominator = dot(endTangent, endTangent);
        const double startTangentParameter = startTangentDenominator <= kEpsilon
            ? 0.0
            : dot(capStartJoin - capRoute.first(), startTangent)
                / startTangentDenominator;
        const double endTangentParameter = endTangentDenominator <= kEpsilon
            ? 0.0
            : dot(capEndJoin - capRoute.at(capRoute.size() - 2), endTangent)
                / endTangentDenominator;
        if ((!startJoinOnRoute && startTangentParameter > 1.0 + 1.0e-5)
            || (!endJoinOnRoute && endTangentParameter < -1.0e-5)) {
            return QVector<SleeveVertex>();
        }
        const int routeStartIndex = startJoinOnRoute
            ? startJoinSegment + 1 : 0;
        const int routeEndIndex = endJoinOnRoute
            ? endJoinSegment : capRoute.size() - 1;
        if (routeStartIndex > routeEndIndex
            || (startJoinOnRoute && endJoinOnRoute
                && startJoinSegment == endJoinSegment
                && startJoinParameter > endJoinParameter)) {
            return QVector<SleeveVertex>();
        }
        clippedCap.append(capStartJoin);
        for (int i = routeStartIndex; i <= routeEndIndex; ++i) {
            if (distance(clippedCap.last(), capRoute.at(i)) > 1.0e-7)
                clippedCap.append(capRoute.at(i));
        }
        if (distance(clippedCap.last(), capEndJoin) > 1.0e-7)
            clippedCap.append(capEndJoin);
    }
    if (clippedCap.size() < 2) {
        return QVector<SleeveVertex>();
    }
    QPointF cuffAtFirst;
    QPointF cuffAtSecond;
    const bool firstCuffIntersection = lineLineIntersection(
        firstSideStart, firstSideDirection, outerCuffFirst,
        outerCuffDirection, &cuffAtFirst, nullptr, nullptr);
    const bool secondCuffIntersection = lineLineIntersection(
        secondSideStart, secondSideDirection, outerCuffFirst,
        outerCuffDirection, &cuffAtSecond, nullptr, nullptr);
    if (!firstCuffIntersection || !secondCuffIntersection) {
        return QVector<SleeveVertex>();
    }

    QVector<SleeveVertex> result;
    for (const QPointF& point : clippedCap)
        appendStraightPoint(&result, point);
    appendStraightPoint(&result, cuffAtFirst);
    appendStraightPoint(&result, cuffAtSecond);
    if (!isSimpleClosedPolyline(result, 1.0e-6)) {
        return QVector<SleeveVertex>();
    }
    return result;
}

LongSleeveContour buildContourInternal(
    const QVector<SleeveVertex>& inputVertices,
    const SleeveAnalysis& analysis,
    const TargetCuffGeometry& targetCuff,
    const SleeveSidePreview& sleeveSides,
    const SleevePatternAnalysis* pattern)
{
    LongSleeveContour result;
    if (!analysis.valid || !targetCuff.valid || !targetCuff.validCuff
        || !sleeveSides.valid) {
        result.failureReason = QStringLiteral(
            "Sleeve analysis, target cuff, and side preview must all be valid.");
        return result;
    }
    if (targetCuff.targetSleeveLengthMM <= analysis.sleeveLengthMM) {
        result.failureReason = QStringLiteral(
            "Target sleeve length must be greater than current sleeve length.");
        return result;
    }
    if (inputVertices.size() < 3 || sleeveSides.upperPoints.size() < 2
        || sleeveSides.lowerPoints.size() < 2) {
        result.failureReason = QStringLiteral("Long sleeve contour data is incomplete.");
        return result;
    }
    if (!samePoint(sleeveSides.upperPoints.first(), analysis.point1)
        || !samePoint(sleeveSides.lowerPoints.first(), analysis.point2)
        || !samePoint(sleeveSides.upperPoints.last(), targetCuff.upper)
        || !samePoint(sleeveSides.lowerPoints.last(), targetCuff.lower)) {
        result.failureReason = QStringLiteral(
            "Side preview endpoints do not match P1/P2 and the target cuff.");
        return result;
    }

    QVector<SleeveVertex> originalVertices = inputVertices;
    if (originalVertices.size() > 1
        && samePoint(originalVertices.first().point,
                     originalVertices.last().point)) {
        originalVertices.removeLast();
    }
    const int point1Index = vertexIndex(originalVertices, analysis.point1);
    const int point2Index = vertexIndex(originalVertices, analysis.point2);
    if (point1Index < 0 || point2Index < 0) {
        result.failureReason = QStringLiteral(
            "P1/P2 were not found in the original polyline vertices.");
        return result;
    }

    int capStart = -1;
    int capEnd = -1;
    if (forwardSegmentCount(point1Index, point2Index, originalVertices.size())
        == analysis.sleeveCapSegmentCount) {
        capStart = point1Index;
        capEnd = point2Index;
    } else if (forwardSegmentCount(point2Index, point1Index,
                                   originalVertices.size())
               == analysis.sleeveCapSegmentCount) {
        capStart = point2Index;
        capEnd = point1Index;
    } else {
        result.failureReason = QStringLiteral(
            "The original sleeve-cap segment range could not be identified.");
        return result;
    }

    int current = capStart;
    double capLengthDrawingUnits = 0.0;
    while (true) {
        result.vertices.append(originalVertices.at(current));
        if (current == capEnd)
            break;
        const int next = (current + 1) % originalVertices.size();
        capLengthDrawingUnits += segmentLength(originalVertices.at(current),
                                               originalVertices.at(next));
        current = next;
    }
    // The endpoint is retained exactly, but its outgoing source bulge belongs
    // to the old short-sleeve side.  The new side is a contractual straight
    // segment, hence its outgoing bulge must be zero.
    result.vertices.last().bulge = 0.0;

    if (capStart == point1Index) {
        for (int i = 1; i < sleeveSides.lowerPoints.size(); ++i)
            appendStraightPoint(&result.vertices, sleeveSides.lowerPoints.at(i));
        appendStraightPoint(&result.vertices, targetCuff.upper);
        for (int i = sleeveSides.upperPoints.size() - 2; i > 0; --i)
            appendStraightPoint(&result.vertices, sleeveSides.upperPoints.at(i));
    } else {
        for (int i = 1; i < sleeveSides.upperPoints.size(); ++i)
            appendStraightPoint(&result.vertices, sleeveSides.upperPoints.at(i));
        appendStraightPoint(&result.vertices, targetCuff.lower);
        for (int i = sleeveSides.lowerPoints.size() - 2; i > 0; --i)
            appendStraightPoint(&result.vertices, sleeveSides.lowerPoints.at(i));
    }

    const SleeveAnalyzer converter(analysis.drawingUnit);
    result.sleeveCapSegmentCount = analysis.sleeveCapSegmentCount;
    result.sleeveCapLengthMM = converter.drawingUnitToMM(capLengthDrawingUnits);
    result.targetSleeveLengthMM = targetCuff.measuredSleeveLengthMM;
    result.targetCuffWidthMM = targetCuff.measuredCuffWidthMM;
    if (pattern) {
        result.seamAllowanceMM = pattern->seamAllowanceDetected
            ? pattern->measuredSeamAllowanceMM
            : pattern->expectedSeamAllowanceMM;
        result.originalCuffFoldWidthMM = pattern->originalCuffFoldWidthMM;
        result.originalCuffStitchWidthMM = pattern->originalCuffStitchWidthMM;
    }

    if (qAbs(result.sleeveCapLengthMM - analysis.sleeveCapLengthMM) > 0.001
        || qAbs(result.targetSleeveLengthMM - targetCuff.targetSleeveLengthMM) > 0.5
        || qAbs(result.targetCuffWidthMM - targetCuff.targetCuffWidthMM) > 0.5) {
        result.vertices.clear();
        result.failureReason = QStringLiteral(
            "Generated contour failed sleeve-cap or target-dimension validation.");
        return result;
    }
    if (!isSimpleClosedPolyline(result.vertices)) {
        result.vertices.clear();
        result.failureReason = QStringLiteral(
            "Generated long-sleeve contour is not a simple closed polyline.");
        return result;
    }

    double measuredSourceCuffCutAxial = 0.0;
    double measuredInnerCuffFeatureAxial = 0.0;
    double newCuffCutAxial = 0.0;
    double seamAllowanceDrawing = 0.0;
    if (pattern) {
        seamAllowanceDrawing = converter.mmToDrawingUnit(
            result.seamAllowanceMM);
        if (seamAllowanceDrawing <= kEpsilon) {
            result.vertices.clear();
            result.failureReason = QStringLiteral(
                "A positive seam allowance is required for the expanded outline.");
            return result;
        }

        const QPointF axis = normalized(analysis.sleeveAxis);
        const QPointF sourceTransverse = normalized(
            analysis.cuffUpper - analysis.cuffLower);
        if (length(axis) <= kEpsilon || length(sourceTransverse) <= kEpsilon) {
            result.vertices.clear();
            result.failureReason = QStringLiteral(
                "Cuff reconstruction directions are invalid.");
            return result;
        }

        measuredSourceCuffCutAxial = sourceCuffCutAxial(
            analysis, axis, 0.0);
        measuredInnerCuffFeatureAxial = innerCuffFeatureAxial(
            *pattern, analysis, axis,
            -converter.mmToDrawingUnit(pattern->originalCuffFoldWidthMM));

        // A is deliberately measured from the source cutting line to the
        // innermost detected cuff fold/stitch line.  It is not a fixed cuff
        // allowance and therefore remains correct for rotated drawings and
        // patterns whose cuff construction differs from the fixture.
        const double cuffCutToFoldDrawing = qAbs(
            measuredInnerCuffFeatureAxial - measuredSourceCuffCutAxial);
        if (cuffCutToFoldDrawing <= kEpsilon) {
            result.vertices.clear();
            result.failureReason = QStringLiteral(
                "The original cuff cutting line and inner fold line coincide.");
            return result;
        }
        result.originalCuffCutToFoldDistanceMM = converter.drawingUnitToMM(
            cuffCutToFoldDrawing);
        result.cuffInsetMM = result.originalCuffCutToFoldDistanceMM;
        // Sleeve Axis is oriented from the sleeve cap toward the cuff.  The
        // expanded cuff cutting line therefore moves outward by A; using the
        // opposite sign would shorten the generated long sleeve.
        newCuffCutAxial = cuffCutToFoldDrawing;
    }

    if (pattern) {
        const QPointF capStartPoint = originalVertices.at(capStart).point;
        const QPointF capEndPoint = originalVertices.at(capEnd).point;
        QVector<SleeveVertex> expandedSource = pattern->seamAllowance.vertices;
        if (expandedSource.size() < 3)
            expandedSource = outwardOffset(result.vertices, seamAllowanceDrawing);

        result.outerVertices = buildExpandedOuter(
            result.vertices, analysis, targetCuff, capStartPoint,
            capEndPoint, capStart == point1Index, seamAllowanceDrawing,
            newCuffCutAxial, expandedSource);
        if (result.outerVertices.isEmpty()) {
            // A detected source cutting path is preferred because it retains
            // the original cap profile.  If that path is not the exact
            // offset of the selected seam (some DXF exports contain a
            // translated/rounded copy), regenerate only the cap source from
            // the actual contour.  The side endpoints are still obtained by
            // line intersection in both cases.
            const QVector<SleeveVertex> geometricSource = outwardOffset(
                result.vertices, seamAllowanceDrawing);
            result.outerVertices = buildExpandedOuter(
                result.vertices, analysis, targetCuff, capStartPoint,
                capEndPoint, capStart == point1Index, seamAllowanceDrawing,
                newCuffCutAxial, geometricSource);
        }
        if (result.outerVertices.isEmpty()) {
            result.vertices.clear();
            result.failureReason = QStringLiteral(
                "The offset side lines could not be joined to the expanded cuff and sleeve-cap outlines.");
            return result;
        }
    }
    result.valid = true;
    return result;
}

} // namespace

LongSleeveContour LongSleeveGenerator::buildContour(
    const QVector<SleeveVertex>& inputVertices,
    const SleeveAnalysis& analysis,
    const TargetCuffGeometry& targetCuff,
    const SleeveSidePreview& sleeveSides)
{
    return buildContourInternal(inputVertices, analysis, targetCuff,
                                sleeveSides, nullptr);
}

bool LongSleeveGenerator::generate(
    Document_Interface* document,
    const QVector<SleeveVertex>& originalVertices,
    const SleeveAnalysis& analysis,
    const TargetCuffGeometry& targetCuff,
    const SleeveSidePreview& sleeveSides,
    QString* errorMessage)
{
    if (!document) {
        if (errorMessage)
            *errorMessage = QStringLiteral("No active drawing document.");
        return false;
    }
    const LongSleeveContour contour = buildContour(
        originalVertices, analysis, targetCuff, sleeveSides);
    if (!contour.valid) {
        if (errorMessage)
            *errorMessage = contour.failureReason;
        return false;
    }

    const QString previousLayer = document->getCurrentLayer();
    document->setLayer(QStringLiteral("LONG_SLEEVE"));
    std::vector<std::vector<Plug_VertexData> > polylines;
    std::vector<bool> closed;
    polylines.push_back(plugVertices(contour.vertices));
    closed.push_back(true);
    addPolylineGroupCompat(document, polylines, closed);
    document->setLayer(previousLayer);
    document->updateView();
    return true;
}

LongSleeveContour LongSleeveGenerator::buildContour(
    const QVector<SleeveVertex>& originalVertices,
    const SleevePatternAnalysis& pattern,
    const TargetCuffGeometry& targetCuff,
    const SleeveSidePreview& sleeveSides)
{
    return buildContourInternal(originalVertices, pattern.geometry, targetCuff,
                                sleeveSides, &pattern);
}

bool LongSleeveGenerator::generate(
    Document_Interface* document,
    const QVector<SleeveVertex>& originalVertices,
    const SleevePatternAnalysis& pattern,
    const TargetCuffGeometry& targetCuff,
    const SleeveSidePreview& sleeveSides,
    QString* errorMessage)
{
    if (!document) {
        if (errorMessage)
            *errorMessage = QStringLiteral("No active drawing document.");
        return false;
    }
    const LongSleeveContour contour = buildContour(
        originalVertices, pattern, targetCuff, sleeveSides);
    if (!contour.valid) {
        if (errorMessage)
            *errorMessage = contour.failureReason;
        return false;
    }

    const QString previousLayer = document->getCurrentLayer();
    document->setLayer(QStringLiteral("LONG_SLEEVE"));
    std::vector<std::vector<Plug_VertexData> > polylines;
    std::vector<bool> closed;
    polylines.push_back(plugVertices(contour.outerVertices));
    closed.push_back(true);
    polylines.push_back(plugVertices(contour.vertices));
    closed.push_back(true);
    for (const LongSleeveFeature& feature : contour.generatedFeatures) {
        polylines.push_back(plugVertices(feature.vertices));
        closed.push_back(feature.closed);
    }
    addPolylineGroupCompat(document, polylines, closed);
    document->setLayer(previousLayer);
    document->updateView();
    return true;
}
