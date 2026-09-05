#include "SleeveDebugDialog.h"

#include "LongSleeveGenerator.h"
#include "SleeveGeometry.h"

#include <QDialogButtonBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <limits>

SleeveDebugWidget::SleeveDebugWidget(const SleeveAnalysis& analysis,
                                     QWidget* parent)
    : QWidget(parent), m_analysis(analysis)
{
    setMinimumSize(560, 520);
    rebuildBounds();
}

void SleeveDebugWidget::setPreview(const TargetCuffGeometry& targetCuff,
                                   const SleeveSidePreview& sleeveSides)
{
    m_targetCuff = targetCuff;
    m_sleeveSides = sleeveSides;
    rebuildBounds();
    update();
}

void SleeveDebugWidget::rebuildBounds()
{
    double minimumX = std::numeric_limits<double>::max();
    double minimumY = std::numeric_limits<double>::max();
    double maximumX = std::numeric_limits<double>::lowest();
    double maximumY = std::numeric_limits<double>::lowest();
    const auto includePoint = [&](const QPointF& point) {
        minimumX = qMin(minimumX, point.x());
        minimumY = qMin(minimumY, point.y());
        maximumX = qMax(maximumX, point.x());
        maximumY = qMax(maximumY, point.y());
    };
    for (const QPointF& point : m_analysis.sampledOutline)
        includePoint(point);
    if (m_targetCuff.valid) {
        includePoint(m_targetCuff.center);
        if (m_targetCuff.validCuff) {
            includePoint(m_targetCuff.upper);
            includePoint(m_targetCuff.lower);
        }
    }
    if (m_sleeveSides.valid) {
        for (const QPointF& point : m_sleeveSides.upperPoints)
            includePoint(point);
        for (const QPointF& point : m_sleeveSides.lowerPoints)
            includePoint(point);
    }
    if (minimumX <= maximumX && minimumY <= maximumY)
        m_bounds = QRectF(QPointF(minimumX, minimumY),
                          QPointF(maximumX, maximumY));
}

QPointF SleeveDebugWidget::toScreen(const QPointF& drawingPoint) const
{
    const double margin = 38.0;
    const double availableWidth = qMax(1.0, width() - 2.0 * margin);
    const double availableHeight = qMax(1.0, height() - 2.0 * margin);
    const double drawingWidth = qMax(SleeveGeometry::kEpsilon, m_bounds.width());
    const double drawingHeight = qMax(SleeveGeometry::kEpsilon, m_bounds.height());
    const double scale = qMin(availableWidth / drawingWidth,
                              availableHeight / drawingHeight);
    const double left = (width() - drawingWidth * scale) * 0.5;
    const double top = (height() - drawingHeight * scale) * 0.5;
    return QPointF(left + (drawingPoint.x() - m_bounds.left()) * scale,
                   top + (m_bounds.bottom() - drawingPoint.y()) * scale);
}

void SleeveDebugWidget::drawPath(QPainter& painter,
                                 const QVector<QPointF>& points,
                                 const QPen& pen) const
{
    if (points.size() < 2)
        return;
    painter.setPen(pen);
    QPainterPath path(toScreen(points.first()));
    for (int i = 1; i < points.size(); ++i)
        path.lineTo(toScreen(points.at(i)));
    painter.drawPath(path);
}

void SleeveDebugWidget::drawMarker(QPainter& painter,
                                   const QPointF& point,
                                   const QString& label,
                                   const QColor& color) const
{
    const QPointF screen = toScreen(point);
    painter.setPen(QPen(color, 2.0));
    painter.setBrush(color);
    painter.drawEllipse(screen, 4.5, 4.5);
    painter.setBrush(Qt::NoBrush);
    painter.drawText(screen + QPointF(7.0, -7.0), label);
}

void SleeveDebugWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(30, 33, 38));

    drawPath(painter, m_analysis.sampledOutline, QPen(QColor(150, 155, 165), 1.2));
    drawPath(painter, m_analysis.sampledSleeveCap, QPen(QColor(255, 90, 210), 3.2));
    drawPath(painter, m_analysis.sampledCuff, QPen(QColor(60, 190, 255), 3.2));

    const QPointF axisStart = toScreen(m_analysis.point3);
    const QPointF axisEnd = toScreen(m_analysis.cuffCenter);
    painter.setPen(QPen(QColor(75, 230, 130), 2.0, Qt::DashLine));
    painter.drawLine(axisStart, axisEnd);
    const QPointF screenDirection = SleeveGeometry::normalized(axisEnd - axisStart);
    const QPointF normal = SleeveGeometry::perpendicularLeft(screenDirection);
    painter.drawLine(axisEnd, axisEnd - screenDirection * 12.0 + normal * 5.0);
    painter.drawLine(axisEnd, axisEnd - screenDirection * 12.0 - normal * 5.0);
    painter.drawText((axisStart + axisEnd) * 0.5 + QPointF(8.0, -8.0),
                     QStringLiteral("Sleeve Axis"));

    drawMarker(painter, m_analysis.point1, QStringLiteral("P1"), QColor(255, 205, 70));
    drawMarker(painter, m_analysis.point2, QStringLiteral("P2"), QColor(255, 205, 70));
    drawMarker(painter, m_analysis.point3, QStringLiteral("P3"), QColor(255, 115, 75));
    drawMarker(painter, m_analysis.cuffCenter, QStringLiteral("Cuff center"), QColor(60, 190, 255));

    if (m_targetCuff.valid) {
        const QColor targetColor(255, 155, 55);
        drawMarker(painter, m_targetCuff.center,
                   QStringLiteral("Target Cuff Center"), targetColor);
        if (m_targetCuff.validCuff) {
            painter.setPen(QPen(targetColor, 3.2));
            painter.drawLine(toScreen(m_targetCuff.upper),
                             toScreen(m_targetCuff.lower));
            drawMarker(painter, m_targetCuff.upper,
                       QStringLiteral("Target Cuff Upper"), targetColor);
            drawMarker(painter, m_targetCuff.lower,
                       QStringLiteral("Target Cuff Lower"), targetColor);
            painter.drawText((toScreen(m_targetCuff.upper)
                              + toScreen(m_targetCuff.lower)) * 0.5
                                 + QPointF(8.0, 18.0),
                             QStringLiteral("Target Cuff"));
        }
    }

    if (m_sleeveSides.valid) {
        const QColor upperSideColor(180, 120, 255);
        const QColor lowerSideColor(255, 220, 80);
        drawPath(painter, m_sleeveSides.upperPoints,
                 QPen(upperSideColor, 2.6));
        drawPath(painter, m_sleeveSides.lowerPoints,
                 QPen(lowerSideColor, 2.6));
        const int upperMiddle = m_sleeveSides.upperPoints.size() / 2;
        const int lowerMiddle = m_sleeveSides.lowerPoints.size() / 2;
        painter.setPen(upperSideColor);
        painter.drawText(toScreen(m_sleeveSides.upperPoints.at(upperMiddle))
                             + QPointF(8.0, -7.0),
                         QStringLiteral("Upper Long Sleeve Side"));
        painter.setPen(lowerSideColor);
        painter.drawText(toScreen(m_sleeveSides.lowerPoints.at(lowerMiddle))
                             + QPointF(8.0, 16.0),
                         QStringLiteral("Lower Long Sleeve Side"));
    }

    painter.setPen(QColor(225, 225, 225));
    painter.drawText(QPointF(14.0, 22.0), QStringLiteral("Sleeve Analyzer — read-only debug view"));
}

namespace {

SleevePatternAnalysis legacyPattern(const SleeveAnalysis& analysis,
                                    const QVector<SleeveVertex>& vertices)
{
    SleevePatternAnalysis pattern;
    pattern.valid = analysis.valid;
    pattern.geometry = analysis;
    pattern.sleevePiece.vertices = vertices;
    pattern.sleevePiece.closed = true;
    pattern.sleevePiece.kind = SleeveFeatureKind::Outer;
    return pattern;
}

} // namespace

SleeveDebugDialog::SleeveDebugDialog(const SleeveAnalysis& analysis,
                                     const QString& report,
                                     SleeveSizeTable* sizeTable,
                                     const QString& detectedSize,
                                     Document_Interface* document,
                                     const QVector<SleeveVertex>& originalVertices,
                                     QWidget* parent)
    : SleeveDebugDialog(legacyPattern(analysis, originalVertices), report,
                        sizeTable, detectedSize, document, originalVertices,
                        parent)
{
}

