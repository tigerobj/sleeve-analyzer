#ifndef LIBRARYINSERTFACTORINSPECTOR_FACTORINSPECTORSNAPSHOT_H
#define LIBRARYINSERTFACTORINSPECTOR_FACTORINSPECTORSNAPSHOT_H

#include <QSettings>
#include <QVariant>
#include <QWidget>

namespace LibraryInsertFactorInspector {

enum class LiveControlState {
    Found,
    NotFound,
    Hidden,
    Ambiguous
};

struct LiveControlSnapshot {
    LiveControlState state = LiveControlState::NotFound;
    QString value;
};

struct InspectorSnapshot {
    LiveControlSnapshot liveFactor;
    LiveControlSnapshot liveAngle;
    bool persistedFactorExists = false;
    QVariant persistedFactor;
    QSettings::Format settingsFormat = QSettings::NativeFormat;
    QString settingsLocation;
};

InspectorSnapshot inspectLibraryInsert(QWidget *hostWindow, QSettings &settings);

} // namespace LibraryInsertFactorInspector

#endif // LIBRARYINSERTFACTORINSPECTOR_FACTORINSPECTORSNAPSHOT_H
