/*****************************************************************************/
/*  objectsize.cpp - Show and copy the bounding-box dimensions of a selection */
/*                                                                           */
/*  This library is free software, licensed under the terms of the GNU       */
/*  General Public License as published by the Free Software Foundation,     */
/*  either version 2 of the License, or (at your option) any later version.  */
/*****************************************************************************/

#include "objectsize.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

const double kPi = 3.14159265358979323846;
const double kTwoPi = 2.0 * kPi;
const double kEpsilon = 1.0e-10;

struct Bounds {
    Bounds()
        : valid(false)
        , minX(0.0)
        , minY(0.0)
        , maxX(0.0)
        , maxY(0.0)
    {}

    void add(const QPointF& point)
    {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
            return;
        if (!valid) {
            minX = maxX = point.x();
            minY = maxY = point.y();
            valid = true;
            return;
        }
        minX = std::min(minX, point.x());
        minY = std::min(minY, point.y());
        maxX = std::max(maxX, point.x());
        maxY = std::max(maxY, point.y());
    }

    bool valid;
    double minX;
    double minY;
    double maxX;
    double maxY;
};

double positiveAngle(double angle)
{
    angle = std::fmod(angle, kTwoPi);
    if (angle < 0.0)
        angle += kTwoPi;
    return angle;
}

double directedSweep(double start, double end, bool reversed)
{
    const double raw = reversed ? start - end : end - start;
    if (std::fabs(raw) >= kTwoPi - kEpsilon)
        return kTwoPi;
    double sweep = std::fmod(raw, kTwoPi);
    if (sweep < 0.0)
        sweep += kTwoPi;
    return sweep;
}

bool angleOnArc(double angle, double start, double end, bool reversed)
{
    const double sweep = directedSweep(start, end, reversed);
    if (sweep >= kTwoPi - kEpsilon)
        return true;
    if (sweep <= kEpsilon)
        return false;

    const double offset = reversed
        ? positiveAngle(start - angle)
        : positiveAngle(angle - start);
    return offset <= sweep + kEpsilon;
}

void addArcPoint(Bounds* bounds, const QPointF& center, double radius,
                 double angle)
{
    bounds->add(QPointF(center.x() + radius * std::cos(angle),
                        center.y() + radius * std::sin(angle)));
}

void addCircularArcBounds(Bounds* bounds, const QPointF& center,
                          double radius, double start, double end,
                          bool reversed)
{
    radius = std::fabs(radius);
    if (!std::isfinite(radius))
        return;

    addArcPoint(bounds, center, radius, start);
    addArcPoint(bounds, center, radius, end);

    const double cardinalAngles[] = {0.0, 0.5 * kPi, kPi, 1.5 * kPi};
    for (double angle : cardinalAngles) {
        if (angleOnArc(angle, start, end, reversed))
            addArcPoint(bounds, center, radius, angle);
    }
}

void addBulgeBounds(Bounds* bounds, const QPointF& start,
                    const QPointF& end, double bulge)
{
    if (!std::isfinite(bulge) || std::fabs(bulge) < kEpsilon) {
        bounds->add(start);
        bounds->add(end);
        return;
    }

    const double dx = end.x() - start.x();
    const double dy = end.y() - start.y();
    const double chord = std::hypot(dx, dy);
    if (chord < kEpsilon) {
        bounds->add(start);
        return;
    }

    // This is the same DXF bulge construction used by RS_Arc:
    // bulge = tan(included-angle / 4).
    const double alpha = 4.0 * std::atan(bulge);
    const double halfChord = chord * 0.5;
    const double sine = std::sin(alpha * 0.5);
    if (std::fabs(sine) < kEpsilon) {
        bounds->add(start);
        bounds->add(end);
        return;
    }

    const double radius = std::fabs(halfChord / sine);
    double heightSquared = radius * radius - halfChord * halfChord;
    if (heightSquared < 0.0)
        heightSquared = 0.0;
    double height = std::sqrt(heightSquared);
    if (std::fabs(alpha) > kPi)
        height = -height;

    const double chordAngle = std::atan2(dy, dx);
    const double normalAngle = chordAngle
        + (bulge > 0.0 ? 0.5 * kPi : -0.5 * kPi);
    const QPointF middle((start.x() + end.x()) * 0.5,
                         (start.y() + end.y()) * 0.5);
    const QPointF center(middle.x() + height * std::cos(normalAngle),
                         middle.y() + height * std::sin(normalAngle));
    const double startAngle = std::atan2(start.y() - center.y(),
                                          start.x() - center.x());
    const double endAngle = std::atan2(end.y() - center.y(),
                                      end.x() - center.x());
    addCircularArcBounds(bounds, center, radius, startAngle, endAngle,
                         bulge < 0.0);
}

