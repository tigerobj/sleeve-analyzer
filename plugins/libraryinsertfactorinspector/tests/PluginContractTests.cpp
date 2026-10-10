#include "../libraryinsertfactorinspector.h"

#include <QtTest>

using namespace LibraryInsertFactorInspector;

class PluginContractTests : public QObject
{
    Q_OBJECT

private slots:
    void registersSinglePluginsMenuAction();
};

void PluginContractTests::registersSinglePluginsMenuAction()
{
    LC_LibraryInsertFactorInspector plugin;
    QCOMPARE(plugin.name(), QStringLiteral("Library Insert Factor Inspector"));

    const PluginCapabilities capabilities = plugin.getCapabilities();
    QCOMPARE(capabilities.menuEntryPoints.size(), 1);
    QCOMPARE(capabilities.menuEntryPoints.first().menuEntryPoint,
             QStringLiteral("plugins_menu"));
    QCOMPARE(capabilities.menuEntryPoints.first().menuEntryActionName,
             QStringLiteral("Library Insert Factor Inspector"));
}

QTEST_MAIN(PluginContractTests)
#include "PluginContractTests.moc"
