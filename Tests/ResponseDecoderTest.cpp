#include "eon/infra/ResponseDecoder.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <cmath>
#include <iostream>

using eon::infra::DecodeSpec;
using eon::infra::ResponseDecoder;

namespace {
int check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition ? 0 : 1;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;

    DecodeSpec integer;
    integer.name = "temperature";
    integer.offset = 3;
    integer.length = 2;
    integer.type = "uint16";
    integer.formula = "x * 0.1 - 40";
    integer.unit = "degC";
    const auto integerResult = ResponseDecoder::decode(
        QByteArray::fromHex("62F1050BB8"), integer);
    failures += check(integerResult.success, "uint16 formula decode");
    failures += check(std::abs(integerResult.measurements.first().value.toDouble() - 260.0) < 1e-9,
                      "uint16 formula value");

    DecodeSpec little;
    little.type = "uint16";
    little.length = 2;
    little.endian = "little";
    const auto littleResult = ResponseDecoder::decode(QByteArray::fromHex("0BB8"), little);
    failures += check(littleResult.success && littleResult.measurements.first().value.toULongLong() == 47115,
                      "little endian uint16");

    DecodeSpec text;
    text.type = "string";
    text.length = 9;
    text.extractRegex = "TEMP=([+-]?[0-9.]+)";
    text.postType = "float64";
    const auto textResult = ResponseDecoder::decode(QByteArray("TEMP=25.6"), text);
    failures += check(textResult.success && std::abs(textResult.measurements.first().value.toDouble() - 25.6) < 1e-9,
                      "regex string conversion");

    DecodeSpec bits;
    bits.type = "bits";
    bits.length = 1;
    bits.bitOffset = 2;
    bits.bitLength = 3;
    const auto bitsResult = ResponseDecoder::decode(QByteArray::fromHex("35"), bits);
    failures += check(bitsResult.success && bitsResult.measurements.first().value.toULongLong() == 5,
                      "bit field extraction");

    DecodeSpec rejected;
    rejected.type = "uint16";
    rejected.length = 1;
    const auto rejectedResult = ResponseDecoder::decode(QByteArray::fromHex("01"), rejected);
    failures += check(!rejectedResult.success, "invalid integer length rejected");

    const QVariantList profile{
        QVariantMap{{"name", "raw"}, {"offset", 0}, {"length", 1}, {"type", "uint8"}},
        QVariantMap{{"name", "scaled"}, {"offset", 1}, {"length", 2},
                    {"type", "uint16"}, {"formula", "x % 10"}}
    };
    const auto manyResult = ResponseDecoder::decodeMany(
        QByteArray::fromHex("057B2D"), profile);
    failures += check(manyResult.success && manyResult.measurements.size() == 2,
                      "multiple measurements decode");
    if (manyResult.success) {
        failures += check(manyResult.measurements.at(0).value.toULongLong() == 5,
                          "first multiple measurement value");
        failures += check(manyResult.measurements.at(1).value.toDouble() == 3,
                          "formula modulo value");
    }

    DecodeSpec rounding;
    rounding.type = "uint8";
    rounding.length = 1;
    rounding.formula = "ceil(x / 2)";
    const auto roundingResult = ResponseDecoder::decode(QByteArray::fromHex("05"), rounding);
    failures += check(roundingResult.success &&
                          roundingResult.measurements.first().value.toDouble() == 3,
                      "formula ceil function");

    return failures == 0 ? 0 : 1;
}
