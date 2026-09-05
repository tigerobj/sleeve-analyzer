#include "SleevePatternFeatures.h"

#include "SleeveGeometry.h"

#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

using namespace SleeveGeometry;

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

QVector<QPointF> sampledPath(const SleevePatternPath& path)
{
    QVector<QPointF> result;
    if (path.vertices.isEmpty())
        return result;

    result.append(path.vertices.first().point);
    for (int i = 0; i + 1 < path.vertices.size(); ++i) {
        const QVector<QPointF> segment = sampleSegment(path.vertices.at(i),
                                                        path.vertices.at(i + 1));
        for (int j = 1; j < segment.size(); ++j)
            result.append(segment.at(j));
    }
    if (path.closed) {
        const QVector<QPointF> segment = sampleSegment(path.vertices.last(),
                                                        path.vertices.first());
        for (int j = 1; j < segment.size(); ++j)
            result.append(segment.at(j));
    }
    return result;
}

QRectF pathBounds(const QVector<QPointF>& points)
{
    if (points.isEmpty())
        return QRectF();
    double minimumX = points.first().x();
    double minimumY = points.first().y();
    double maximumX = minimumX;
    double maximumY = minimumY;
    for (int i = 1; i < points.size(); ++i) {
        minimumX = qMin(minimumX, points.at(i).x());
        minimumY = qMin(minimumY, points.at(i).y());
        maximumX = qMax(maximumX, points.at(i).x());
        maximumY = qMax(maximumY, points.at(i).y());
    }
    return QRectF(QPointF(minimumX, minimumY),
                  QPointF(maximumX, maximumY));
}

double segmentDistance(const QPointF& point,
                       const QPointF& start,
                       const QPointF& end)
{
    const QPointF direction = end - start;
    const double denominator = dot(direction, direction);
    if (denominator <= kEpsilon)
        return distance(point, start);
    const double parameter = clamp(dot(point - start, direction) / denominator,
                                   0.0, 1.0);
    return distance(point, start + direction * parameter);
}

double pointPathDistance(const QPointF& point,
                         const QVector<QPointF>& path)
{
    if (path.isEmpty())
        return std::numeric_limits<double>::max();
    if (path.size() == 1)
        return distance(point, path.first());

    double result = std::numeric_limits<double>::max();
    for (int i = 1; i < path.size(); ++i)
        result = qMin(result, segmentDistance(point, path.at(i - 1), path.at(i)));
    return result;
}

double pathDistance(const QVector<QPointF>& first,
                    const QVector<QPointF>& second)
{
    double result = std::numeric_limits<double>::max();
    for (const QPointF& point : first)
        result = qMin(result, pointPathDistance(point, second));
    for (const QPointF& point : second)
        result = qMin(result, pointPathDistance(point, first));
    return result;
}