QPointF ellipsePoint(const QPointF& center, const QPointF& major,
                     double ratio, double parameter)
{
    const double cosine = std::cos(parameter);
    const double sine = std::sin(parameter);
    return QPointF(center.x() + major.x() * cosine
                       - ratio * major.y() * sine,
                   center.y() + major.y() * cosine
                       + ratio * major.x() * sine);
}

void addEllipseBounds(Bounds* bounds, const QHash<int, QVariant>& data)
{
    const QPointF center(data.value(DPI::STARTX).toDouble(),
                         data.value(DPI::STARTY).toDouble());
    const QPointF major(data.value(DPI::ENDX).toDouble(),
                        data.value(DPI::ENDY).toDouble());
    const double ratio = std::fabs(data.value(DPI::HEIGHT).toDouble());
    const double majorRadius = std::hypot(major.x(), major.y());
    if (!std::isfinite(majorRadius) || !std::isfinite(ratio))
        return;

    const double start = data.value(DPI::STARTANGLE).toDouble();
    const double end = data.value(DPI::ENDANGLE).toDouble();
    const bool reversed = data.value(DPI::REVERSED).toBool();
    const bool wholeEllipse = std::fabs(start) < kEpsilon
        && std::fabs(end) < kEpsilon;

    if (wholeEllipse) {
        const double xRadius = std::hypot(major.x(), ratio * major.y());
        const double yRadius = std::hypot(major.y(), ratio * major.x());
        bounds->add(QPointF(center.x() - xRadius, center.y() - yRadius));
        bounds->add(QPointF(center.x() + xRadius, center.y() + yRadius));
        return;
    }

    bounds->add(ellipsePoint(center, major, ratio, start));
    bounds->add(ellipsePoint(center, major, ratio, end));

    // Parameter values where the derivative of X or Y is zero.
    const double xExtreme = std::atan2(-ratio * major.y(), major.x());
    const double yExtreme = std::atan2(ratio * major.x(), major.y());
    const double extrema[] = {
        xExtreme, xExtreme + kPi, yExtreme, yExtreme + kPi
    };
    for (double parameter : extrema) {
        if (angleOnArc(parameter, start, end, reversed))
            bounds->add(ellipsePoint(center, major, ratio, parameter));
    }
}

bool addEntityBounds(Plug_Entity* entity, Bounds* bounds)
{
    if (!entity)
        return false;

    QHash<int, QVariant> data;
    entity->getData(&data);
    const int type = data.value(DPI::ETYPE).toInt();

    switch (type) {
    case DPI::POINT:
        bounds->add(QPointF(data.value(DPI::STARTX).toDouble(),
                            data.value(DPI::STARTY).toDouble()));
        return bounds->valid;

    case DPI::LINE:
        bounds->add(QPointF(data.value(DPI::STARTX).toDouble(),
                            data.value(DPI::STARTY).toDouble()));
        bounds->add(QPointF(data.value(DPI::ENDX).toDouble(),
                            data.value(DPI::ENDY).toDouble()));
        return bounds->valid;

    case DPI::CIRCLE: {
        const QPointF center(data.value(DPI::STARTX).toDouble(),
                             data.value(DPI::STARTY).toDouble());
        const double radius = std::fabs(data.value(DPI::RADIUS).toDouble());
        bounds->add(QPointF(center.x() - radius, center.y() - radius));
        bounds->add(QPointF(center.x() + radius, center.y() + radius));
        return bounds->valid;
    }

    case DPI::ARC: {
        const QPointF center(data.value(DPI::STARTX).toDouble(),
                             data.value(DPI::STARTY).toDouble());
        addCircularArcBounds(bounds, center,
                             data.value(DPI::RADIUS).toDouble(),
                             data.value(DPI::STARTANGLE).toDouble(),
                             data.value(DPI::ENDANGLE).toDouble(),
                             data.value(DPI::REVERSED).toBool());
        return bounds->valid;
    }

    case DPI::ELLIPSE:
        addEllipseBounds(bounds, data);
        return bounds->valid;

    case DPI::POLYLINE: {
        QList<Plug_VertexData> vertices;
        entity->getPolylineData(&vertices);
        if (vertices.isEmpty())
            return false;

        for (const Plug_VertexData& vertex : vertices)
            bounds->add(vertex.point);

        const bool closed = data.value(DPI::CLOSEPOLY).toInt() != 0;
        const int segmentCount = closed ? vertices.size()
                                        : vertices.size() - 1;
        for (int i = 0; i < segmentCount; ++i) {
            const int next = (i + 1) % vertices.size();
            addBulgeBounds(bounds, vertices.at(i).point,
                           vertices.at(next).point, vertices.at(i).bulge);
        }
        return bounds->valid;
    }

    case DPI::IMAGE: {
        const QPointF insertion(data.value(DPI::STARTX).toDouble(),
                                data.value(DPI::STARTY).toDouble());
        const QPointF uVector(data.value(DPI::ENDX).toDouble(),
                              data.value(DPI::ENDY).toDouble());
        const QPointF vVector(data.value(DPI::VVECTORX).toDouble(),
                              data.value(DPI::VVECTORY).toDouble());
        const double width = data.value(DPI::SIZEU).toDouble();
        const double height = data.value(DPI::SIZEV).toDouble();
        const QPointF u(uVector.x() * width, uVector.y() * width);
        const QPointF v(vVector.x() * height, vVector.y() * height);
        bounds->add(insertion);
        bounds->add(insertion + u);
        bounds->add(insertion + v);
        bounds->add(insertion + u + v);
        return bounds->valid;
    }

    default:
        // The public plugin API does not expose the complete geometry for
        // text, inserts, hatches, dimensions, or splines.
        return false;
    }
}

