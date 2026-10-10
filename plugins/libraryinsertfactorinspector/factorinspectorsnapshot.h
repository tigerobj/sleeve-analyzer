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
    QString persistedFactorSource;
    QSettings::Format settingsFormat = QSettings::NativeFormat;
    QString settingsLocation;
};

struct PersistedFactorOverride {
    bool available = false;
    bool exists = false;
    QVariant value;
    QString source;
};

InspectorSnapshot inspectLibraryInsert(QWidget *hostWindow, QSettings &settings);
InspectorSnapshot inspectLibraryInsert(QWidget *hostWindow, QSettings &settings,
                                        const PersistedFactorOverride &persistedOverride);

} // namespace LibraryInsertFactorInspector

#endif // LIBRARYINSERTFACTORINSPECTOR_FACTORINSPECTORSNAPSHOT_H