double median(QVector<double> values)
{
    if (values.isEmpty())
        return 0.0;
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

double rectangleGap(const QRectF& first, const QRectF& second)
{
    const double dx = qMax(0.0, qMax(first.left() - second.right(),
                                     second.left() - first.right()));
    const double dy = qMax(0.0, qMax(first.top() - second.bottom(),
                                     second.top() - first.bottom()));
    return qSqrt(dx * dx + dy * dy);
}

bool sameSource(const SleevePatternPath& first,
                const SleevePatternPath& second)
{
    return first.sourceIndex >= 0 && first.sourceIndex == second.sourceIndex;
}

bool closedPath(const SleevePatternPath& path)
{
    return path.closed
        || (path.vertices.size() > 1
            && distance(path.vertices.first().point,
                        path.vertices.last().point) <= 1.0e-7);
}

bool isAssociated(const SleevePatternPath& candidate,
                  const SleevePatternPath& sleeve,
                  double radius)
{
    const QVector<QPointF> candidatePoints = sampledPath(candidate);
    const QVector<QPointF> sleevePoints = sampledPath(sleeve);
    if (candidatePoints.isEmpty() || sleevePoints.isEmpty())
        return false;

    const QRectF candidateBounds = pathBounds(candidatePoints);
    const QRectF sleeveBounds = pathBounds(sleevePoints);
    if (rectangleGap(candidateBounds, sleeveBounds) > radius)
        return false;
    return pathDistance(candidatePoints, sleevePoints) <= radius;
}

bool encloses(const QRectF& outer, const QRectF& inner, double tolerance)
{
    return outer.left() <= inner.left() + tolerance
        && outer.right() >= inner.right() - tolerance
        && outer.top() <= inner.top() + tolerance
        && outer.bottom() >= inner.bottom() - tolerance;
}

double average(const QVector<double>& values)
{
    if (values.isEmpty())
        return 0.0;
    double total = 0.0;
    for (double value : values)
        total += value;
    return total / values.size();
}

double closedArea(const SleevePatternPath& path)
{
    const QVector<QPointF> points = sampledPath(path);
    if (points.size() < 3)
        return 0.0;

    int count = points.size();
    if (distance(points.first(), points.last()) <= kEpsilon)
        --count;
    if (count < 3)
        return 0.0;

    double twiceArea = 0.0;
    for (int i = 0; i < count; ++i) {
        const QPointF& first = points.at(i);
        const QPointF& second = points.at((i + 1) % count);
        twiceArea += first.x() * second.y() - second.x() * first.y();
    }
    return qAbs(twiceArea) * 0.5;
}

double boundaryDistance(const QVector<QPointF>& first,
                        const QVector<QPointF>& second)
{
    QVector<double> distances;
    for (const QPointF& point : first)
        distances.append(pointPathDistance(point, second));
    for (const QPointF& point : second)
        distances.append(pointPathDistance(point, first));
    return median(distances);
}

} // namespace

double SleevePatternAnalyzer::defaultSeamAllowanceMM()
{
    // 0.3125 inch expressed exactly in millimetres.
    return 7.9375;
}

SleevePatternAnalysis SleevePatternAnalyzer::analyze(
    const SleevePatternPath& selectedPath,
    const QVector<SleevePatternPath>& documentPaths,
    SleeveDrawingUnit drawingUnit,
    double toleranceMM)
{
    SleevePatternAnalysis result;
    result.expectedSeamAllowanceMM = defaultSeamAllowanceMM();
    result.toleranceMM = qMax(0.0, toleranceMM);
    SleeveAnalyzer analyzer(drawingUnit);
    result.geometry = analyzer.analyze(selectedPath.vertices, selectedPath.closed);
    if (!result.geometry.valid) {
        result.failureReason = result.geometry.failureReason;
        return result;
    }
    return analyze(selectedPath, result.geometry, documentPaths, toleranceMM);
}