void deleteEntities(QList<Plug_Entity*>* entities)
{
    while (!entities->isEmpty())
        delete entities->takeFirst();
}

struct DrawingUnitInfo
{
    double toMillimeters;
    QString name;
};

DrawingUnitInfo drawingUnitInfo(Document_Interface* document)
{
    int units = 0;
    if (!document || !document->getVariableInt(QStringLiteral("$INSUNITS"),
                                               &units)) {
        // Unitless LibreCAD drawings are treated as millimetres.
        return DrawingUnitInfo{1.0, QStringLiteral("未指定（按 mm）")};
    }

    switch (units) {
    case 1:  return DrawingUnitInfo{25.4, QStringLiteral("英吋")};
    case 2:  return DrawingUnitInfo{304.8, QStringLiteral("英尺")};
    case 3:  return DrawingUnitInfo{1609344.0, QStringLiteral("英里")};
    case 4:  return DrawingUnitInfo{1.0, QStringLiteral("毫米")};
    case 5:  return DrawingUnitInfo{10.0, QStringLiteral("公分")};
    case 6:  return DrawingUnitInfo{1000.0, QStringLiteral("公尺")};
    case 7:  return DrawingUnitInfo{1000000.0, QStringLiteral("公里")};
    case 8:  return DrawingUnitInfo{0.0000254, QStringLiteral("微英吋")};
    case 9:  return DrawingUnitInfo{0.0254, QStringLiteral("密耳")};
    case 10: return DrawingUnitInfo{914.4, QStringLiteral("碼")};
    case 11: return DrawingUnitInfo{1.0e-7, QStringLiteral("埃")};
    case 12: return DrawingUnitInfo{1.0e-6, QStringLiteral("奈米")};
    case 13: return DrawingUnitInfo{0.001, QStringLiteral("微米")};
    case 14: return DrawingUnitInfo{100.0, QStringLiteral("分米")};
    case 15: return DrawingUnitInfo{10000.0, QStringLiteral("十公尺")};
    case 16: return DrawingUnitInfo{100000.0, QStringLiteral("百公尺")};
    case 17: return DrawingUnitInfo{1.0e12, QStringLiteral("十億公尺")};
    case 18: return DrawingUnitInfo{1.495978707e14,
                                    QStringLiteral("天文單位")};
    case 19: return DrawingUnitInfo{9.460730472e18,
                                    QStringLiteral("光年")};
    case 20: return DrawingUnitInfo{3.085677581e19,
                                    QStringLiteral("秒差距")};
    case 21: return DrawingUnitInfo{304.800609601,
                                    QStringLiteral("美制測量英尺")};
    case 22: return DrawingUnitInfo{25.4000508,
                                    QStringLiteral("美制測量英吋")};
    case 23: return DrawingUnitInfo{914.401828803,
                                    QStringLiteral("美制測量碼")};
    case 24: return DrawingUnitInfo{1609347.258,
                                    QStringLiteral("美制測量英里")};
    default: return DrawingUnitInfo{1.0, QStringLiteral("未知（按 mm）")};
    }
}

QString numberString(double value)
{
    if (std::fabs(value) < 0.0000005)
        return QStringLiteral("0");

    QString text = QString::number(value, 'f', 6);
    while (text.contains('.') && text.endsWith('0'))
        text.chop(1);
    if (text.endsWith('.'))
        text.chop(1);
    return text;
}

