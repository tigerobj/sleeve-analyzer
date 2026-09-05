#include "sleeveanalyzerplugin.h"

#include "SleeveAnalyzer.h"
#include "SleeveDebugDialog.h"
#include "SleevePatternFeatures.h"
#include "document_interface.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLineF>
#include <QMessageBox>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QVariant>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

int moduleAnchor = 0;

QString pluginDirectory()
{
#ifdef Q_OS_WIN
    HMODULE module = nullptr;
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&moduleAnchor), &module)) {
        wchar_t path[32768] = {};
        const DWORD length = GetModuleFileNameW(module, path, 32768);
        if (length > 0 && length < 32768)
            return QFileInfo(QString::fromWCharArray(path, int(length))).absolutePath();
    }
#endif
    return QCoreApplication::applicationDirPath() + QStringLiteral("/resources/plugins");
}

QString currentDocumentTitle(QWidget* parent)
{
    if (!parent)
        return QString();
    QMdiArea* area = parent->findChild<QMdiArea*>();
    QMdiSubWindow* window = area ? area->activeSubWindow() : nullptr;
    return window ? window->windowTitle() : QString();
}

SleeveDrawingUnit drawingUnit(Document_Interface* document)
{
    int insunits = 0;
    document->getVariableInt(QStringLiteral("$INSUNITS"), &insunits);
    if (insunits == 1)
        return SleeveDrawingUnit::Inch;
    if (insunits == 4)
        return SleeveDrawingUnit::Millimeter;
    return SleeveDrawingUnit::Unknown;
}

QString analysisReport(const SleeveAnalysis& analysis)
{
    QString report;
    report += QStringLiteral("Sleeve Analysis\n\n");
    report += QStringLiteral("Drawing unit:\n%1\n\n")
        .arg(SleeveAnalyzer::drawingUnitName(analysis.drawingUnit));
    report += QStringLiteral("P1:\nx = %1\ny = %2\n\n")
        .arg(analysis.point1.x(), 0, 'f', 6)
        .arg(analysis.point1.y(), 0, 'f', 6);
    report += QStringLiteral("P2:\nx = %1\ny = %2\n\n")
        .arg(analysis.point2.x(), 0, 'f', 6)
        .arg(analysis.point2.y(), 0, 'f', 6);
    report += QStringLiteral("P3:\nx = %1\ny = %2\n\n")
        .arg(analysis.point3.x(), 0, 'f', 6)
        .arg(analysis.point3.y(), 0, 'f', 6);
    report += QStringLiteral("Sleeve axis:\ndx = %1\ndy = %2\n\n")
        .arg(analysis.sleeveAxis.x(), 0, 'f', 9)
        .arg(analysis.sleeveAxis.y(), 0, 'f', 9);
    report += QStringLiteral("Current sleeve length:\n%1 mm\n\n")
        .arg(analysis.sleeveLengthMM, 0, 'f', 2);
    report += QStringLiteral("Current cuff width:\n%1 mm\n\n")
        .arg(analysis.cuffWidthMM, 0, 'f', 2);
    report += QStringLiteral("Sleeve cap segment count:\n%1\n\n")
        .arg(analysis.sleeveCapSegmentCount);
    report += QStringLiteral("Sleeve cap length:\n%1 mm\n\n")
        .arg(analysis.sleeveCapLengthMM, 0, 'f', 2);
    report += QStringLiteral("Orientation:\n%1\n").arg(analysis.orientation);
    return report;
}

bool pathFromEntity(Plug_Entity* entity,
                    int sourceIndex,
                    SleevePatternPath* path)
{
    if (!entity || !path)
        return false;
    QHash<int, QVariant> data;
    entity->getData(&data);
    const int type = data.value(DPI::ETYPE).toInt();
    path->sourceIndex = sourceIndex;
    path->layer = data.value(DPI::LAYER).toString();
    path->closed = false;
    path->vertices.clear();

    if (type == DPI::POLYLINE) {
        QList<Plug_VertexData> vertices;
        entity->getPolylineData(&vertices);
        for (const Plug_VertexData& vertex : vertices)
            path->vertices.append(SleeveVertex(vertex.point, vertex.bulge));
        path->closed = data.value(DPI::CLOSEPOLY).toInt() != 0;
    } else if (type == DPI::LINE) {
        path->vertices.append(SleeveVertex(
            QPointF(data.value(DPI::STARTX).toDouble(),
                    data.value(DPI::STARTY).toDouble()), 0.0));
        path->vertices.append(SleeveVertex(
            QPointF(data.value(DPI::ENDX).toDouble(),
                    data.value(DPI::ENDY).toDouble()), 0.0));
    } else if (type == DPI::POINT || type == DPI::TEXT || type == DPI::MTEXT) {
        path->vertices.append(SleeveVertex(
            QPointF(data.value(DPI::STARTX).toDouble(),
                    data.value(DPI::STARTY).toDouble()), 0.0));
    } else {
        return false;
    }
    return !path->vertices.isEmpty();
}