SleevePatternAnalysis SleevePatternAnalyzer::analyze(
    const SleevePatternPath& selectedPath,
    const SleeveAnalysis& geometry,
    const QVector<SleevePatternPath>& documentPaths,
    double toleranceMM)
{
    SleevePatternAnalysis result;
    result.geometry = geometry;
    result.expectedSeamAllowanceMM = defaultSeamAllowanceMM();
    result.toleranceMM = qMax(0.0, toleranceMM);
    result.sleevePiece = selectedPath;
    result.sleevePiece.kind = SleeveFeatureKind::Outer;
    if (!geometry.valid) {
        result.failureReason = geometry.failureReason;
        return result;
    }

    const SleeveAnalyzer analyzer(geometry.drawingUnit);
    const double expectedDrawing = analyzer.mmToDrawingUnit(
        result.expectedSeamAllowanceMM);
    const double toleranceDrawing = analyzer.mmToDrawingUnit(result.toleranceMM);
    // A sleeve's cuff construction can sit on the far side of the nominal
    // seam allowance (the supplied patterns use roughly 0.75 in).  Keep the
    // radius bounded by three allowances; the bounding-box and path-distance
    // checks below still prevent unrelated pieces from being absorbed.
    const double radius = qMax(expectedDrawing * 3.0,
                               qMax(toleranceDrawing * 4.0, 1.0e-7));
    result.associationRadiusMM = analyzer.drawingUnitToMM(radius);
    const QVector<QPointF> sleeveSamples = sampledPath(selectedPath);

    for (const SleevePatternPath& candidate : documentPaths) {
        if (sameSource(candidate, selectedPath))
            continue;
        if (isAssociated(candidate, selectedPath, radius))
            result.associatedPaths.append(candidate);
    }

    const QRectF sleeveBounds = pathBounds(sleeveSamples);
    double bestSeamScore = std::numeric_limits<double>::max();
    for (int i = 0; i < result.associatedPaths.size(); ++i) {
        const SleevePatternPath& candidate = result.associatedPaths.at(i);
        if (!closedPath(candidate) || candidate.vertices.size() < 3)
            continue;
        const QVector<QPointF> candidateSamples = sampledPath(candidate);
        const QRectF candidateBounds = pathBounds(candidateSamples);
        QVector<double> distances;
        for (const QPointF& point : sleeveSamples)
            distances.append(pointPathDistance(point, candidateSamples));
        for (const QPointF& point : candidateSamples)
            distances.append(pointPathDistance(point, sleeveSamples));
        const double measuredDrawing = median(distances);
        const bool nearExpected = qAbs(measuredDrawing - expectedDrawing)
                                  <= qMax(expectedDrawing * 0.50,
                                         toleranceDrawing * 4.0);
        const bool enclosing = encloses(candidateBounds, sleeveBounds,
                                        toleranceDrawing * 2.0);
        if ((!nearExpected && !enclosing) || measuredDrawing <= kEpsilon)
            continue;

        const double normalizedMeasuredDrawing = nearExpected
            ? expectedDrawing : measuredDrawing;
        const double score = qAbs(measuredDrawing - expectedDrawing)
                             + (enclosing ? 0.0 : expectedDrawing);
        if (score < bestSeamScore) {
            bestSeamScore = score;
            result.seamAllowance = candidate;
            result.seamAllowance.kind = SleeveFeatureKind::SeamAllowance;
            result.measuredSeamAllowanceMM = analyzer.drawingUnitToMM(
                normalizedMeasuredDrawing);
            result.seamAllowanceDetected = true;
        }
    }

    const QPointF axis = normalized(geometry.sleeveAxis);
    const QPointF transverse = normalized(geometry.cuffUpper - geometry.cuffLower);
    const double cuffWidthDrawing = distance(geometry.cuffUpper,
                                             geometry.cuffLower);
    if (length(axis) <= kEpsilon || length(transverse) <= kEpsilon
        || cuffWidthDrawing <= kEpsilon) {
        result.failureReason = QStringLiteral("Cuff direction is invalid.");
        return result;
    }

    struct CuffCandidate
    {
        CuffCandidate() = default;
        CuffCandidate(const SleevePatternPath& candidatePath,
                      double candidateOffset,
                      double candidateSpan)
            : path(candidatePath), offset(candidateOffset), span(candidateSpan) {}

        SleevePatternPath path;
        double offset = 0.0;
        double span = 0.0;
    };
    QVector<CuffCandidate> cuffCandidates;
    for (const SleevePatternPath& candidate : result.associatedPaths) {
        if (closedPath(candidate) || candidate.vertices.size() < 2)
            continue;
        const QVector<QPointF> samples = sampledPath(candidate);
        if (samples.size() < 2)
            continue;

        const QPointF direction = normalized(samples.last() - samples.first());
        if (length(direction) <= kEpsilon
            || qAbs(dot(direction, transverse)) < 0.90)
            continue;

        QVector<double> axialValues;
        QVector<double> transverseValues;
        for (const QPointF& point : samples) {
            const QPointF relative = point - geometry.cuffCenter;
            axialValues.append(dot(relative, axis));
            transverseValues.append(dot(relative, transverse));
        }
        const double axial = average(axialValues);
        double axialRange = 0.0;
        for (double value : axialValues)
            axialRange = qMax(axialRange, qAbs(value - axial));
        const double span = *std::max_element(transverseValues.begin(),
                                              transverseValues.end())
                            - *std::min_element(transverseValues.begin(),
                                                transverseValues.end());
        if (span < cuffWidthDrawing * 0.40
            || axialRange > qMax(toleranceDrawing * 4.0,
                                 expectedDrawing * 0.40))
            continue;

        cuffCandidates.append(CuffCandidate(candidate, axial, span));
    }

    std::sort(cuffCandidates.begin(), cuffCandidates.end(),
              [](const CuffCandidate& first, const CuffCandidate& second) {
                  return qAbs(first.offset) > qAbs(second.offset);
              });
    for (int i = 0; i < cuffCandidates.size(); ++i) {
        bool duplicate = false;
        const QVector<QPointF> candidateSamples = sampledPath(cuffCandidates.at(i).path);
        for (int j = 0; j < i; ++j) {
            const QVector<QPointF> previousSamples = sampledPath(cuffCandidates.at(j).path);
            if (qAbs(cuffCandidates.at(i).offset - cuffCandidates.at(j).offset)
                    <= toleranceDrawing * 2.0
                && qAbs(cuffCandidates.at(i).span - cuffCandidates.at(j).span)
                    <= toleranceDrawing * 2.0
                && pathDistance(candidateSamples, previousSamples)
                    <= toleranceDrawing * 2.0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;
        SleeveCuffFeature feature;
        feature.source = cuffCandidates.at(i).path;
        feature.kind = result.cuffFeatures.isEmpty()
            ? SleeveFeatureKind::CuffFold : SleeveFeatureKind::CuffStitch;
        feature.source.kind = feature.kind;
        feature.axialOffsetMM = analyzer.drawingUnitToMM(
            cuffCandidates.at(i).offset);
        feature.spanMM = analyzer.drawingUnitToMM(cuffCandidates.at(i).span);
        result.cuffFeatures.append(feature);
    }

    if (!result.cuffFeatures.isEmpty()) {
        result.originalCuffFoldWidthMM = qAbs(
            result.cuffFeatures.first().axialOffsetMM);
        if (result.cuffFeatures.size() > 1) {
            result.originalCuffStitchWidthMM = qAbs(
                result.cuffFeatures.at(1).axialOffsetMM
                - result.cuffFeatures.first().axialOffsetMM);
        }
        result.cuffStitchStructure = QStringLiteral(
            "%1 parallel cuff line(s); fold offset %2 mm; stitch spacing %3 mm")
            .arg(result.cuffFeatures.size())
            .arg(result.originalCuffFoldWidthMM, 0, 'f', 3)
            .arg(result.originalCuffStitchWidthMM, 0, 'f', 3);
    } else {
        result.cuffStitchStructure = QStringLiteral("No parallel cuff fold/stitch lines detected");
    }

    for (SleevePatternPath path : result.associatedPaths) {
        bool isCuff = false;
        for (const SleeveCuffFeature& cuff : result.cuffFeatures) {
            if (sameSource(path, cuff.source)) {
                isCuff = true;
                break;
            }
        }
        if (!isCuff && !sameSource(path, result.seamAllowance)) {
            path.kind = SleeveFeatureKind::Marking;
            result.markingLines.append(path);
        }
    }

    result.valid = true;
    return result;
}

SleevePatternAnalysis SleevePatternAnalyzer::analyzeDocument(
    const QVector<SleevePatternPath>& documentPaths,
    int selectedSourceIndex,
    const QPointF& selectionAnchor,
    SleeveDrawingUnit drawingUnit,
    double toleranceMM)
{
    SleeveAnalyzer analyzer(drawingUnit);
    struct ValidCandidate
    {
        SleevePatternPath path;
        SleeveAnalysis geometry;
        QVector<QPointF> samples;
        QRectF bounds;
        double area = 0.0;
    };

    QVector<ValidCandidate> candidates;
    for (const SleevePatternPath& candidate : documentPaths) {
        const bool closed = closedPath(candidate);
        if (!closed || candidate.vertices.size() < 8)
            continue;
        const SleeveAnalysis geometry = analyzer.analyze(candidate.vertices,
                                                         closed);
        if (!geometry.valid)
            continue;
        ValidCandidate valid;
        valid.path = candidate;
        valid.geometry = geometry;
        valid.samples = sampledPath(candidate);
        valid.bounds = pathBounds(valid.samples);
        valid.area = closedArea(candidate);
        candidates.append(valid);
    }

    SleevePatternPath selected;
    SleeveAnalysis selectedGeometry;
    if (selectedSourceIndex >= 0) {
        for (const ValidCandidate& candidate : candidates) {
            if (candidate.path.sourceIndex == selectedSourceIndex) {
                selected = candidate.path;
                selectedGeometry = candidate.geometry;
                break;
            }
        }
    }

    if (selected.vertices.isEmpty() && !selectionAnchor.isNull()) {
        double bestDistance = std::numeric_limits<double>::max();
        for (const ValidCandidate& candidate : candidates) {
            const double candidateDistance = pathDistance(
                QVector<QPointF>{selectionAnchor}, candidate.samples);
            if (candidateDistance < bestDistance) {
                bestDistance = candidateDistance;
                selected = candidate.path;
                selectedGeometry = candidate.geometry;
            }
        }
    }

    if (selected.vertices.isEmpty()) {
        const double expectedDrawing = analyzer.mmToDrawingUnit(
            defaultSeamAllowanceMM());
        const double toleranceDrawing = analyzer.mmToDrawingUnit(
            qMax(0.0, toleranceMM));
        double bestPairScore = std::numeric_limits<double>::max();
        double bestArea = std::numeric_limits<double>::max();
        for (const ValidCandidate& inner : candidates) {
            double pairScore = std::numeric_limits<double>::max();
            for (const ValidCandidate& outer : candidates) {
                if (inner.path.sourceIndex == outer.path.sourceIndex
                    || outer.area <= inner.area
                    || !encloses(outer.bounds, inner.bounds,
                                 toleranceDrawing * 2.0)) {
                    continue;
                }
                const double measured = boundaryDistance(inner.samples,
                                                         outer.samples);
                const bool nearExpected = qAbs(measured - expectedDrawing)
                    <= qMax(expectedDrawing * 0.50,
                           toleranceDrawing * 4.0);
                if (!nearExpected || measured <= kEpsilon)
                    continue;
                pairScore = qMin(pairScore,
                                 qAbs(measured - expectedDrawing));
            }

            // Prefer the inner member of the closest expected allowance pair.
            // Area is only a tie-breaker, so another nested detail cannot win
            // over a true 0.3125 in sleeve/seam pair.
            const bool sameFinitePairScore = std::isfinite(pairScore)
                && std::isfinite(bestPairScore)
                && qFuzzyCompare(pairScore + 1.0, bestPairScore + 1.0);
            if (pairScore < bestPairScore
                || (sameFinitePairScore && inner.area < bestArea)) {
                bestPairScore = pairScore;
                bestArea = inner.area;
                selected = inner.path;
                selectedGeometry = inner.geometry;
            }
        }

        if (selected.vertices.isEmpty() && !candidates.isEmpty()) {
            const ValidCandidate* fallback = &candidates.first();
            for (const ValidCandidate& candidate : candidates) {
                if (candidate.area > 0.0
                    && (fallback->area <= 0.0
                        || candidate.area < fallback->area)) {
                    fallback = &candidate;
                }
            }
            selected = fallback->path;
            selectedGeometry = fallback->geometry;
        }
    }

    if (selected.vertices.isEmpty()) {
        SleevePatternAnalysis result;
        result.failureReason = QStringLiteral("No valid sleeve piece was found in the document.");
        return result;
    }

    if (!selectedGeometry.valid)
        selectedGeometry = analyzer.analyze(selected.vertices,
                                            closedPath(selected));
    return analyze(selected, selectedGeometry, documentPaths, toleranceMM);
}

QString SleevePatternAnalyzer::featureKindName(SleeveFeatureKind kind)
{
    switch (kind) {
    case SleeveFeatureKind::Outer:
        return QStringLiteral("outer");
    case SleeveFeatureKind::SeamAllowance:
        return QStringLiteral("seam allowance");
    case SleeveFeatureKind::CuffFold:
        return QStringLiteral("cuff fold");
    case SleeveFeatureKind::CuffStitch:
        return QStringLiteral("cuff stitch");
    case SleeveFeatureKind::Marking:
        return QStringLiteral("marking");
    default:
        return QStringLiteral("unknown");
    }
}
