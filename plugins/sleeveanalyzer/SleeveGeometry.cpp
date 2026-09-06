#include "SleeveGeometry.h"

#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace SleeveGeometry {

double dot(const QPointF& a, const QPointF& b)
{
    return a.x() * b.x() + a.y() * b.y();
}

double cross(const QPointF& a, const QPointF& b)
{
    return a.x() * b.y() - a.y() * b.x();
}

double length(const QPointF& value)
{
    return qSqrt(dot(value, value));
}

double distance(const QPointF& a, const QPointF& b)
{
    return length(b - a);
}

QPointF normalized(const QPointF& value)
{
    const double magnitude = length(value);
    return magnitude > kEpsilon ? value / magnitude : QPointF();
}

QPointF perpendicularLeft(const QPointF& value)
{
    return QPointF(-value.y(), value.x());
}

double clamp(double value, double minimum, double maximum)
{
    return qMax(minimum, qMin(maximum, value));
}

double angleBetween(const QPointF& a, const QPointF& b)
{
    const double denominator = length(a) * length(b);
    if (denominator <= kEpsilon)
        return 0.0;
    return qAcos(clamp(dot(a, b) / denominator, -1.0, 1.0));
}

double pointLineDistance(const QPointF& point,
                         const QPointF& lineStart,
                         const QPointF& lineEnd)
{
    const QPointF direction = lineEnd - lineStart;
    const double denominator = length(direction);
    if (denominator <= kEpsilon)
        return distance(point, lineStart);
    return qAbs(cross(direction, point - lineStart)) / denominator;
}

bool infiniteLineSegmentIntersection(const QPointF& linePoint,
                                     const QPointF& lineDirection,
                                     const QPointF& segmentStart,
                                     const QPointF& segmentEnd,
                                     QPointF* intersection,
                                     double* lineParameter)
{
    const QPointF segmentDirection = segmentEnd - segmentStart;
    const double denominator = cross(lineDirection, segmentDirection);
    if (qAbs(denominator) <= kEpsilon)
        return false;

    const QPointF delta = segmentStart - linePoint;
    const double onLine = cross(delta, segmentDirection) / denominator;
    const double onSegment = cross(delta, lineDirection) / denominator;
    if (onSegment < -kEpsilon || onSegment > 1.0 + kEpsilon)
        return false;

    if (intersection)
        *intersection = linePoint + onLine * lineDirection;
    if (lineParameter)
        *lineParameter = onLine;
    return true;
}

double polylineLength(const QVector<QPointF>& points)
{
    double result = 0.0;
    for (int i = 1; i < points.size(); ++i)
        result += distance(points.at(i - 1), points.at(i));
    return result;
}

