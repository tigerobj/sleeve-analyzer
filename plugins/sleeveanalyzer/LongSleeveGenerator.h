#ifndef LONGSLEEVEGENERATOR_H
#define LONGSLEEVEGENERATOR_H

#include "SleeveAnalyzer.h"
#include "SleevePatternFeatures.h"
#include "SleeveSideGeometry.h"
#include "SleeveTargetGeometry.h"

#include <QString>
#include <QVector>

class Document_Interface;

struct LongSleeveFeature
{
    SleeveFeatureKind kind = SleeveFeatureKind::Unknown;
    QVector<SleeveVertex> vertices;
    bool closed = false;
};

struct LongSleeveContour
{
    // The actual-size long-sleeve seam line.  The original sleeve-cap
    // vertices and bulges are preserved here.
    QVector<SleeveVertex> vertices;
    // A second, closed cutting outline.  Its two long-sleeve sides are offset
    // from the actual-size seam by the detected short-sleeve allowance; the
    // sleeve-cap profile is joined to the resulting cuff-cut intersections.
    QVector<SleeveVertex> outerVertices;
    QVector<LongSleeveFeature> generatedFeatures;
    int sleeveCapSegmentCount = 0;
    double sleeveCapLengthMM = 0.0;
    double targetSleeveLengthMM = 0.0;
    double targetCuffWidthMM = 0.0;
    double seamAllowanceMM = 0.0;
    double originalCuffFoldWidthMM = 0.0;
    double originalCuffStitchWidthMM = 0.0;
    double originalCuffCutToFoldDistanceMM = 0.0;
    double cuffInsetMM = 0.0;
    bool valid = false;
    QString failureReason;
};

class LongSleeveGenerator
{
public:
    static LongSleeveContour buildContour(
        const QVector<SleeveVertex>& originalVertices,
        const SleeveAnalysis& analysis,
        const TargetCuffGeometry& targetCuff,
        const SleeveSidePreview& sleeveSides);

    static bool generate(Document_Interface* document,
                         const QVector<SleeveVertex>& originalVertices,
                         const SleeveAnalysis& analysis,
                         const TargetCuffGeometry& targetCuff,
                         const SleeveSidePreview& sleeveSides,
                         QString* errorMessage = nullptr);

    static LongSleeveContour buildContour(
        const QVector<SleeveVertex>& originalVertices,
        const SleevePatternAnalysis& pattern,
        const TargetCuffGeometry& targetCuff,
        const SleeveSidePreview& sleeveSides);

    static bool generate(Document_Interface* document,
                         const QVector<SleeveVertex>& originalVertices,
                         const SleevePatternAnalysis& pattern,
                         const TargetCuffGeometry& targetCuff,
                         const SleeveSidePreview& sleeveSides,
                         QString* errorMessage = nullptr);
};

#endif // LONGSLEEVEGENERATOR_H
