#include "libraryinsertfactorinspector.h"

#include "factorinspectordialog.h"
#include "factorinspectorsnapshot.h"

#include <QSettings>

namespace LibraryInsertFactorInspector {

QString LC_LibraryInsertFactorInspector::name() const
{
    return tr("Library Insert Factor Inspector");
}

PluginCapabilities LC_LibraryInsertFactorInspector::getCapabilities() const
{
    PluginCapabilities capabilities;
    capabilities.menuEntryPoints
        << PluginMenuLocation(QStringLiteral("plugins_menu"), name());
    return capabilities;
}

void LC_LibraryInsertFactorInspector::execComm(Document_Interface *doc,
                                               QWidget *parent,
                                               QString command)
{
    Q_UNUSED(doc);
    Q_UNUSED(command);

    QSettings settings(QStringLiteral("LibreCAD"), QStringLiteral("LibreCAD"));
    const InspectorSnapshot snapshot = inspectLibraryInsert(parent, settings);
    FactorInspectorDialog dialog(snapshot, parent);
    dialog.exec();
}

} // namespace LibraryInsertFactorInspector
