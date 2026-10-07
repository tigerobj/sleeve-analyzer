#include "../factorinspectordialog.h"

#include <QLabel>
#include <QtTest>

using namespace LibraryInsertFactorInspector;

class FactorInspectorDialogTests : public QObject
{
    Q_OBJECT

private slots:
    void showsLiveAndPersistedValuesSeparately();
    void labelsUnavailableLiveValue();
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

QTEST_MAIN(FactorInspectorDialogTests)
#include "FactorInspectorDialogTests.moc"
