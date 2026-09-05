#ifndef SLEEVEGEOMETRY_H
#define SLEEVEGEOMETRY_H

#include "SleeveAnalyzer.h"

#include <QPointF>
#include <QVector>

namespace SleeveGeometry {

constexpr double kEpsilon = 1.0e-9;

double dot(const QPointF& a, const QPointF& b);
double cross(const QPointF& a, const QPointF& b);
double length(const QPointF& value);
double distance(const QPointF& a, const QPointF& b);
QPointF normalized(const QPointF& value);
QPointF perpendicularLeft(const QPointF& value);
double clamp(double value, double minimum, double maximum);
double angleBetween(const QPointF& a, const QPointF& b);
double pointLineDistance(const QPointF& point,
                         const QPointF& lineStart,
                         const QPointF& lineEnd);
bool infiniteLineSegmentIntersection(const QPointF& linePoint,
                                     const QPointF& lineDirection,
                                     const QPointF& segmentStart,
                                     const QPointF& segmentEnd,
                                     QPointF* intersection,
                                     double* lineParameter = nullptr);
double polylineLength(const QVector<QPointF>& points);

// Validate the sampled geometry of a closed polyline, including bulge arcs.
// Consecutive duplicate vertices, duplicate segments, and self-intersections
// are rejected.  This is deliberately a geometric check rather than a DXF
// flag check so generated contours cannot silently contain invalid topology.
bool isSimpleClosedPolyline(const QVector<SleeveVertex>& vertices,
                            double tolerance = 1.0e-7);

} // namespace SleeveGeometry

#endif // SLEEVEGEOMETRY_H
