#ifndef SLEEVESIDEGEOMETRY_H
#define SLEEVESIDEGEOMETRY_H

#include "SleeveAnalyzer.h"
#include "SleeveTargetGeometry.h"

#include <QPointF>
#include <QString>
#include <QVector>

struct SleeveSidePreview
{
    QVector<QPointF> upperPoints;
    QVector<QPointF> lowerPoints;
    QPointF upperStartTangent;
    QPointF lowerStartTangent;

    bool valid = false;
    QString failureReason;
};

class SleeveSideGeometry
{
public:
    static SleeveSidePreview calculate(const SleeveAnalysis& analysis,
                                        const TargetCuffGeometry& targetCuff,
                                        int segmentCount = 7,
                                        double curveStrength = 50.0);
};

#endif // SLEEVESIDEGEOMETRY_H
