#ifndef LIBRARYINSERTFACTORINSPECTOR_PLUGIN_H
#define LIBRARYINSERTFACTORINSPECTOR_PLUGIN_H

#include "qc_plugininterface.h"

#include <QObject>
#include <QWidget>

class Document_Interface;

namespace LibraryInsertFactorInspector {

class LC_LibraryInsertFactorInspector final : public QObject,
                                               public QC_PluginInterface
{
    Q_OBJECT
    Q_INTERFACES(QC_PluginInterface)
    Q_PLUGIN_METADATA(IID LC_DocumentInterface_iid
                      FILE "libraryinsertfactorinspector.json")

public:
    PluginCapabilities getCapabilities() const Q_DECL_OVERRIDE;
    QString name() const Q_DECL_OVERRIDE;
    void execComm(Document_Interface *doc, QWidget *parent,
                  QString command) Q_DECL_OVERRIDE;
};

} // namespace LibraryInsertFactorInspector

#endif // LIBRARYINSERTFACTORINSPECTOR_PLUGIN_H
