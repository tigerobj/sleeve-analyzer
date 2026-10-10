#include "factorinspectorsnapshot.h"

#include <QLineEdit>
#include <QList>
#include <QStringList>

namespace LibraryInsertFactorInspector {
namespace {

const char kLibraryInsertOptionsClass[] = "QG_LibraryInsertOptions";

struct OptionsPanes {
    QList<QWidget *> visible;
    bool hasHidden = false;
};

OptionsPanes findLibraryInsertPanes(QWidget *hostWindow)
{
    OptionsPanes result;
    if (!hostWindow)
        return result;

    const QList<QWidget *> widgets =
        hostWindow->findChildren<QWidget *>(QString(), Qt::FindChildrenRecursively);
    for (QWidget *widget : widgets) {
        if (QString::fromLatin1(widget->metaObject()->className()) !=
            QLatin1String(kLibraryInsertOptionsClass)) {
            continue;
        }

        if (widget->isVisible())
            result.visible.append(widget);
        else
            result.hasHidden = true;
    }
    return result;
}

LiveControlSnapshot readLiveControl(const OptionsPanes &panes,
                                    const QString &objectName)
{
    LiveControlSnapshot result;
    if (panes.visible.size() > 1) {
        result.state = LiveControlState::Ambiguous;
        return result;
    }

    if (panes.visible.isEmpty()) {
        result.state = panes.hasHidden ? LiveControlState::Hidden
                                       : LiveControlState::NotFound;
        return result;
    }

    const QList<QLineEdit *> fields = panes.visible.first()->findChildren<QLineEdit *>(
        objectName, Qt::FindChildrenRecursively);
    if (fields.isEmpty()) {
        result.state = LiveControlState::NotFound;
        return result;
    }
    if (fields.size() > 1) {
        result.state = LiveControlState::Ambiguous;
        return result;
    }
    if (!fields.first()->isVisible()) {
        result.state = LiveControlState::Hidden;
        return result;
    }

    result.state = LiveControlState::Found;
    result.value = fields.first()->text();
    return result;
}

} // namespace

InspectorSnapshot inspectLibraryInsert(QWidget *hostWindow, QSettings &settings)
{
    return inspectLibraryInsert(hostWindow, settings, PersistedFactorOverride());
}

InspectorSnapshot inspectLibraryInsert(
    QWidget *hostWindow, QSettings &settings,
    const PersistedFactorOverride &persistedOverride)
{
    InspectorSnapshot snapshot;
    const OptionsPanes panes = findLibraryInsertPanes(hostWindow);
    snapshot.liveFactor = readLiveControl(panes, QStringLiteral("leFactor"));
    snapshot.liveAngle = readLiveControl(panes, QStringLiteral("leAngle"));

    const QString originalGroup = settings.group();
    while (!settings.group().isEmpty())
        settings.endGroup();

    if (persistedOverride.available) {
        snapshot.persistedFactorExists = persistedOverride.exists;
        snapshot.persistedFactor = persistedOverride.value;
        snapshot.persistedFactorSource = persistedOverride.source.isEmpty()
            ? QStringLiteral("Authoritative settings source")
            : persistedOverride.source;
    } else {
        settings.beginGroup(QStringLiteral("LibraryInsert"));
        snapshot.persistedFactorExists =
            settings.contains(QStringLiteral("LibraryInsertFactor"));
        if (snapshot.persistedFactorExists)
            snapshot.persistedFactor = settings.value(QStringLiteral("LibraryInsertFactor"));
        settings.endGroup();
        snapshot.persistedFactorSource = QStringLiteral("QSettings in-process cache");
        if (!persistedOverride.source.isEmpty())
            snapshot.persistedFactorSource += QStringLiteral("; ") + persistedOverride.source;
    }

    const QStringList originalGroups =
        originalGroup.split('/', QString::SkipEmptyParts);
    for (const QString &group : originalGroups)
        settings.beginGroup(group);

    snapshot.settingsFormat = settings.format();
    snapshot.settingsLocation = settings.fileName();
    return snapshot;
}

} // namespace LibraryInsertFactorInspector
