/*****************************************************************************/
/*  objectsize.h - Show and copy the bounding-box dimensions of a selection  */
/*                                                                           */
/*  This library is free software, licensed under the terms of the GNU       */
/*  General Public License as published by the Free Software Foundation,     */
/*  either version 2 of the License, or (at your option) any later version.  */
/*****************************************************************************/

#ifndef LC_OBJECTSIZE_H
#define LC_OBJECTSIZE_H

#include "document_interface.h"
#include "qc_plugininterface.h"

#include <QDialog>

class QLabel;
class QTextEdit;

class LC_ObjectSizePlugin : public QObject, public QC_PluginInterface
{
    Q_OBJECT
    Q_INTERFACES(QC_PluginInterface)
    Q_PLUGIN_METADATA(IID LC_DocumentInterface_iid FILE "objectsize.json")

public:
    PluginCapabilities getCapabilities() const Q_DECL_OVERRIDE;
    QString name() const Q_DECL_OVERRIDE;
    void execComm(Document_Interface* document,
                  QWidget* parent,
                  QString command) Q_DECL_OVERRIDE;
};

class LC_ObjectSizeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LC_ObjectSizeDialog(QWidget* parent = nullptr);
    void setReport(const QString& displayText, const QString& copyText);

private slots:
    void copyDimensions();

private:
    QTextEdit* m_report;
    QLabel* m_status;
    QString m_copyText;
};

#endif // LC_OBJECTSIZE_H
