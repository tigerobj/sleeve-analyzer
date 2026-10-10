#include "libraryinsertfactorinspector.h"

#include "factorinspectordialog.h"
#include "factorinspectorsnapshot.h"

#include <QByteArray>
#include <QSettings>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace LibraryInsertFactorInspector {
namespace {

PersistedFactorOverride readPersistedFactorFromRegistry()
{
    PersistedFactorOverride result;
#ifdef Q_OS_WIN
    HKEY libraryInsertKey = nullptr;
    const LONG openResult = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\LibreCAD\\LibreCAD\\LibraryInsert",
        0, KEY_QUERY_VALUE, &libraryInsertKey);

    if (openResult == ERROR_FILE_NOT_FOUND) {
        result.available = true;
        result.source = QStringLiteral("Win32 registry: key missing (error %1)")
                            .arg(openResult);
        return result;
    }
    if (openResult != ERROR_SUCCESS) {
        result.source = QStringLiteral("Win32 registry unavailable: RegOpenKeyExW error %1")
                            .arg(openResult);
        return result;
    }

    result.available = true;
    DWORD valueType = 0;
    DWORD valueSize = 0;
    const LONG sizeResult = RegQueryValueExW(
        libraryInsertKey, L"LibraryInsertFactor", nullptr,
        &valueType, nullptr, &valueSize);
    if (sizeResult == ERROR_FILE_NOT_FOUND) {
        result.source = QStringLiteral("Win32 registry: value not stored");
        RegCloseKey(libraryInsertKey);
        return result;
    }
    if (sizeResult != ERROR_SUCCESS) {
        result.available = false;
        result.source = QStringLiteral("Win32 registry unavailable: size query error %1")
                            .arg(sizeResult);
        RegCloseKey(libraryInsertKey);
        return result;
    }

    QByteArray valueData(static_cast<int>(valueSize) + sizeof(wchar_t), '\0');
    const LONG readResult = RegQueryValueExW(
        libraryInsertKey, L"LibraryInsertFactor", nullptr,
        &valueType, reinterpret_cast<LPBYTE>(valueData.data()), &valueSize);
    RegCloseKey(libraryInsertKey);
    if (readResult != ERROR_SUCCESS) {
        result.available = false;
        result.source = QStringLiteral("Win32 registry unavailable: value read error %1")
                            .arg(readResult);
        return result;
    }

    result.exists = true;
    result.source = QStringLiteral("Win32 registry read succeeded (type %1, %2 bytes)")
                        .arg(valueType).arg(valueSize);
    if (valueType == REG_SZ || valueType == REG_EXPAND_SZ) {
        const int characterCount = static_cast<int>(valueSize / sizeof(wchar_t));
        result.value = QString::fromWCharArray(
            reinterpret_cast<const wchar_t *>(valueData.constData()),
            characterCount).trimmed();
    } else if (valueType == REG_DWORD && valueSize == sizeof(DWORD)) {
        DWORD value = 0;
        std::memcpy(&value, valueData.constData(), sizeof(value));
        result.value = QVariant(static_cast<uint>(value));
    } else if (valueType == REG_QWORD && valueSize == sizeof(ULONGLONG)) {
        ULONGLONG value = 0;
        std::memcpy(&value, valueData.constData(), sizeof(value));
        result.value = QVariant::fromValue<qulonglong>(value);
    } else {
        result.value = QString::fromLatin1(valueData.left(valueSize).toHex());
    }
#endif
    if (result.source.isEmpty())
        result.source = QStringLiteral("Win32 registry API unavailable; using QSettings cache");
    return result;
}

} // namespace

QString LC_LibraryInsertFactorInspector::name() const
{
    return tr("Library Insert Factor Inspector");
}

PluginCapabilities LC_LibraryInsertFactorInspector::getCapabilities() const
{
    PluginCapabilities capabilities;
    capabilities.menuEntryPoints
        << PluginMenuLocation(QStringLiteral("plugins_menu"), name());
    return capabilities;
}

void LC_LibraryInsertFactorInspector::execComm(Document_Interface *doc,
                                               QWidget *parent,
                                               QString command)
{
    Q_UNUSED(doc);
    Q_UNUSED(command);

    QSettings settings(QStringLiteral("LibreCAD"), QStringLiteral("LibreCAD"));
    const PersistedFactorOverride persistedFactor = readPersistedFactorFromRegistry();
    const InspectorSnapshot snapshot =
        inspectLibraryInsert(parent, settings, persistedFactor);
    FactorInspectorDialog dialog(snapshot, parent);
    dialog.exec();
}

} // namespace LibraryInsertFactorInspector