namespace {

constexpr double kPi = 3.14159265358979323846;

double positiveAngle(double angle)
{
    angle = std::fmod(angle, 2.0 * kPi);
    if (angle < 0.0)
        angle += 2.0 * kPi;
    return angle;
}

bool angleOnDirectedArc(double angle, double startAngle, double sweep)
{
    const double travelled = sweep >= 0.0
        ? positiveAngle(angle - startAngle)
        : positiveAngle(startAngle - angle);
    return travelled <= qAbs(sweep) + 1.0e-10;
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

    const QPointF chord = end.point - start.point;
    const double chordLength = length(chord);
    const double angle = 4.0 * qAtan(start.bulge);
    const double sine = qSin(qAbs(angle) * 0.5);
    if (chordLength <= kEpsilon || sine <= kEpsilon) {
        result.append(end.point);
        return result;
    }
    const double radius = chordLength / (2.0 * sine);
    const QPointF midpoint = (start.point + end.point) * 0.5;
    const QPointF normal = normalized(perpendicularLeft(chord));
    const QPointF center = midpoint
                           + normal * (chordLength / (2.0 * qTan(angle * 0.5)));
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

bool pointOnSegment(const QPointF& point,
                    const QPointF& start,
                    const QPointF& end,
                    double tolerance)
{
    if (pointLineDistance(point, start, end) > tolerance)
        return false;
    return point.x() >= qMin(start.x(), end.x()) - tolerance
        && point.x() <= qMax(start.x(), end.x()) + tolerance
        && point.y() >= qMin(start.y(), end.y()) - tolerance
        && point.y() <= qMax(start.y(), end.y()) + tolerance;
}

int orientation(const QPointF& first,
                const QPointF& second,
                const QPointF& third,
                double tolerance)
{
    const double value = cross(second - first, third - first);
    if (value > tolerance)
        return 1;
    if (value < -tolerance)
        return -1;
    return 0;
}

bool segmentsIntersect(const QPointF& firstStart,
                       const QPointF& firstEnd,
                       const QPointF& secondStart,
                       const QPointF& secondEnd,
                       double tolerance)
{
    const int a = orientation(firstStart, firstEnd, secondStart, tolerance);
    const int b = orientation(firstStart, firstEnd, secondEnd, tolerance);
    const int c = orientation(secondStart, secondEnd, firstStart, tolerance);
    const int d = orientation(secondStart, secondEnd, firstEnd, tolerance);
    if (a != b && c != d)
        return true;
    return (a == 0 && pointOnSegment(secondStart, firstStart, firstEnd, tolerance))
        || (b == 0 && pointOnSegment(secondEnd, firstStart, firstEnd, tolerance))
        || (c == 0 && pointOnSegment(firstStart, secondStart, secondEnd, tolerance))
        || (d == 0 && pointOnSegment(firstEnd, secondStart, secondEnd, tolerance));
}

bool adjacent(int first, int second, int segmentCount)
{
    return first == second
        || first + 1 == second
        || second + 1 == first
        || (first == 0 && second == segmentCount - 1)
        || (second == 0 && first == segmentCount - 1);
}

} // namespace

double minimumAxialProjection(const QVector<SleeveVertex>& vertices,
                              const QPointF& origin,
                              const QPointF& axis)
{
    const QPointF direction = normalized(axis);
    if (vertices.isEmpty() || length(direction) <= kEpsilon)
        return 0.0;

    double minimum = std::numeric_limits<double>::max();
    const auto addPoint = [&](const QPointF& point) {
        minimum = qMin(minimum, dot(point - origin, direction));
    };

    for (int i = 0; i + 1 < vertices.size(); ++i) {
        const SleeveVertex& start = vertices.at(i);
        const SleeveVertex& end = vertices.at(i + 1);
        addPoint(start.point);
        addPoint(end.point);
        if (qAbs(start.bulge) <= kEpsilon)
            continue;

        const QPointF chord = end.point - start.point;
        const double chordLength = length(chord);
        const double sweep = 4.0 * qAtan(start.bulge);
        const double sine = qSin(qAbs(sweep) * 0.5);
        if (chordLength <= kEpsilon || sine <= kEpsilon)
            continue;

        const double radius = chordLength / (2.0 * sine);
        const QPointF midpoint = (start.point + end.point) * 0.5;
        const QPointF normal = normalized(perpendicularLeft(chord));
        const QPointF center = midpoint
            + normal * (chordLength / (2.0 * qTan(sweep * 0.5)));
        const double startAngle = qAtan2(start.point.y() - center.y(),
                                         start.point.x() - center.x());
        const double axisAngle = qAtan2(direction.y(), direction.x());
        const double extrema[] = {axisAngle, axisAngle + kPi};
        for (const double angle : extrema) {
            if (angleOnDirectedArc(angle, startAngle, sweep))
                addPoint(center + QPointF(qCos(angle), qSin(angle)) * radius);
        }
    }
    return minimum == std::numeric_limits<double>::max() ? 0.0 : minimum;
}

bool isSimpleClosedPolyline(const QVector<SleeveVertex>& vertices,
                            double tolerance)
{
    if (vertices.size() < 3)
        return false;

    QVector<SleeveVertex> normalizedVertices = vertices;
    if (distance(normalizedVertices.first().point,
                 normalizedVertices.last().point) <= tolerance) {
        normalizedVertices.removeLast();
    }
    if (normalizedVertices.size() < 3)
        return false;

    QVector<QPointF> sampled;
    for (int i = 0; i < normalizedVertices.size(); ++i) {
        const int next = (i + 1) % normalizedVertices.size();
        const QVector<QPointF> segment = sampleBulgedSegment(
            normalizedVertices.at(i), normalizedVertices.at(next));
        if (segment.size() < 2)
            return false;
        if (sampled.isEmpty())
            sampled.append(segment.first());
        for (int j = 1; j < segment.size(); ++j) {
            if (distance(sampled.last(), segment.at(j)) <= tolerance)
                return false;
            sampled.append(segment.at(j));
        }
    }
    if (sampled.size() < 4)
        return false;

    const int segmentCount = sampled.size() - 1;
    for (int i = 0; i < segmentCount; ++i) {
        for (int j = i + 1; j < segmentCount; ++j) {
            if (adjacent(i, j, segmentCount))
                continue;
            if (segmentsIntersect(sampled.at(i), sampled.at(i + 1),
                                   sampled.at(j), sampled.at(j + 1),
                                   tolerance))
                return false;
        }
    }

    // The last sampled point closes the contour.  Remove it when checking
    // duplicate vertices so the intentional closure is not rejected.
    for (int i = 0; i + 1 < sampled.size(); ++i) {
        for (int j = i + 1; j + 1 < sampled.size(); ++j) {
            if (distance(sampled.at(i), sampled.at(j)) <= tolerance
                && !adjacent(i, j, segmentCount))
                return false;
        }
    }
    return true;
}

} // namespace SleeveGeometry
