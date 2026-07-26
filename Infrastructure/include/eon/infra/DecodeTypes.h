#pragma once

#include <QByteArray>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QString>

namespace eon::infra {

struct DecodeSpec {
    QString name;
    int offset = 0;
    int length = -1;
    QString type = "hex";
    QString endian = "big";
    QString encoding = "ascii";
    int bitOffset = 0;
    int bitLength = 0;
    QString formula;
    QString unit;
    QString extractRegex;
    QString postType;
    bool trim = true;
    QString padding;

    static DecodeSpec fromVariantMap(const QVariantMap& values);
    bool hasExplicitDecode() const;
};

struct DecodedMeasurement {
    QString name;
    QVariant rawValue;
    QVariant value;
    QString rawHex;
    QString unit;
    QString type;
    QString formula;
    bool passed = true;
    QString errorMessage;

    QVariantMap toVariantMap() const;
};

struct DecodeResult {
    QByteArray payload;
    QList<DecodedMeasurement> measurements;
    QString errorMessage;
    bool success = false;

    QVariantList toVariantList() const;
};

} // namespace eon::infra
