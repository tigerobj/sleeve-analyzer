#ifndef SLEEVEDEBUGDIALOG_H
#define SLEEVEDEBUGDIALOG_H

#include "SleeveAnalyzer.h"
#include "SleevePatternFeatures.h"
#include "SleeveSideGeometry.h"
#include "SleeveSizeTable.h"
#include "SleeveTargetGeometry.h"

#include <QDialog>
#include <QWidget>

class Document_Interface;

class SleeveDebugWidget : public QWidget
{
public:
    explicit SleeveDebugWidget(const SleeveAnalysis& analysis,
                               QWidget* parent = nullptr);
    void setPreview(const TargetCuffGeometry& targetCuff,
                    const SleeveSidePreview& sleeveSides);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void rebuildBounds();
    QPointF toScreen(const QPointF& drawingPoint) const;
    void drawPath(class QPainter& painter,
                  const QVector<QPointF>& points,
                  const class QPen& pen) const;
    void drawMarker(class QPainter& painter,
                    const QPointF& point,
                    const QString& label,
                    const class QColor& color) const;

    SleeveAnalysis m_analysis;
    TargetCuffGeometry m_targetCuff;
    SleeveSidePreview m_sleeveSides;
    QRectF m_bounds;
};

class SleeveDebugDialog : public QDialog
{
public:
    SleeveDebugDialog(const SleevePatternAnalysis& pattern,
                      const QString& report,
                      SleeveSizeTable* sizeTable,
                      const QString& detectedSize,
                      Document_Interface* document,
                      const QVector<SleeveVertex>& originalVertices,
                      QWidget* parent = nullptr);
    SleeveDebugDialog(const SleeveAnalysis& analysis,
                      const QString& report,
                      SleeveSizeTable* sizeTable,
                      const QString& detectedSize,
                      Document_Interface* document,
                      const QVector<SleeveVertex>& originalVertices,
                      QWidget* parent = nullptr);

private:
    void rebuildSizes(const QString& preferredSize,
                      const QString& preferredVariant);
    void rebuildVariants(const QString& preferredVariant);
    void updateSizeValues();
    void updateTargetPreview();
    void reloadSizeTable();
    void generateLongSleeve();

    SleeveAnalysis m_analysis;
    SleevePatternAnalysis m_pattern;
    QString m_baseReport;
    SleeveSizeTable* m_sizeTable = nullptr;
    Document_Interface* m_document = nullptr;
    QVector<SleeveVertex> m_originalVertices;
    TargetCuffGeometry m_currentTarget;
    SleeveSidePreview m_currentSides;
    SleeveDebugWidget* m_drawing = nullptr;
    class QPlainTextEdit* m_output = nullptr;
    class QComboBox* m_sizeCombo = nullptr;
    class QComboBox* m_variantCombo = nullptr;
    class QDoubleSpinBox* m_sleeveLength = nullptr;
    class QDoubleSpinBox* m_cuffWidth = nullptr;
    class QSpinBox* m_curveStrength = nullptr;
    class QPushButton* m_generateButton = nullptr;
};

#endif // SLEEVEDEBUGDIALOG_H
