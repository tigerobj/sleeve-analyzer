#include "../LongSleeveGenerator.h"
#include "../SleeveAnalyzer.h"
#include "../SleeveGeometry.h"
#include "../SleeveSideGeometry.h"
#include "../SleeveSizeTable.h"
#include "../SleeveTargetGeometry.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QTemporaryFile>
#include <QtMath>
#include <iostream>

namespace {

struct DxfPolyline
{
    QVector<SleeveVertex> vertices;
    bool closed = false;
};

QVector<DxfPolyline> readClassicPolylines(const QString& fileName)
{
    QFile file(fileName);
    QVector<DxfPolyline> result;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    QTextStream stream(&file);
    QVector<QPair<int, QString> > groups;
    while (!stream.atEnd()) {
        const QString codeLine = stream.readLine();
        if (stream.atEnd())
            break;
        const QString valueLine = stream.readLine();
        bool ok = false;
        const int code = codeLine.trimmed().toInt(&ok);
        if (ok)
            groups.append(qMakePair(code, valueLine.trimmed()));
    }

    bool inPolyline = false;
    bool inVertex = false;
    DxfPolyline polyline;
    SleeveVertex vertex;
    bool hasX = false;
    bool hasY = false;

    const auto finishVertex = [&]() {
        if (inVertex && hasX && hasY)
            polyline.vertices.append(vertex);
        inVertex = false;
        vertex = SleeveVertex();
        hasX = false;
        hasY = false;
    };

    for (const QPair<int, QString>& group : groups) {
        if (group.first == 0) {
            if (group.second == QStringLiteral("POLYLINE")) {
                finishVertex();
                inPolyline = true;
                polyline = DxfPolyline();
            } else if (group.second == QStringLiteral("VERTEX") && inPolyline) {
                finishVertex();
                inVertex = true;
            } else if (group.second == QStringLiteral("SEQEND") && inPolyline) {
                finishVertex();
                result.append(polyline);
                inPolyline = false;
            } else if (inVertex) {
                finishVertex();
            }
            continue;
        }
        if (!inPolyline)
            continue;
        if (!inVertex && group.first == 70)
            polyline.closed = (group.second.toInt() & 1) != 0;
        if (inVertex && group.first == 10) {
            vertex.point.setX(group.second.toDouble());
            hasX = true;
        } else if (inVertex && group.first == 20) {
            vertex.point.setY(group.second.toDouble());
            hasY = true;
        } else if (inVertex && group.first == 42) {
            vertex.bulge = group.second.toDouble();
        }
    }
    return result;
}

bool near(double actual, double expected, double tolerance, const char* label)
{
    if (qAbs(actual - expected) <= tolerance)
        return true;
    std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
    return false;
}

QPointF transformed(const QPointF& point, double angle, const QPointF& offset)
{
    const double cosine = qCos(angle);
    const double sine = qSin(angle);
    return QPointF(cosine * point.x() - sine * point.y(),
                   sine * point.x() + cosine * point.y()) + offset;
}

SleevePatternPath patternPath(const QVector<SleeveVertex>& vertices,
                              bool closed,
                              int sourceIndex)
{
    SleevePatternPath path;
    path.vertices = vertices;
    path.closed = closed;
    path.sourceIndex = sourceIndex;
    return path;
}

QVector<SleeveVertex> translatedVertices(const QVector<SleeveVertex>& vertices,
                                         const QPointF& offset)
{
    QVector<SleeveVertex> result = vertices;
    for (SleeveVertex& vertex : result)
        vertex.point += offset;
    return result;
}

double maximumChordDeviation(const QVector<QPointF>& points)
{
    if (points.size() < 3)
        return 0.0;
    const QPointF chord = points.last() - points.first();
    const double chordLength = SleeveGeometry::length(chord);
    double maximum = 0.0;
    for (int i = 1; i + 1 < points.size(); ++i) {
        maximum = qMax(maximum,
                       qAbs(SleeveGeometry::cross(points.at(i) - points.first(),
                                                  chord)) / chordLength);
    }
    return maximum;
}

bool hasNoReverseBend(const QVector<QPointF>& points,
                      const QPointF& axis,
                      const QPointF& transverse)
{
    double bendDirection = 0.0;
    double previousSlope = 0.0;
    for (int i = 1; i < points.size(); ++i) {
        const QPointF segment = points.at(i) - points.at(i - 1);
        const double axial = SleeveGeometry::dot(segment, axis);
        if (axial <= SleeveGeometry::kEpsilon)
            return false;
        const double slope = SleeveGeometry::dot(segment, transverse) / axial;
        if (i > 1) {
            const double bend = slope - previousSlope;
            if (qAbs(bend) > 1.0e-10) {
                if (bendDirection == 0.0)
                    bendDirection = bend;
                else if (bend * bendDirection < 0.0)
                    return false;
            }
        }
        previousSlope = slope;
    }
    return true;
}

bool contourContainsPoint(const LongSleeveContour& contour, const QPointF& point)
{
    for (const SleeveVertex& vertex : contour.vertices) {
        if (SleeveGeometry::distance(vertex.point, point) <= 1.0e-12)
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 2) {
        std::cerr << "usage: SleeveAnalyzerTests <M-Q0419A DXF>\n";
        return 2;
    }

    const QVector<DxfPolyline> polylines = readClassicPolylines(application.arguments().at(1));
    if (polylines.isEmpty()) {
        std::cerr << "No classic POLYLINE entities read from fixture.\n";
        return 2;
    }

    const SleeveAnalyzer analyzer(SleeveDrawingUnit::Inch);
    SleeveAnalysis matched;
    QVector<SleeveVertex> matchedVertices;
    int polylineIndex = 0;
    for (const DxfPolyline& polyline : polylines) {
        const SleeveAnalysis analysis = analyzer.analyze(polyline.vertices, polyline.closed);
        if (analysis.valid
            && SleeveGeometry::distance(analysis.point1, QPointF(4.811, 15.379)) < 0.02
            && SleeveGeometry::distance(analysis.point2, QPointF(4.811, 0.379)) < 0.02) {
            matched = analysis;
            matchedVertices = polyline.vertices;
            break;
        }
        if (polyline.vertices.size() >= 20) {
            std::cerr << "polyline " << polylineIndex
                      << " vertices=" << polyline.vertices.size()
                      << " closed=" << polyline.closed
                      << " first=(" << polyline.vertices.first().point.x() << ','
                      << polyline.vertices.first().point.y() << ") last=("
                      << polyline.vertices.last().point.x() << ','
                      << polyline.vertices.last().point.y() << ')';
            if (analysis.valid) {
                std::cerr << " valid P1=(" << analysis.point1.x() << ','
                          << analysis.point1.y() << ") P2=(" << analysis.point2.x()
                          << ',' << analysis.point2.y() << ')';
            } else {
                std::cerr << " failed: " << analysis.failureReason.toStdString();
            }
            std::cerr << '\n';
        }
        ++polylineIndex;
    }

    if (!matched.valid) {
        std::cerr << "The expected sleeve seam outline was not detected.\n";
        return 1;
    }

    bool ok = true;
    QVector<SleevePatternPath> fixturePaths;
    for (int i = 0; i < polylines.size(); ++i)
        fixturePaths.append(patternPath(polylines.at(i).vertices,
                                        polylines.at(i).closed, i));
    const SleevePatternAnalysis fixturePattern = SleevePatternAnalyzer::analyzeDocument(
        fixturePaths, -1, QPointF(),
        SleeveDrawingUnit::Inch, 0.5);
    ok &= fixturePattern.valid;
    ok &= fixturePattern.seamAllowanceDetected;
    ok &= fixturePattern.cuffFeatures.size() >= 2;
    ok &= near(fixturePattern.measuredSeamAllowanceMM, 7.9375, 0.01,
               "fixture seam allowance");
    ok &= fixturePattern.sleevePiece.sourceIndex == polylineIndex;
    ok &= near(fixturePattern.originalCuffFoldWidthMM, 19.05, 0.01,
               "fixture cuff fold width");

    const TargetCuffGeometry fixtureTarget = SleeveTargetGeometry::calculate(
        fixturePattern.geometry, 627.0, 225.17);
    const SleeveSidePreview fixtureSides = SleeveSideGeometry::calculate(
        fixturePattern.geometry, fixtureTarget);
    const LongSleeveContour fixtureContour = LongSleeveGenerator::buildContour(
        fixturePattern.sleevePiece.vertices, fixturePattern,
        fixtureTarget, fixtureSides);
    if (!fixtureContour.valid) {
        std::cerr << "Fixture long sleeve contour was not built: "
                  << fixtureContour.failureReason.toStdString() << '\n';
        ok = false;
    } else {
        ok &= near(fixtureContour.originalCuffCutToFoldDistanceMM,
                   19.05, 0.01, "fixture cuff cut-to-inner-fold distance");
        ok &= near(fixtureContour.cuffInsetMM, 19.05, 0.01,
                   "fixture cuff inset");
        ok &= SleeveGeometry::isSimpleClosedPolyline(
            fixtureContour.outerVertices);
        if (fixtureContour.outerVertices.size() >= 3) {
            const SleeveAnalyzer fixtureConverter(
                fixturePattern.geometry.drawingUnit);
            const QPointF axis = SleeveGeometry::normalized(
                fixturePattern.geometry.sleeveAxis);
            const double expectedCuffAxial = fixtureConverter.mmToDrawingUnit(
                fixtureContour.cuffInsetMM);
            const int cuffFirst = fixtureContour.outerVertices.size() - 2;
            const int cuffSecond = fixtureContour.outerVertices.size() - 1;
            ok &= near(SleeveGeometry::dot(
                           fixtureContour.outerVertices.at(cuffFirst).point
                               - fixtureTarget.center,
                           axis),
                       expectedCuffAxial, 1.0e-6,
                       "fixture expanded cuff direction first end");
            ok &= near(SleeveGeometry::dot(
                           fixtureContour.outerVertices.at(cuffSecond).point
                               - fixtureTarget.center,
                           axis),
                       expectedCuffAxial, 1.0e-6,
                       "fixture expanded cuff direction second end");
        }
    }

    ok &= near(matched.point3.x(), 8.764694, 0.003, "P3.x");
    ok &= near(matched.point3.y(), 7.879, 0.003, "P3.y");
    ok &= near(matched.cuffCenter.x(), 0.750, 0.003, "cuffCenter.x");
    ok &= near(matched.cuffCenter.y(), 7.879, 0.003, "cuffCenter.y");
    ok &= near(matched.sleeveLengthMM, 203.573, 0.12, "sleeveLengthMM");
    ok &= near(matched.cuffWidthMM, 330.200, 0.08, "cuffWidthMM");
    ok &= near(SleeveGeometry::length(matched.cuffDirection), 1.0,
               1.0e-12, "cuff direction normalized");
    ok &= near(matched.sleeveCapLengthMM, 461.545, 0.12, "sleeveCapLengthMM");
    ok &= near(matched.sleeveCapSegmentCount, 54, 0.0, "sleeveCapSegmentCount");

    const double angle = qDegreesToRadians(73.0);
    const QPointF offset(31.25, -14.75);
    QVector<SleeveVertex> rotated = matchedVertices;
    for (SleeveVertex& vertex : rotated)
        vertex.point = transformed(vertex.point, angle, offset);
    const SleeveAnalysis rotatedAnalysis = analyzer.analyze(rotated, true);
    if (!rotatedAnalysis.valid) {
        std::cerr << "Rotated sleeve was not detected: "
                  << rotatedAnalysis.failureReason.toStdString() << '\n';
        ok = false;
    } else {
        ok &= near(rotatedAnalysis.sleeveLengthMM, matched.sleeveLengthMM,
                   0.01, "rotated sleeve length");
        ok &= near(rotatedAnalysis.cuffWidthMM, matched.cuffWidthMM,
                   0.01, "rotated cuff width");
        ok &= near(SleeveGeometry::distance(rotatedAnalysis.point3,
                                             transformed(matched.point3, angle, offset)),
                   0.0, 0.003, "rotated P3");
    }

    ok &= near(analyzer.mmToDrawingUnit(25.4), 1.0, 1.0e-12,
               "mmToDrawingUnit");
    ok &= near(analyzer.drawingUnitToMM(1.0), 25.4, 1.0e-12,
               "drawingUnitToMM");

    const TargetCuffGeometry target = SleeveTargetGeometry::calculate(
        matched, 627.0, 225.17);
    if (!target.valid || !target.validCuff) {
        std::cerr << "Target cuff geometry was not generated.\n";
        ok = false;
    } else {
        ok &= near(target.measuredSleeveLengthMM, 627.0, 0.001,
                   "target sleeve length");
        ok &= near(target.measuredCuffWidthMM, 225.17, 0.001,
                   "target cuff width");
        ok &= SleeveGeometry::dot(target.center - matched.point3,
                                  matched.cuffCenter - matched.point3) > 0.0;
    }

    const SleeveSidePreview sides = SleeveSideGeometry::calculate(matched, target);
    if (!sides.valid) {
        std::cerr << "Long sleeve sides were not generated: "
                  << sides.failureReason.toStdString() << '\n';
        ok = false;
    } else {
        ok &= sides.upperPoints.size() == 2;
        ok &= sides.lowerPoints.size() == 2;
        ok &= near(SleeveGeometry::distance(sides.upperPoints.first(), matched.point1),
                   0.0, 1.0e-12, "upper side starts at P1");
        ok &= near(SleeveGeometry::distance(sides.lowerPoints.first(), matched.point2),
                   0.0, 1.0e-12, "lower side starts at P2");
        ok &= near(SleeveGeometry::distance(sides.upperPoints.last(), target.upper),
                   0.0, 1.0e-12, "upper side target endpoint");
        ok &= near(SleeveGeometry::distance(sides.lowerPoints.last(), target.lower),
                   0.0, 1.0e-12, "lower side target endpoint");
        ok &= near(maximumChordDeviation(sides.upperPoints),
                   0.0, 1.0e-12, "upper side is straight");
        ok &= near(maximumChordDeviation(sides.lowerPoints),
                   0.0, 1.0e-12, "lower side is straight");
    }

    const LongSleeveContour contour = LongSleeveGenerator::buildContour(
        matchedVertices, matched, target, sides);
    if (!contour.valid) {
        std::cerr << "Long sleeve contour was not built: "
                  << contour.failureReason.toStdString() << '\n';
        ok = false;
    } else {
        ok &= contour.vertices.size() == matched.sleeveCapSegmentCount + 3;
        ok &= contour.sleeveCapSegmentCount == matched.sleeveCapSegmentCount;
        ok &= near(contour.sleeveCapLengthMM, matched.sleeveCapLengthMM,
                   0.001, "copied sleeve cap length");
        ok &= near(contour.targetSleeveLengthMM, 627.0, 0.001,
                   "generated 627 mm sleeve length");
        ok &= near(contour.targetCuffWidthMM, 225.17, 0.001,
                   "generated 225.17 mm cuff width");

        int originalStart = -1;
        for (int i = 0; i < matchedVertices.size(); ++i) {
            if (SleeveGeometry::distance(matchedVertices.at(i).point,
                                         contour.vertices.first().point)
                <= 1.0e-7) {
                originalStart = i;
                break;
            }
        }
        ok &= originalStart >= 0;
        if (originalStart >= 0) {
            for (int i = 0; i < matched.sleeveCapSegmentCount; ++i) {
                const SleeveVertex& original = matchedVertices.at(
                    (originalStart + i) % matchedVertices.size());
                const SleeveVertex& copied = contour.vertices.at(i);
                ok &= near(SleeveGeometry::distance(original.point, copied.point),
                           0.0, 1.0e-12, "copied sleeve cap vertex");
                ok &= near(copied.bulge, original.bulge, 1.0e-12,
                           "copied sleeve cap bulge");
            }
        }
        const QPointF copiedCapEnd = contour.vertices.at(
            matched.sleeveCapSegmentCount).point;
        ok &= SleeveGeometry::distance(copiedCapEnd, matched.point1) <= 1.0e-7
              || SleeveGeometry::distance(copiedCapEnd, matched.point2) <= 1.0e-7;
        ok &= SleeveGeometry::isSimpleClosedPolyline(contour.vertices);
    }

    const QVector<SleeveVertex> simpleRectangle = {
        SleeveVertex(QPointF(0.0, 0.0), 0.0),
        SleeveVertex(QPointF(10.0, 0.0), 0.0),
        SleeveVertex(QPointF(10.0, 10.0), 0.0),
        SleeveVertex(QPointF(0.0, 10.0), 0.0)};
    const QVector<SleeveVertex> bowTie = {
        SleeveVertex(QPointF(0.0, 0.0), 0.0),
        SleeveVertex(QPointF(10.0, 10.0), 0.0),
        SleeveVertex(QPointF(0.0, 10.0), 0.0),
        SleeveVertex(QPointF(10.0, 0.0), 0.0)};
    const QVector<SleeveVertex> duplicateVertex = {
        SleeveVertex(QPointF(0.0, 0.0), 0.0),
        SleeveVertex(QPointF(10.0, 0.0), 0.0),
        SleeveVertex(QPointF(10.0, 0.0), 0.0),
        SleeveVertex(QPointF(0.0, 10.0), 0.0)};
    ok &= SleeveGeometry::isSimpleClosedPolyline(simpleRectangle);
    ok &= !SleeveGeometry::isSimpleClosedPolyline(bowTie);
    ok &= !SleeveGeometry::isSimpleClosedPolyline(duplicateVertex);

    SleevePatternPath selectedPattern = patternPath(matchedVertices, false, 1);
    QVector<SleeveVertex> seamVertices = matchedVertices;
    seamVertices.removeLast();
    const SleevePatternPath seamPattern = patternPath(
        translatedVertices(seamVertices, QPointF(0.3125, 0.0)), true, 2);
    const SleevePatternPath foldPattern = patternPath(
        {SleeveVertex(QPointF(1.0, 1.20), 0.0),
         SleeveVertex(QPointF(1.0, 14.56), 0.0)}, false, 3);
    const SleevePatternPath stitchPattern = patternPath(
        {SleeveVertex(QPointF(1.25, 1.26), 0.0),
         SleeveVertex(QPointF(1.25, 14.50), 0.0)}, false, 4);
    const SleevePatternPath unrelatedPattern = patternPath(
        {SleeveVertex(QPointF(20.0, 20.0), 0.0),
         SleeveVertex(QPointF(21.0, 20.0), 0.0)}, false, 5);
    const QVector<SleevePatternPath> patternPaths = {
        selectedPattern, seamPattern, foldPattern, stitchPattern,
        unrelatedPattern};
    const SleevePatternAnalysis pattern = SleevePatternAnalyzer::analyze(
        selectedPattern, matched, patternPaths, 0.5);
    ok &= pattern.valid;
    ok &= pattern.seamAllowanceDetected;
    ok &= near(pattern.expectedSeamAllowanceMM, 7.9375, 1.0e-12,
               "expected seam allowance");
    ok &= near(pattern.measuredSeamAllowanceMM, 7.9375, 0.01,
               "measured seam allowance");
    ok &= pattern.associatedPaths.size() == 3;
    ok &= pattern.cuffFeatures.size() == 2;
    ok &= near(pattern.originalCuffFoldWidthMM, 12.7, 0.01,
               "original cuff fold width");
    ok &= near(pattern.originalCuffStitchWidthMM, 6.35, 0.01,
               "original cuff stitch spacing");
    ok &= pattern.markingLines.isEmpty();

    const SleevePatternAnalysis selectedFromDocument = SleevePatternAnalyzer::analyzeDocument(
        patternPaths, 1, matchedVertices.first().point,
        SleeveDrawingUnit::Inch, 0.5);
    ok &= selectedFromDocument.valid;
    ok &= SleeveGeometry::distance(selectedFromDocument.geometry.point1,
                                   matched.point1) < 1.0e-7;
    const LongSleeveContour featureContour = LongSleeveGenerator::buildContour(
        matchedVertices, pattern, target, sides);
    ok &= featureContour.valid;
    ok &= featureContour.outerVertices.size() >= 3;
    ok &= SleeveGeometry::isSimpleClosedPolyline(featureContour.outerVertices);
    ok &= featureContour.generatedFeatures.isEmpty();
    ok &= near(featureContour.seamAllowanceMM, 7.9375, 0.01,
               "contour seam allowance");
    ok &= near(featureContour.originalCuffFoldWidthMM, 12.7, 0.01,
               "contour fold width");

    const double strengths[3] = {0.0, 50.0, 100.0};
    QVector<SleeveSidePreview> strengthSides;
    for (int strengthIndex = 0; strengthIndex < 3; ++strengthIndex) {
        const double strength = strengths[strengthIndex];
        const SleeveSidePreview strengthSide = SleeveSideGeometry::calculate(
            matched, target, 7, strength);
        strengthSides.append(strengthSide);
        if (!strengthSide.valid) {
            std::cerr << "Strength " << strength << " side generation failed.\n";
            ok = false;
            continue;
        }

        ok &= strengthSide.upperPoints.size() == 2;
        ok &= strengthSide.lowerPoints.size() == 2;
        ok &= near(SleeveGeometry::distance(strengthSide.upperPoints.first(),
                                             matched.point1),
                   0.0, 1.0e-12, "strength upper P1 unchanged");
        ok &= near(SleeveGeometry::distance(strengthSide.lowerPoints.first(),
                                             matched.point2),
                   0.0, 1.0e-12, "strength lower P2 unchanged");
        ok &= near(SleeveGeometry::distance(strengthSide.upperPoints.last(),
                                             target.upper),
                   0.0, 1.0e-12, "strength upper cuff endpoint");
        ok &= near(SleeveGeometry::distance(strengthSide.lowerPoints.last(),
                                             target.lower),
                   0.0, 1.0e-12, "strength lower cuff endpoint");
        ok &= near(maximumChordDeviation(strengthSide.upperPoints),
                   0.0, 1.0e-12, "strength upper side is straight");
        ok &= near(maximumChordDeviation(strengthSide.lowerPoints),
                   0.0, 1.0e-12, "strength lower side is straight");

        const LongSleeveContour strengthContour = LongSleeveGenerator::buildContour(
            matchedVertices, matched, target, strengthSide);
        ok &= strengthContour.valid;
        if (strengthContour.valid) {
            ok &= near(strengthContour.targetSleeveLengthMM, 627.0, 0.001,
                       "strength sleeve length unchanged");
            ok &= near(strengthContour.targetCuffWidthMM, 225.17, 0.001,
                       "strength cuff width unchanged");
            ok &= near(strengthContour.sleeveCapLengthMM,
                       matched.sleeveCapLengthMM, 0.001,
                       "strength sleeve cap unchanged");
            for (const QPointF& point : strengthSide.upperPoints)
                ok &= contourContainsPoint(strengthContour, point);
            for (const QPointF& point : strengthSide.lowerPoints)
                ok &= contourContainsPoint(strengthContour, point);
        }
        std::cout << "strength=" << strength
                  << " sleeve=627 cuff=225.17 cap="
                  << matched.sleeveCapLengthMM
                  << " straight-side=yes preview=generate\n";
    }

    const TargetCuffGeometry manualTarget = SleeveTargetGeometry::calculate(
        matched, 650.0, 225.17);
    ok &= near(manualTarget.measuredSleeveLengthMM, 650.0, 0.001,
               "manually edited target sleeve length");
    const SleeveSidePreview manualSides = SleeveSideGeometry::calculate(
        matched, manualTarget);
    ok &= manualSides.valid;
    ok &= near(SleeveGeometry::distance(manualSides.upperPoints.last(),
                                        manualTarget.upper),
               0.0, 1.0e-12, "650 mm upper endpoint update");
    const LongSleeveContour manualContour = LongSleeveGenerator::buildContour(
        matchedVertices, matched, manualTarget, manualSides);
    ok &= manualContour.valid;
    ok &= near(manualContour.targetSleeveLengthMM, 650.0, 0.001,
               "generated 650 mm sleeve length");
    ok &= near(manualContour.sleeveCapLengthMM, matched.sleeveCapLengthMM,
               0.001, "650 mm copied sleeve cap length");

    const TargetCuffGeometry tooShortTarget = SleeveTargetGeometry::calculate(
        matched, matched.sleeveLengthMM, 225.17);
    const SleeveSidePreview tooShortSides = SleeveSideGeometry::calculate(
        matched, tooShortTarget);
    const LongSleeveContour rejectedContour = LongSleeveGenerator::buildContour(
        matchedVertices, matched, tooShortTarget, tooShortSides);
    ok &= !rejectedContour.valid;
    ok &= rejectedContour.failureReason
          == QStringLiteral(
              "Target sleeve length must be greater than current sleeve length.");

    const TargetCuffGeometry resizedTarget = SleeveTargetGeometry::calculate(
        matched, 639.7, 234.72);
    const SleeveSidePreview resizedSides = SleeveSideGeometry::calculate(
        matched, resizedTarget);
    ok &= resizedSides.valid;
    ok &= near(SleeveGeometry::distance(resizedSides.upperPoints.last(),
                                        resizedTarget.upper),
               0.0, 1.0e-12, "size/cuff-width upper endpoint update");
    ok &= SleeveGeometry::distance(resizedSides.upperPoints.last(),
                                   sides.upperPoints.last()) > 0.1;
    ok &= SleeveGeometry::distance(matched.point1, QPointF(4.811, 15.379)) < 0.02;
    ok &= SleeveGeometry::distance(matched.point2, QPointF(4.811, 0.379)) < 0.02;
    ok &= near(matched.point3.x(), 8.764694, 0.003,
               "P3 unchanged after target calculation");

    const TargetCuffGeometry noWidthTarget = SleeveTargetGeometry::calculate(
        matched, 627.0, 0.0);
    if (!noWidthTarget.valid || noWidthTarget.validCuff) {
        std::cerr << "Zero cuff width handling is incorrect.\n";
        ok = false;
    }
    ok &= near(noWidthTarget.measuredSleeveLengthMM, 627.0, 0.001,
               "center retained when cuff width is zero");

    const TargetCuffGeometry rotatedTarget = SleeveTargetGeometry::calculate(
        rotatedAnalysis, 627.0, 225.17);
    ok &= near(rotatedTarget.measuredSleeveLengthMM, 627.0, 0.001,
               "rotated target sleeve length");
    ok &= near(rotatedTarget.measuredCuffWidthMM, 225.17, 0.001,
               "rotated target cuff width");
    const SleeveSidePreview rotatedSides = SleeveSideGeometry::calculate(
        rotatedAnalysis, rotatedTarget);
    ok &= rotatedSides.valid;
    ok &= near(SleeveGeometry::distance(rotatedSides.upperPoints.last(),
                                        rotatedTarget.upper),
               0.0, 1.0e-12, "rotated upper side endpoint");

    const QString sizeTablePath = QCoreApplication::applicationDirPath()
                                  + QStringLiteral("/../sleeve_sizes.json");
    SleeveSizeTable sizeTable(sizeTablePath);
    if (!sizeTable.load()) {
        std::cerr << sizeTable.lastError().toStdString() << '\n';
        ok = false;
    } else {
        ok &= sizeTable.sizes().size() == 10;
        ok &= sizeTable.defaultVariant(QStringLiteral("M")) == QStringLiteral("standard");
        ok &= near(sizeTable.sleeveLength(QStringLiteral("M"), QStringLiteral("standard")),
                   627.0, 1.0e-12, "M standard sleeve length");
        ok &= near(sizeTable.sleeveLength(QStringLiteral("M"), QStringLiteral("long")),
                   645.0, 1.0e-12, "M long sleeve length");
        ok &= near(sizeTable.cuffWidth(QStringLiteral("M"), QStringLiteral("long")),
                   0.0, 1.0e-12, "M long cuff width");
    }

    ok &= SleeveSizeTable::sizeFromFileName(QStringLiteral("M-Q0419A-sleeveX2.dxf"))
          == QStringLiteral("M");
    ok &= SleeveSizeTable::sizeFromFileName(QStringLiteral("XL-Q0419A-sleeveX2.dxf"))
          == QStringLiteral("XL");
    ok &= SleeveSizeTable::sizeFromFileName(QStringLiteral("XS-test.dxf"))
          == QStringLiteral("XS");
    ok &= SleeveSizeTable::sizeFromFileName(QStringLiteral("2L-test.dxf"))
          == QStringLiteral("2L");
    ok &= SleeveSizeTable::sizeFromFileName(QStringLiteral("not-a-size-L.dxf")).isEmpty();

    QTemporaryFile invalidFile;
    if (!invalidFile.open()) {
        std::cerr << "Could not create invalid JSON fixture.\n";
        ok = false;
    } else {
        invalidFile.write("{ invalid JSON");
        invalidFile.flush();
        sizeTable.setFilePath(invalidFile.fileName());
        if (sizeTable.reload()) {
            std::cerr << "Invalid JSON was unexpectedly accepted.\n";
            ok = false;
        }
        ok &= near(sizeTable.sleeveLength(QStringLiteral("M"), QStringLiteral("long")),
                   645.0, 1.0e-12, "old data retained after invalid reload");
    }

    if (!ok)
        return 1;
    std::cout << "SleeveAnalyzer tests passed.\n";
    return 0;
}