void deleteWrappers(QList<Plug_Entity*>* entities)
{
    while (!entities->isEmpty())
        delete entities->takeFirst();
}

} // namespace

QString LC_SleeveAnalyzerPlugin::name() const
{
    return tr("Sleeve Analyzer");
}

PluginCapabilities LC_SleeveAnalyzerPlugin::getCapabilities() const
{
    PluginCapabilities capabilities;
    capabilities.menuEntryPoints
        << PluginMenuLocation(QStringLiteral("plugins_menu"), tr("Sleeve Analyzer"));
    return capabilities;
}

void LC_SleeveAnalyzerPlugin::execComm(Document_Interface* document,
                                       QWidget* parent,
                                       QString command)
{
    Q_UNUSED(command);
    if (!document)
        return;

    m_sizeTable.setFilePath(QDir(pluginDirectory()).filePath(
        QStringLiteral("sleeve_sizes.json")));
    if (!m_sizeTable.reload()) {
        QMessageBox::warning(parent, tr("Sleeve Analyzer"),
                             m_sizeTable.lastError());
        if (m_sizeTable.isEmpty())
            return;
    }

    QList<Plug_Entity*> allEntities;
    QVector<SleevePatternPath> documentPaths;
    if (document->getAllEntities(&allEntities, false)) {
        int sourceIndex = 0;
        for (Plug_Entity* candidate : allEntities) {
            SleevePatternPath path;
            if (pathFromEntity(candidate, sourceIndex, &path))
                documentPaths.append(path);
            ++sourceIndex;
        }
    }
    deleteWrappers(&allEntities);
    const SleevePatternAnalysis pattern = SleevePatternAnalyzer::analyzeDocument(
        documentPaths, -1, QPointF(),
        drawingUnit(document), 0.5);
    if (!pattern.valid) {
        const QString detail = tr("Sleeve analysis failed.\n\n%1")
                                   .arg(pattern.failureReason);
        qWarning().noquote() << detail;
        QMessageBox::warning(parent, tr("Sleeve Analyzer"), detail);
        return;
    }

    const SleeveAnalysis& analysis = pattern.geometry;
    const QVector<SleeveVertex>& vertices = pattern.sleevePiece.vertices;
    QString report = analysisReport(analysis);
    report += QStringLiteral("\n\nPattern feature detection\n\n");
    report += QStringLiteral("Associated entities:\n%1\n\n")
                  .arg(pattern.associatedPaths.size());
    report += QStringLiteral("Association radius:\n%1 mm\n\n")
                  .arg(pattern.associationRadiusMM, 0, 'f', 3);
    report += QStringLiteral("Seam allowance:\nexpected %1 mm; measured %2 mm\n\n")
                  .arg(pattern.expectedSeamAllowanceMM, 0, 'f', 4)
                  .arg(pattern.seamAllowanceDetected
                           ? pattern.measuredSeamAllowanceMM : 0.0,
                       0, 'f', 4);
    report += QStringLiteral(
        "Long-sleeve outline:\ninner actual size; new outer cutting outline +%1 mm\n\n")
                  .arg(pattern.seamAllowanceDetected
                           ? pattern.measuredSeamAllowanceMM
                           : pattern.expectedSeamAllowanceMM,
                       0, 'f', 4);
    report += QStringLiteral("Cuff fold width:\n%1 mm\n\n")
                  .arg(pattern.originalCuffFoldWidthMM, 0, 'f', 3);
    report += QStringLiteral("Cuff stitch spacing:\n%1 mm\n\n")
                  .arg(pattern.originalCuffStitchWidthMM, 0, 'f', 3);
    report += QStringLiteral("Cuff structure:\n%1\n")
                  .arg(pattern.cuffStitchStructure);
    qInfo().noquote() << report;

    const QString detectedSize = SleeveSizeTable::sizeFromFileName(
        currentDocumentTitle(parent));
    SleeveDebugDialog dialog(pattern, report, &m_sizeTable, detectedSize,
                             document, vertices, parent);
    dialog.exec();
}
