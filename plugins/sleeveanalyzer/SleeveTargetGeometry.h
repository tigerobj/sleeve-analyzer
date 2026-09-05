#ifndef SLEEVETARGETGEOMETRY_H
#define SLEEVETARGETGEOMETRY_H

#include "SleeveAnalyzer.h"

#include <QPointF>
#include <QString>

struct TargetCuffGeometry
{
    QPointF center;
    QPointF upper;
    QPointF lower;

    double targetSleeveLengthMM = 0.0;
    double targetCuffWidthMM = 0.0;
    double measuredSleeveLengthMM = 0.0;
    double measuredCuffWidthMM = 0.0;

    bool valid = false;
    bool validCuff = false;
    QString failureReason;
};

class SleeveTargetGeometry
{
public:
    static TargetCuffGeometry calculate(const SleeveAnalysis& analysis,
                                        double targetSleeveLengthMM,
                                        double targetCuffWidthMM);
};

#endif // SLEEVETARGETGEOMETRY_H
