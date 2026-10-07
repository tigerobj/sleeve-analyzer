#include "../factorinspectorsnapshot.h"

#include <QApplication>
#include <QFile>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QtTest>

using namespace LibraryInsertFactorInspector;

class QG_LibraryInsertOptions : public QWidget
{
    Q_OBJECT
public:
    explicit QG_LibraryInsertOptions(QWidget *parent = nullptr)
        : QWidget(parent), m_layout(new QVBoxLayout(this))
    {
    }

    QLineEdit *addField(const QString &name, const QString &text)
    {
        QLineEdit *field = new QLineEdit(this);
        field->setObjectName(name);
        field->setText(text);
        m_layout->addWidget(field);
        return field;
    }

private:
    QVBoxLayout *m_layout;
};

class FactorInspectorSnapshotTests : public QObject
{
    Q_OBJECT

private slots:
    void readsLibraryInsertPaneNotStandardInsert();
    void reportsMissingControl();
    void reportsHiddenControl();
    void reportsAmbiguousVisiblePanes();
    void distinguishesMissingKeyFromStoredOne();
    void snapshotDoesNotWriteSettings();
};

void FactorInspectorSnapshotTests::readsLibraryInsertPaneNotStandardInsert()
{
    QWidget host;
    QWidget standardInsertOptions(&host);
    QLineEdit standardFactor(&standardInsertOptions);
    standardFactor.setObjectName(QStringLiteral("leFactor"));
    standardFactor.setText(QStringLiteral("77"));

    QG_LibraryInsertOptions libraryOptions(&host);
    libraryOptions.addField(QStringLiteral("leAngle"), QStringLiteral("0.0"));
    libraryOptions.addField(QStringLiteral("leFactor"), QStringLiteral("10"));
    host.show();
    QApplication::processEvents();

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);
    settings.setValue(QStringLiteral("LibraryInsert/LibraryInsertFactor"),
                      QStringLiteral("1"));
    settings.sync();

    const InspectorSnapshot snapshot = inspectLibraryInsert(&host, settings);
    QVERIFY(snapshot.liveFactor.state == LiveControlState::Found);
    QCOMPARE(snapshot.liveFactor.value, QStringLiteral("10"));
    QVERIFY(snapshot.liveAngle.state == LiveControlState::Found);
    QCOMPARE(snapshot.liveAngle.value, QStringLiteral("0.0"));
    QVERIFY(snapshot.persistedFactorExists);
    QCOMPARE(snapshot.persistedFactor.toString(), QStringLiteral("1"));
}

void FactorInspectorSnapshotTests::reportsMissingControl()
{
    QWidget host;
    host.show();
    QApplication::processEvents();

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);

    const InspectorSnapshot snapshot = inspectLibraryInsert(&host, settings);
    QVERIFY(snapshot.liveFactor.state == LiveControlState::NotFound);
    QVERIFY(snapshot.liveFactor.value.isEmpty());
}

void FactorInspectorSnapshotTests::reportsHiddenControl()
{
    QWidget host;
    QG_LibraryInsertOptions libraryOptions(&host);
    libraryOptions.addField(QStringLiteral("leFactor"), QStringLiteral("10"));
    host.show();
    libraryOptions.hide();
    QApplication::processEvents();

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);

    const InspectorSnapshot snapshot = inspectLibraryInsert(&host, settings);
    QVERIFY(snapshot.liveFactor.state == LiveControlState::Hidden);
    QVERIFY(snapshot.liveFactor.value.isEmpty());
}

void FactorInspectorSnapshotTests::reportsAmbiguousVisiblePanes()
{
    QWidget host;
    QG_LibraryInsertOptions first(&host);
    first.addField(QStringLiteral("leFactor"), QStringLiteral("10"));
    QG_LibraryInsertOptions second(&host);
    second.addField(QStringLiteral("leFactor"), QStringLiteral("20"));
    host.show();
    QApplication::processEvents();

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);

    const InspectorSnapshot snapshot = inspectLibraryInsert(&host, settings);
    QVERIFY(snapshot.liveFactor.state == LiveControlState::Ambiguous);
    QVERIFY(snapshot.liveFactor.value.isEmpty());
}

void FactorInspectorSnapshotTests::distinguishesMissingKeyFromStoredOne()
{
    QWidget host;
    QG_LibraryInsertOptions libraryOptions(&host);
    libraryOptions.addField(QStringLiteral("leFactor"), QStringLiteral("10"));
    host.show();
    QApplication::processEvents();

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);

    InspectorSnapshot snapshot = inspectLibraryInsert(&host, settings);
    QVERIFY(!snapshot.persistedFactorExists);
    QVERIFY(!snapshot.persistedFactor.isValid());

    settings.setValue(QStringLiteral("LibraryInsert/LibraryInsertFactor"),
                      QStringLiteral("1"));
    settings.sync();
    snapshot = inspectLibraryInsert(&host, settings);
    QVERIFY(snapshot.persistedFactorExists);
    QCOMPARE(snapshot.persistedFactor.toString(), QStringLiteral("1"));
    QCOMPARE(snapshot.settingsFormat, QSettings::IniFormat);
    QCOMPARE(snapshot.settingsLocation, settings.fileName());
}

void FactorInspectorSnapshotTests::snapshotDoesNotWriteSettings()
{
    QWidget host;
    QG_LibraryInsertOptions libraryOptions(&host);
    libraryOptions.addField(QStringLiteral("leFactor"), QStringLiteral("10"));
    host.show();
    QApplication::processEvents();

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("LibraryInsert/LibraryInsertFactor"),
                      QStringLiteral("1"));
    settings.sync();

    QFile beforeFile(settingsPath);
    QVERIFY(beforeFile.open(QIODevice::ReadOnly));
    const QByteArray before = beforeFile.readAll();
    beforeFile.close();

    const InspectorSnapshot snapshot = inspectLibraryInsert(&host, settings);
    QVERIFY(snapshot.persistedFactorExists);

    QFile afterFile(settingsPath);
    QVERIFY(afterFile.open(QIODevice::ReadOnly));
    QCOMPARE(afterFile.readAll(), before);
}

QTEST_MAIN(FactorInspectorSnapshotTests)
#include "FactorInspectorSnapshotTests.moc"
