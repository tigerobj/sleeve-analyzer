#include "factorinspectordialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace LibraryInsertFactorInspector {
namespace {

QString liveControlText(const LiveControlSnapshot &control)
{
    switch (control.state) {
    case LiveControlState::Found:
        return control.value;
    case LiveControlState::NotFound:
        return QObject::tr("Unavailable (control not found)");
    case LiveControlState::Hidden:
        return QObject::tr("Unavailable (control hidden)");
    case LiveControlState::Ambiguous:
        return QObject::tr("Unavailable (multiple matches)");
    }
    return QObject::tr("Unavailable");
}

QString settingsFormatText(QSettings::Format format)
{
    switch (format) {
    case QSettings::NativeFormat:
        return QStringLiteral("NativeFormat");
    case QSettings::IniFormat:
        return QStringLiteral("IniFormat");
    default:
        return QObject::tr("Unknown format (%1)").arg(static_cast<int>(format));
    }
}

QLabel *addValueRow(QGridLayout *layout, int row, const QString &caption,
                    const QString &objectName, const QString &value)
{
    QLabel *captionLabel = new QLabel(caption);
    QLabel *valueLabel = new QLabel(value);
    valueLabel->setObjectName(objectName);
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    valueLabel->setWordWrap(true);
    layout->addWidget(captionLabel, row, 0);
    layout->addWidget(valueLabel, row, 1);
    return valueLabel;
}

} // namespace

FactorInspectorDialog::FactorInspectorDialog(const InspectorSnapshot &snapshot,
                                             QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Library Insert Factor Inspector"));
    setModal(true);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QGridLayout *valuesLayout = new QGridLayout;
    valuesLayout->setColumnStretch(1, 1);

    addValueRow(valuesLayout, 0, tr("Live Library Insert angle:"),
                QStringLiteral("liveAngleValue"), liveControlText(snapshot.liveAngle));
    addValueRow(valuesLayout, 1, tr("Live Library Insert factor:"),
                QStringLiteral("liveFactorValue"), liveControlText(snapshot.liveFactor));

    const QString storedValue = snapshot.persistedFactorExists
        ? snapshot.persistedFactor.toString()
        : tr("Not stored");
    addValueRow(valuesLayout, 2, tr("Persisted factor value:"),
                QStringLiteral("persistedFactorValue"), storedValue);

    const QString keyPath = QStringLiteral("LibraryInsert/LibraryInsertFactor");
    const QString keyState = snapshot.persistedFactorExists
        ? tr("%1 — key exists").arg(keyPath)
        : tr("%1 — key missing; LibreCAD default 1.0 may be used").arg(keyPath);
    addValueRow(valuesLayout, 3, tr("Registry/settings key:"),
                QStringLiteral("persistedFactorKeyState"), keyState);
    addValueRow(valuesLayout, 4, tr("Settings format:"),
                QStringLiteral("settingsFormatValue"),
                settingsFormatText(snapshot.settingsFormat));
    addValueRow(valuesLayout, 5, tr("Settings location:"),
                QStringLiteral("settingsLocationValue"), snapshot.settingsLocation);

    mainLayout->addLayout(valuesLayout);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked,
            this, &QDialog::accept);
    mainLayout->addWidget(buttons);
}

} // namespace LibraryInsertFactorInspector
