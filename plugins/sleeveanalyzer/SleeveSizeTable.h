#ifndef SLEEVESIZETABLE_H
#define SLEEVESIZETABLE_H

#include <QMap>
#include <QString>
#include <QStringList>

struct SleeveVariant
{
    double sleeveLengthMM = 0.0;
    double cuffWidthMM = 0.0;
};

struct SleeveSizeData
{
    QString defaultVariant;
    QMap<QString, SleeveVariant> variants;
};

class SleeveSizeTable
{
public:
    explicit SleeveSizeTable(const QString& filePath = QString());

    void setFilePath(const QString& filePath);
    QString filePath() const;
    bool load();
    bool reload();
    bool isEmpty() const;
    QString lastError() const;

    QStringList sizes() const;
    QStringList variants(const QString& size) const;
    QString defaultVariant(const QString& size) const;
    double sleeveLength(const QString& size, const QString& variant) const;
    double cuffWidth(const QString& size, const QString& variant) const;

    static QString sizeFromFileName(const QString& fileName);

private:
    bool parse(const QByteArray& json, QMap<QString, SleeveSizeData>* parsed,
               QStringList* parsedOrder, QString* error) const;
    const SleeveVariant* findVariant(const QString& size,
                                     const QString& variant) const;

    QString m_filePath;
    QMap<QString, SleeveSizeData> m_data;
    QStringList m_sizeOrder;
    QString m_lastError;
};

#endif // SLEEVESIZETABLE_H
