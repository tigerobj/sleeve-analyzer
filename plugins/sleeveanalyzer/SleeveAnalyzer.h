#ifndef SLEEVEANALYZER_H
#define SLEEVEANALYZER_H

#include <QPointF>
#include <QString>
#include <QVector>

struct SleeveVertex
{
    SleeveVertex() = default;
    SleeveVertex(const QPointF& vertexPoint, double vertexBulge)
        : point(vertexPoint), bulge(vertexBulge) {}

    QPointF point;
    double bulge = 0.0;
};

enum class SleeveDrawingUnit
{
    Unknown,
    Inch,
    Millimeter
};

struct SleeveAnalysis
{
    bool valid = false;
    QString failureReason;
    SleeveDrawingUnit drawingUnit = SleeveDrawingUnit::Unknown;

    QPointF point1;
    QPointF point2;
    QPointF point3;

    QPointF cuffUpper;
    QPointF cuffLower;
    QPointF cuffCenter;
    QPointF sleeveAxis;
    QPointF cuffDirection;

    double sleeveLengthMM = 0.0;
    double cuffWidthMM = 0.0;
    double sleeveCapLengthMM = 0.0;
    // Signed axial projection of the farthest sleeve-cap point from P3.
    // A negative value means that the cap extends beyond P3, opposite the
    // cap-to-cuff axis.  Target geometry uses this to make the generated
    // actual-size contour span the requested sleeve length.
    double sleeveCapMinimumAxialMM = 0.0;
    int sleeveCapSegmentCount = 0;

    QVector<QPointF> sampledOutline;
    QVector<QPointF> sampledSleeveCap;
    QVector<QPointF> sampledCuff;
    QString orientation;
};

class SleeveAnalyzer
{
public:
    explicit SleeveAnalyzer(SleeveDrawingUnit drawingUnit);

    SleeveAnalysis analyze(const QVector<SleeveVertex>& vertices,
                           bool closed) const;

    double mmToDrawingUnit(double mm) const;
    double drawingUnitToMM(double value) const;
    static QString drawingUnitName(SleeveDrawingUnit unit);

private:
    SleeveDrawingUnit m_drawingUnit;
};

#endif // SLEEVEANALYZER_H