SleeveDebugDialog::SleeveDebugDialog(const SleevePatternAnalysis& pattern,
                                     const QString& report,
                                     SleeveSizeTable* sizeTable,
                                     const QString& detectedSize,
                                     Document_Interface* document,
                                     const QVector<SleeveVertex>& originalVertices,
                                     QWidget* parent)
    : QDialog(parent), m_analysis(pattern.geometry), m_pattern(pattern),
      m_baseReport(report),
      m_sizeTable(sizeTable), m_document(document),
      m_originalVertices(originalVertices)
{
    setWindowTitle(tr("Sleeve Analyzer"));
    resize(900, 620);

    m_drawing = new SleeveDebugWidget(m_analysis, this);
    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setPlainText(report);
    m_output->setMinimumWidth(285);

    m_sizeCombo = new QComboBox(this);
    m_variantCombo = new QComboBox(this);
    m_sleeveLength = new QDoubleSpinBox(this);
    m_cuffWidth = new QDoubleSpinBox(this);
    m_curveStrength = new QSpinBox(this);
    for (QDoubleSpinBox* value : {m_sleeveLength, m_cuffWidth}) {
        value->setDecimals(2);
        value->setRange(0.0, 100000.0);
        value->setSuffix(tr(" mm"));
    }
    m_cuffWidth->setReadOnly(true);
    m_cuffWidth->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_curveStrength->setRange(0, 100);
    m_curveStrength->setValue(50);

    QFormLayout* parameters = new QFormLayout;
    parameters->addRow(tr("尺寸："), m_sizeCombo);
    parameters->addRow(tr("版本："), m_variantCombo);
    parameters->addRow(tr("袖長："), m_sleeveLength);
    parameters->addRow(tr("袖口寬度："), m_cuffWidth);
    parameters->addRow(tr("袖身曲線強度："), m_curveStrength);

    QPushButton* reloadButton = new QPushButton(tr("重新載入尺寸表"), this);
    m_generateButton = new QPushButton(tr("產生長袖"), this);
    m_generateButton->setEnabled(false);
    connect(reloadButton, &QPushButton::clicked, this,
            [this]() { reloadSizeTable(); });
    connect(m_generateButton, &QPushButton::clicked, this,
            [this]() { generateLongSleeve(); });
    connect(m_sizeCombo, &QComboBox::currentTextChanged, this,
            [this](const QString&) { rebuildVariants(QString()); });
    connect(m_variantCombo, &QComboBox::currentTextChanged, this,
            [this](const QString&) { updateSizeValues(); });
    connect(m_sleeveLength,
            static_cast<void (QDoubleSpinBox::*)(double)>(
                &QDoubleSpinBox::valueChanged),
            this, [this](double) { updateTargetPreview(); });
    connect(m_cuffWidth,
            static_cast<void (QDoubleSpinBox::*)(double)>(
                &QDoubleSpinBox::valueChanged),
            this, [this](double) { updateTargetPreview(); });
    connect(m_curveStrength,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, [this](int) { updateTargetPreview(); });

    QVBoxLayout* details = new QVBoxLayout;
    details->addLayout(parameters);
    details->addWidget(reloadButton);
    details->addWidget(m_generateButton);
    details->addWidget(m_output, 1);

    QHBoxLayout* content = new QHBoxLayout;
    content->addWidget(m_drawing, 3);
    content->addLayout(details, 2);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close,
                                                      Qt::Horizontal, this);
    connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(content);
    layout->addWidget(buttons);

    rebuildSizes(detectedSize, QString());
}

void SleeveDebugDialog::rebuildSizes(const QString& preferredSize,
                                     const QString& preferredVariant)
{
    const QSignalBlocker blocker(m_sizeCombo);
    m_sizeCombo->clear();
    m_sizeCombo->addItems(m_sizeTable->sizes());
    int index = m_sizeCombo->findText(preferredSize);
    if (index < 0 && m_sizeCombo->count() > 0)
        index = 0;
    m_sizeCombo->setCurrentIndex(index);
    rebuildVariants(preferredVariant);
}

void SleeveDebugDialog::rebuildVariants(const QString& preferredVariant)
{
    const QString size = m_sizeCombo->currentText();
    const QSignalBlocker blocker(m_variantCombo);
    m_variantCombo->clear();
    m_variantCombo->addItems(m_sizeTable->variants(size));
    int index = m_variantCombo->findText(preferredVariant);
    if (index < 0)
        index = m_variantCombo->findText(m_sizeTable->defaultVariant(size));
    if (index < 0 && m_variantCombo->count() > 0)
        index = 0;
    m_variantCombo->setCurrentIndex(index);
    updateSizeValues();
}

void SleeveDebugDialog::updateSizeValues()
{
    const QString size = m_sizeCombo->currentText();
    const QString variant = m_variantCombo->currentText();
    const QSignalBlocker lengthBlocker(m_sleeveLength);
    const QSignalBlocker widthBlocker(m_cuffWidth);
    if (size.isEmpty() || variant.isEmpty()) {
        m_sleeveLength->setValue(0.0);
        m_cuffWidth->setValue(0.0);
        updateTargetPreview();
        return;
    }
    m_sleeveLength->setValue(m_sizeTable->sleeveLength(size, variant));
    m_cuffWidth->setValue(m_sizeTable->cuffWidth(size, variant));
    updateTargetPreview();
}