QString dualUnitString(double value, const DrawingUnitInfo& unit)
{
    const double millimeters = value * unit.toMillimeters;
    const double inches = millimeters / 25.4;
    return QStringLiteral("%1 mm（%2 英吋）")
        .arg(numberString(millimeters), numberString(inches));
}

} // namespace

QString LC_ObjectSizePlugin::name() const
{
    return tr("物件長寬");
}

PluginCapabilities LC_ObjectSizePlugin::getCapabilities() const
{
    PluginCapabilities capabilities;
    capabilities.menuEntryPoints
        << PluginMenuLocation(QStringLiteral("plugins_menu"),
                              tr("物件長寬"));
    return capabilities;
}

void LC_ObjectSizePlugin::execComm(Document_Interface* document,
                                   QWidget* parent,
                                   QString command)
{
    Q_UNUSED(command);
    if (!document)
        return;

    QList<Plug_Entity*> selected;
    const bool selectionCompleted = document->getSelect(
        &selected, tr("請選擇物件，完成後按 Enter"));
    if (!selectionCompleted || selected.isEmpty()) {
        deleteEntities(&selected);
        return;
    }

    Bounds bounds;
    int unsupportedCount = 0;
    for (Plug_Entity* entity : selected) {
        Bounds entityBounds;
        if (!addEntityBounds(entity, &entityBounds)) {
            ++unsupportedCount;
            continue;
        }
        bounds.add(QPointF(entityBounds.minX, entityBounds.minY));
        bounds.add(QPointF(entityBounds.maxX, entityBounds.maxY));
    }

    if (!bounds.valid) {
        QMessageBox::information(
            parent, tr("物件長寬"),
            tr("所選物件沒有可由插件介面讀取的幾何資料。"));
        deleteEntities(&selected);
        return;
    }

    const DrawingUnitInfo unit = drawingUnitInfo(document);
    const QString length = dualUnitString(bounds.maxX - bounds.minX, unit);
    const QString width = dualUnitString(bounds.maxY - bounds.minY, unit);
    const QString copyText = QStringLiteral("長度（X）：%1\n寬度（Y）：%2")
        .arg(length, width);

    QString report = QStringLiteral("選取物件數：%1\n檔案單位（自動判別）：%2\n\n")
        .arg(selected.size())
        .arg(unit.name);
    report += QStringLiteral("外框範圍（軸對齊）\n");
    report += QStringLiteral("長度（X）：%1\n寬度（Y）：%2\n\n")
        .arg(length, width);
    report += QStringLiteral("最小 X：%1\n最大 X：%2\n最小 Y：%3\n最大 Y：%4")
        .arg(dualUnitString(bounds.minX, unit),
             dualUnitString(bounds.maxX, unit),
             dualUnitString(bounds.minY, unit),
             dualUnitString(bounds.maxY, unit));
    if (unsupportedCount > 0) {
        report += QStringLiteral("\n\n未支援幾何的物件：%1")
            .arg(unsupportedCount);
    }

    LC_ObjectSizeDialog dialog(parent);
    dialog.setReport(report, copyText);
    dialog.exec();

    deleteEntities(&selected);
}

LC_ObjectSizeDialog::LC_ObjectSizeDialog(QWidget* parent)
    : QDialog(parent)
    , m_report(new QTextEdit(this))
    , m_status(new QLabel(this))
{
    setWindowTitle(tr("物件長寬"));
    resize(430, 330);

    m_report->setReadOnly(true);
    m_report->setAcceptRichText(false);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QDialogButtonBox* buttons = new QDialogButtonBox(this);
    QPushButton* copyButton = buttons->addButton(
        tr("複製長寬"), QDialogButtonBox::AcceptRole);
    QPushButton* closeButton = buttons->addButton(
        tr("關閉"), QDialogButtonBox::RejectRole);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_report);
    layout->addWidget(m_status);
    layout->addWidget(buttons);

    connect(copyButton, &QPushButton::clicked,
            this, &LC_ObjectSizeDialog::copyDimensions);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
}

void LC_ObjectSizeDialog::setReport(const QString& displayText,
                                    const QString& copyText)
{
    m_report->setPlainText(displayText);
    m_copyText = copyText;
    m_status->clear();
}

void LC_ObjectSizeDialog::copyDimensions()
{
    QApplication::clipboard()->setText(m_copyText);
    m_status->setText(tr("長寬資訊已複製到剪貼簿。"));
}
