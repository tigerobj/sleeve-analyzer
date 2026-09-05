#include "SleeveSizeTable.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QtGlobal>
#include <cmath>

namespace {

const QStringList kPreferredSizeOrder = {
    QStringLiteral("12"), QStringLiteral("XS"), QStringLiteral("S"),
    QStringLiteral("M"), QStringLiteral("L"), QStringLiteral("XL"),
    QStringLiteral("2L"), QStringLiteral("3L"), QStringLiteral("4L"),
    QStringLiteral("5L")
};

bool readNumber(const QJsonObject& object, const QString& key, double* value)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble())
        return false;
    const double number = jsonValue.toDouble();
    if (!std::isfinite(number))
        return false;
    *value = number;
    return true;
}

} // namespace

SleeveSizeTable::SleeveSizeTable(const QString& filePath)
    : m_filePath(filePath)
{
}

void SleeveSizeTable::setFilePath(const QString& filePath)
{
    m_filePath = filePath;
}

QString SleeveSizeTable::filePath() const
{
    return m_filePath;
}

bool SleeveSizeTable::load()
{
    return reload();
}

bool SleeveSizeTable::reload()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("Failed to load sleeve_sizes.json\n%1")
                          .arg(QFileInfo(m_filePath).absoluteFilePath());
        return false;
    }

    QMap<QString, SleeveSizeData> parsed;
    QStringList parsedOrder;
    QString error;
    if (!parse(file.readAll(), &parsed, &parsedOrder, &error)) {
        m_lastError = error;
        return false;
    }

    // Replace the live table only after the entire file has passed validation.
    m_data = parsed;
    m_sizeOrder = parsedOrder;
    m_lastError.clear();
    return true;
}

bool SleeveSizeTable::isEmpty() const
{
    return m_data.isEmpty();
}

QString SleeveSizeTable::lastError() const
{
    return m_lastError;
}

QStringList SleeveSizeTable::sizes() const
{
    return m_sizeOrder;
}

QStringList SleeveSizeTable::variants(const QString& size) const
{
    return m_data.value(size).variants.keys();
}

QString SleeveSizeTable::defaultVariant(const QString& size) const
{
    return m_data.value(size).defaultVariant;
}

double SleeveSizeTable::sleeveLength(const QString& size,
                                     const QString& variant) const
{
    const SleeveVariant* value = findVariant(size, variant);
    return value ? value->sleeveLengthMM : 0.0;
}

double SleeveSizeTable::cuffWidth(const QString& size,
                                  const QString& variant) const
{
    const SleeveVariant* value = findVariant(size, variant);
    return value ? value->cuffWidthMM : 0.0;
}

QString SleeveSizeTable::sizeFromFileName(const QString& fileName)
{
    const QString baseName = QFileInfo(fileName).fileName();
    const QRegularExpression expression(
        QStringLiteral("^(12|XS|XL|[2-5]L|S|M|L)(?=$|[-_.\\s])"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(baseName);
    return match.hasMatch() ? match.captured(1).toUpper() : QString();
}

bool SleeveSizeTable::parse(const QByteArray& json,
                            QMap<QString, SleeveSizeData>* parsed,
                            QStringList* parsedOrder,
                            QString* error) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("Invalid sleeve size configuration\n%1")
                     .arg(parseError.errorString());
        return false;
    }

    const QJsonObject root = document.object();
    if (root.isEmpty()) {
        *error = QStringLiteral("Invalid sleeve size configuration");
        return false;
    }

    QStringList keys = root.keys();
    for (const QString& preferred : kPreferredSizeOrder) {
        if (keys.removeOne(preferred))
            parsedOrder->append(preferred);
    }
    parsedOrder->append(keys);

    for (const QString& size : *parsedOrder) {
        if (!root.value(size).isObject()) {
            *error = QStringLiteral("Invalid configuration for size %1").arg(size);
            return false;
        }
        const QJsonObject sizeObject = root.value(size).toObject();
        const QJsonValue defaultValue = sizeObject.value(QStringLiteral("default"));
        if (!defaultValue.isString() || defaultValue.toString().isEmpty()) {
            *error = QStringLiteral("Missing default variant for size %1").arg(size);
            return false;
        }
        const QJsonValue variantsValue = sizeObject.value(QStringLiteral("variants"));
        if (!variantsValue.isObject() || variantsValue.toObject().isEmpty()) {
            *error = QStringLiteral("Missing variants for size %1").arg(size);
            return false;
        }

        SleeveSizeData sizeData;
        sizeData.defaultVariant = defaultValue.toString();
        const QJsonObject variantsObject = variantsValue.toObject();
        for (const QString& variantName : variantsObject.keys()) {
            if (!variantsObject.value(variantName).isObject()) {
                *error = QStringLiteral("Invalid variant %1 for size %2")
                             .arg(variantName, size);
                return false;
            }
            const QJsonObject variantObject = variantsObject.value(variantName).toObject();
            SleeveVariant variant;
            if (!readNumber(variantObject, QStringLiteral("sleeve_length_mm"),
                            &variant.sleeveLengthMM)
                || variant.sleeveLengthMM < 0.0) {
                *error = QStringLiteral("Invalid sleeve_length_mm for %1 / %2")
                             .arg(size, variantName);
                return false;
            }
            if (!readNumber(variantObject, QStringLiteral("cuff_width_mm"),
                            &variant.cuffWidthMM)
                || variant.cuffWidthMM < 0.0) {
                *error = QStringLiteral("Invalid cuff_width_mm for %1 / %2")
                             .arg(size, variantName);
                return false;
            }
            sizeData.variants.insert(variantName, variant);
        }
        if (!sizeData.variants.contains(sizeData.defaultVariant)) {
            *error = QStringLiteral("Default variant not found for size %1").arg(size);
            return false;
        }
        parsed->insert(size, sizeData);
    }
    return true;
}

const SleeveVariant* SleeveSizeTable::findVariant(const QString& size,
                                                   const QString& variant) const
{
    const auto sizeIt = m_data.constFind(size);
    if (sizeIt == m_data.constEnd())
        return nullptr;
    const auto variantIt = sizeIt->variants.constFind(variant);
    return variantIt == sizeIt->variants.constEnd() ? nullptr : &variantIt.value();
}
