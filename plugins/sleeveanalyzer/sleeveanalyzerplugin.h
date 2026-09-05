#ifndef SLEEVEANALYZERPLUGIN_H
#define SLEEVEANALYZERPLUGIN_H

#include "qc_plugininterface.h"
#include "SleeveSizeTable.h"

class LC_SleeveAnalyzerPlugin : public QObject, public QC_PluginInterface
{
    Q_OBJECT
    Q_INTERFACES(QC_PluginInterface)
    Q_PLUGIN_METADATA(IID LC_DocumentInterface_iid FILE "sleeveanalyzer.json")

public:
    PluginCapabilities getCapabilities() const Q_DECL_OVERRIDE;
    QString name() const Q_DECL_OVERRIDE;
    void execComm(Document_Interface* document,
                  QWidget* parent,
                  QString command) Q_DECL_OVERRIDE;

private:
    SleeveSizeTable m_sizeTable;
};

#endif // SLEEVEANALYZERPLUGIN_H
