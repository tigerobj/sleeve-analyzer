#ifndef LIBRARYINSERTFACTORINSPECTOR_FACTORINSPECTORDIALOG_H
#define LIBRARYINSERTFACTORINSPECTOR_FACTORINSPECTORDIALOG_H

#include "factorinspectorsnapshot.h"

#include <QDialog>

namespace LibraryInsertFactorInspector {

class FactorInspectorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FactorInspectorDialog(const InspectorSnapshot &snapshot,
                                   QWidget *parent = nullptr);
};

} // namespace LibraryInsertFactorInspector

#endif // LIBRARYINSERTFACTORINSPECTOR_FACTORINSPECTORDIALOG_H