void SleeveDebugDialog::updateTargetPreview()
{
    const TargetCuffGeometry target = SleeveTargetGeometry::calculate(
        m_analysis, m_sleeveLength->value(), m_cuffWidth->value());
    const SleeveSidePreview sleeveSides = SleeveSideGeometry::calculate(
        m_analysis, target, 7, m_curveStrength->value());
    m_currentTarget = target;
    m_currentSides = sleeveSides;
    m_drawing->setPreview(target, sleeveSides);
    const bool longerThanOriginal = target.targetSleeveLengthMM
                                    > m_analysis.sleeveLengthMM;
    m_generateButton->setEnabled(
        m_analysis.valid && target.valid && target.validCuff
        && sleeveSides.valid && target.targetCuffWidthMM > 0.0
        && longerThanOriginal);

    QString targetReport;
    targetReport += QStringLiteral("\n\nTarget Cuff Preview\n\n");
    targetReport += QStringLiteral("Selected size:\n%1\n\n")
                        .arg(m_sizeCombo->currentText());
    targetReport += QStringLiteral("Selected variant:\n%1\n\n")
                        .arg(m_variantCombo->currentText());
    targetReport += QStringLiteral("Target sleeve length:\n%1 mm\n\n")
                        .arg(target.targetSleeveLengthMM, 0, 'f', 2);
    targetReport += QStringLiteral("Target cuff width:\n%1 mm\n\n")
                        .arg(target.targetCuffWidthMM, 0, 'f', 2);
    targetReport += QStringLiteral("Rebuilt cuff fold width:\n%1 mm\n\n")
                        .arg(m_pattern.originalCuffFoldWidthMM, 0, 'f', 3);
    targetReport += QStringLiteral("Rebuilt cuff lines:\n%1\n\n")
                        .arg(m_pattern.cuffFeatures.size());

    if (!target.valid) {
        targetReport += target.failureReason;
        m_output->setPlainText(m_baseReport + targetReport);
        return;
    }

    const auto pointText = [](const QString& label, const QPointF& point) {
        return QStringLiteral("%1:\nx = %2\ny = %3\n\n")
            .arg(label)
            .arg(point.x(), 0, 'f', 6)
            .arg(point.y(), 0, 'f', 6);
    };
    targetReport += pointText(QStringLiteral("Target cuff center"), target.center);
    if (target.validCuff) {
        targetReport += pointText(QStringLiteral("Target cuff upper"), target.upper);
        targetReport += pointText(QStringLiteral("Target cuff lower"), target.lower);
    } else {
        targetReport += QStringLiteral(
            "Cuff width is not configured for this size/variant.\n\n");
    }
    targetReport += QStringLiteral("Measured target sleeve length:\n%1 mm\n\n")
                        .arg(target.measuredSleeveLengthMM, 0, 'f', 2);
    if (target.validCuff) {
        targetReport += QStringLiteral("Measured target cuff width:\n%1 mm\n")
                            .arg(target.measuredCuffWidthMM, 0, 'f', 2);
    }
    targetReport += QStringLiteral("\n\nLong Sleeve Side Preview\n\n");
    if (sleeveSides.valid) {
        targetReport += QStringLiteral(
            "Upper Long Sleeve Side: %1 segments\n"
            "Lower Long Sleeve Side: %2 segments\n"
            "Start directions: original short-sleeve tangents\n")
                            .arg(sleeveSides.upperPoints.size() - 1)
                            .arg(sleeveSides.lowerPoints.size() - 1);
    } else {
        targetReport += sleeveSides.failureReason;
    }
    if (!longerThanOriginal) {
        targetReport += QStringLiteral(
            "\nTarget sleeve length must be greater than current sleeve length.\n");
    }
    m_output->setPlainText(m_baseReport + targetReport);
}

void SleeveDebugDialog::generateLongSleeve()
{
    QString error;
    if (!LongSleeveGenerator::generate(m_document, m_originalVertices,
                                       m_pattern, m_currentTarget,
                                       m_currentSides, &error)) {
        QMessageBox::warning(this, tr("Sleeve Analyzer"), error);
    }
}

void SleeveDebugDialog::reloadSizeTable()
{
    const QString previousSize = m_sizeCombo->currentText();
    const QString previousVariant = m_variantCombo->currentText();
    if (!m_sizeTable->reload()) {
        QMessageBox::warning(this, tr("Sleeve Analyzer"),
                             m_sizeTable->lastError());
        return;
    }
    rebuildSizes(previousSize, previousVariant);
}
