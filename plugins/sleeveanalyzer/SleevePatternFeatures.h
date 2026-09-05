#ifndef SLEEVEPATTERNFEATURES_H
#define SLEEVEPATTERNFEATURES_H

#include "SleeveAnalyzer.h"

#include <QRectF>
#include <QString>
#include <QVector>

enum class SleeveFeatureKind
{
    Unknown,
    Outer,
    SeamAllowance,
    CuffFold,
    CuffStitch,
    Marking
};

// A document entity reduced to the geometry needed by the sleeve analyser.
// Keeping this independent of Plug_Entity also makes the association rules
// deterministic and easy to test without a running LibreCAD document.
struct SleevePatternPath
{
    QVector<SleeveVertex> vertices;
    bool closed = false;
    QString layer;
    int sourceIndex = -1;
    SleeveFeatureKind kind = SleeveFeatureKind::Unknown;
};

struct SleeveCuffFeature
{
    SleevePatternPath source;
    SleeveFeatureKind kind = SleeveFeatureKind::Unknown;
    double axialOffsetMM = 0.0;
    double spanMM = 0.0;
};

struct SleevePatternAnalysis
{
    bool valid = false;
    QString failureReason;

    SleeveAnalysis geometry;
    SleevePatternPath sleevePiece;
    SleevePatternPath seamAllowance;
    bool seamAllowanceDetected = false;
    double expectedSeamAllowanceMM = 7.9375;
    double measuredSeamAllowanceMM = 0.0;
    double toleranceMM = 0.5;
    double associationRadiusMM = 0.0;

    QVector<SleevePatternPath> associatedPaths;
    QVector<SleevePatternPath> markingLines;
    QVector<SleeveCuffFeature> cuffFeatures;

    // The farthest parallel line from the actual cuff edge is treated as the
    // fold line.  The next line (when present) is the stitch line; all source
    // lines are retained in cuffFeatures so the exact structure can be
    // reproduced.  This matches the supplied short-sleeve convention where
    // the fold is 0.75 in from the cuff edge.
    double originalCuffFoldWidthMM = 0.0;
    double originalCuffStitchWidthMM = 0.0;
    QString cuffStitchStructure;
};

class SleevePatternAnalyzer
{
public:
    static double defaultSeamAllowanceMM();

    static SleevePatternAnalysis analyze(
        const SleevePatternPath& selectedPath,
        const QVector<SleevePatternPath>& documentPaths,
        SleeveDrawingUnit drawingUnit,
        double toleranceMM = 0.5);

    static SleevePatternAnalysis analyze(
        const SleevePatternPath& selectedPath,
        const SleeveAnalysis& geometry,
        const QVector<SleevePatternPath>& documentPaths,
        double toleranceMM = 0.5);

    // Find the actual (inner) sleeve outline automatically.  When the
    // document contains an outer outline at the expected seam allowance, the
    // inner member of that pair is selected.  The explicit arguments remain
    // available for deterministic tests and backwards-compatible callers.
    static SleevePatternAnalysis analyzeDocument(
        const QVector<SleevePatternPath>& documentPaths,
        int selectedSourceIndex,
        const QPointF& selectionAnchor,
        SleeveDrawingUnit drawingUnit,
        double toleranceMM = 0.5);

    static QString featureKindName(SleeveFeatureKind kind);
};

#endif // SLEEVEPATTERNFEATURES_H
