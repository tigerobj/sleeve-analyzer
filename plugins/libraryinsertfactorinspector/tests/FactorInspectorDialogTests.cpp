#include "../factorinspectordialog.h"

#include <QGridLayout>
#include <QLabel>
#include <QtTest>

using namespace LibraryInsertFactorInspector;

class FactorInspectorDialogTests : public QObject
{
    Q_OBJECT

private slots:
    void showsLiveAndPersistedValuesSeparately();
    void labelsUnavailableLiveValue();
    void identifiesPersistedValueSource();
    void placesPersistedSourceAndRegistryKeyOnSeparateRows();
};

void FactorInspectorDialogTests::showsLiveAndPersistedValuesSeparately()
{
    InspectorSnapshot snapshot;
    snapshot.liveFactor.state = LiveControlState::Found;
    snapshot.liveFactor.value = QStringLiteral("10");
    snapshot.liveAngle.state = LiveControlState::Found;
    snapshot.liveAngle.value = QStringLiteral("0.0");
    snapshot.persistedFactorExists = true;
    snapshot.persistedFactor = QStringLiteral("1");
    snapshot.settingsFormat = QSettings::NativeFormat;
    snapshot.settingsLocation = QStringLiteral("HKEY_CURRENT_USER\\Software\\LibreCAD");

    FactorInspectorDialog dialog(snapshot);
    QLabel *liveFactor = dialog.findChild<QLabel *>(QStringLiteral("liveFactorValue"));
    QLabel *persistedFactor = dialog.findChild<QLabel *>(QStringLiteral("persistedFactorValue"));
    QLabel *liveAngle = dialog.findChild<QLabel *>(QStringLiteral("liveAngleValue"));
    QLabel *keyState = dialog.findChild<QLabel *>(QStringLiteral("persistedFactorKeyState"));
    QLabel *format = dialog.findChild<QLabel *>(QStringLiteral("settingsFormatValue"));
    QLabel *location = dialog.findChild<QLabel *>(QStringLiteral("settingsLocationValue"));

    QVERIFY(liveFactor);
    QVERIFY(persistedFactor);
    QVERIFY(liveAngle);
    QVERIFY(keyState);
    QVERIFY(format);
    QVERIFY(location);
    QCOMPARE(liveFactor->text(), QStringLiteral("10"));
    QCOMPARE(persistedFactor->text(), QStringLiteral("1"));
    QCOMPARE(liveAngle->text(), QStringLiteral("0.0"));
    QVERIFY(keyState->text().contains(
        QStringLiteral("LibraryInsert/LibraryInsertFactor")));
    QVERIFY(keyState->text().contains(QStringLiteral("exists"), Qt::CaseInsensitive));
    QVERIFY(!format->text().isEmpty());
    QCOMPARE(location->text(), snapshot.settingsLocation);
}

void FactorInspectorDialogTests::labelsUnavailableLiveValue()
{
    InspectorSnapshot snapshot;
    snapshot.liveFactor.state = LiveControlState::NotFound;
    snapshot.persistedFactorExists = false;

    FactorInspectorDialog dialog(snapshot);
    QLabel *liveFactor = dialog.findChild<QLabel *>(QStringLiteral("liveFactorValue"));
    QLabel *persistedFactor = dialog.findChild<QLabel *>(QStringLiteral("persistedFactorValue"));
    QLabel *keyState = dialog.findChild<QLabel *>(QStringLiteral("persistedFactorKeyState"));

    QVERIFY(liveFactor);
    QVERIFY(persistedFactor);
    QVERIFY(keyState);
    QVERIFY(liveFactor->text().contains(QStringLiteral("Unavailable"), Qt::CaseInsensitive));
    QVERIFY(liveFactor->text() != QStringLiteral("1"));
    QVERIFY(persistedFactor->text() != QStringLiteral("1"));
    QVERIFY(keyState->text().contains(QStringLiteral("missing"), Qt::CaseInsensitive));
}

void FactorInspectorDialogTests::identifiesPersistedValueSource()
{
    InspectorSnapshot snapshot;
    snapshot.persistedFactorExists = true;
    snapshot.persistedFactor = QStringLiteral("10");
    snapshot.persistedFactorSource =
        QStringLiteral("QSettings cache fallback (Win32 error 5)");

    FactorInspectorDialog dialog(snapshot);
    QLabel *source = dialog.findChild<QLabel *>(
        QStringLiteral("persistedFactorSourceValue"));

    QVERIFY(source);
    QCOMPARE(source->text(),
             QStringLiteral("QSettings cache fallback (Win32 error 5)"));
}

void FactorInspectorDialogTests::placesPersistedSourceAndRegistryKeyOnSeparateRows()
{
    InspectorSnapshot snapshot;
    FactorInspectorDialog dialog(snapshot);
    QGridLayout *valuesLayout = qobject_cast<QGridLayout *>(
        dialog.layout()->itemAt(0)->layout());
    QLabel *source = dialog.findChild<QLabel *>(
        QStringLiteral("persistedFactorSourceValue"));
    QLabel *keyState = dialog.findChild<QLabel *>(
        QStringLiteral("persistedFactorKeyState"));

    QVERIFY(valuesLayout);
    QVERIFY(source);
    QVERIFY(keyState);

    int sourceRow = -1;
    int sourceColumn = -1;
    int sourceRowSpan = 0;
    int sourceColumnSpan = 0;
    int keyRow = -1;
    int keyColumn = -1;
    int keyRowSpan = 0;
    int keyColumnSpan = 0;
    valuesLayout->getItemPosition(valuesLayout->indexOf(source),
                                  &sourceRow, &sourceColumn,
                                  &sourceRowSpan, &sourceColumnSpan);
    valuesLayout->getItemPosition(valuesLayout->indexOf(keyState),
                                  &keyRow, &keyColumn,
                                  &keyRowSpan, &keyColumnSpan);

    QVERIFY2(sourceRow != keyRow,
             "Persisted value source and registry key share a grid row.");
}

QTEST_MAIN(FactorInspectorDialogTests)
#include "FactorInspectorDialogTests.moc"
