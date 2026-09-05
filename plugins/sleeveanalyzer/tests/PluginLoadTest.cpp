#include "qc_plugininterface.h"

#include <QCoreApplication>
#include <QPluginLoader>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 2) {
        std::cerr << "usage: PluginLoadTest <sleeveanalyzer dll>\n";
        return 2;
    }

    QPluginLoader loader(application.arguments().at(1));
    QObject* instance = loader.instance();
    if (!instance) {
        std::cerr << loader.errorString().toStdString() << '\n';
        return 1;
    }
    QC_PluginInterface* plugin = qobject_cast<QC_PluginInterface*>(instance);
    if (!plugin) {
        std::cerr << "Plugin does not expose QC_PluginInterface/1.0.\n";
        return 1;
    }
    std::cout << plugin->name().toStdString() << " loaded successfully.\n";
    return 0;
}
