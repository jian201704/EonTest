#include "eon/infra/ResponseDecoder.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace eon::infra {
namespace {

QString normalized(const QString& value) { return value.trimmed().toLower(); }

QByteArray parseHex(const QVariant& value) {
    const QByteArray text = value.toString().toLatin1().trimmed();
    if (text.isEmpty()) return {};
    return QByteArray::fromHex(text);
}

bool parseBool(const QVariant& value, bool fallback) {
    if (!value.isValid()) return fallback;
    const QString text = normalized(value.toString());
    if (text == "true" || text == "1" || text == "yes") return true;
    if (text == "false" || text == "0" || text == "no") return false;
    return value.toBool();
}

quint64 unsignedValue(const QByteArray& bytes, bool littleEndian) {
    quint64 result = 0;
    if (littleEndian) {
        for (int i = bytes.size() - 1; i >= 0; --i)
            result = (result << 8) | static_cast<quint8>(bytes.at(i));
    } else {
        for (const auto byte : bytes)
            result = (result << 8) | static_cast<quint8>(byte);
    }
    return result;
}

qint64 signedValue(const QByteArray& bytes, bool littleEndian) {
    const quint64 raw = unsignedValue(bytes, littleEndian);
    const int bits = bytes.size() * 8;
    if (bits == 64) return static_cast<qint64>(raw);
    const quint64 sign = quint64(1) << (bits - 1);
    return static_cast<qint64>((raw & sign) ? (raw | (~quint64(0) << bits)) : raw);
}

bool numeric(const QVariant& value, double& result) {
    bool ok = false;
    result = value.toDouble(&ok);
    return ok && std::isfinite(result);
}

class FormulaParser {
public:
    FormulaParser(const QString& expression, double x, QString& error)
        : text_(expression), x_(x), error_(error) {}

    bool parse(double& result) {
        position_ = 0;
        if (!expression()) return false;
        skipSpaces();
        if (position_ != text_.size()) return fail("unexpected token");
        if (!std::isfinite(result_)) return fail("formula result is not finite");
        result = result_;
        return true;
    }

private:
    bool expression() {
        if (!term()) return false;
        double value = rhs_;
        while (true) {
            skipSpaces();
            if (match('+')) {
                if (!term()) return false;
                value += rhs_;
            } else if (match('-')) {
                if (!term()) return false;
                value -= rhs_;
            } else {
                result_ = value;
                return true;
            }
        }
    }

    bool term() {
        if (!factor()) return false;
        double value = rhs_;
        while (true) {
            skipSpaces();
            if (match('*')) { if (!factor()) return false; value *= rhs_; }
            else if (match('/')) {
                if (!factor()) return false;
                if (rhs_ == 0.0) return fail("division by zero");
                value /= rhs_;
            } else if (match('%')) {
                if (!factor()) return false;
                if (rhs_ == 0.0) return fail("modulo by zero");
                value = std::fmod(value, rhs_);
            } else {
                rhs_ = value;
                return true;
            }
        }
    }

    bool factor() {
        skipSpaces();
        if (match('+')) return factor();
        if (match('-')) { if (!factor()) return false; rhs_ = -rhs_; return true; }
        if (match('(')) {
            if (!expression()) return false;
            const double value = result_;
            skipSpaces();
            if (!match(')')) return fail("missing ')'" );
            rhs_ = value;
            return true;
        }
        const int start = position_;
        bool exponent = false;
        while (position_ < text_.size()) {
            const QChar character = text_.at(position_);
            if (character.isDigit() || character == '.') {
                ++position_;
                continue;
            }
            if ((character == 'e' || character == 'E') && !exponent) {
                exponent = true;
                ++position_;
                if (position_ < text_.size() &&
                    (text_.at(position_) == '+' || text_.at(position_) == '-')) {
                    ++position_;
                }
                continue;
            }
            break;
        }
        if (position_ > start) {
            bool ok = false;
            rhs_ = text_.mid(start, position_ - start).toDouble(&ok);
            if (!ok) return fail("invalid number");
            return true;
        }
        if (text_.mid(position_, 1).compare("x", Qt::CaseInsensitive) == 0) {
            ++position_; rhs_ = x_; return true;
        }
        const QStringList functions = {"abs", "round", "floor", "ceil", "min", "max"};
        for (const auto& function : functions) {
            if (text_.mid(position_, function.size()).compare(function, Qt::CaseInsensitive) == 0) {
                position_ += function.size(); skipSpaces();
                if (!match('(')) return fail("missing '(' after function");
                if (!expression()) return false;
                const double first = result_;
                skipSpaces();
                if (function == "abs" || function == "round" ||
                    function == "floor" || function == "ceil") {
                    if (!match(')')) return fail("function expects one argument");
                    if (function == "abs") rhs_ = std::abs(first);
                    else if (function == "round") rhs_ = std::round(first);
                    else if (function == "floor") rhs_ = std::floor(first);
                    else rhs_ = std::ceil(first);
                    return true;
                }
                if (!match(',')) return fail("function expects two arguments");
                if (!expression()) return false;
                const double second = result_;
                skipSpaces();
                if (!match(')')) return fail("missing ')'" );
                rhs_ = function == "min" ? std::min(first, second) : std::max(first, second);
                return true;
            }
        }
        return fail("expected number, x, or supported function");
    }

    bool match(QChar character) {
        skipSpaces();
        if (position_ < text_.size() && text_.at(position_) == character) { ++position_; return true; }
        return false;
    }
    void skipSpaces() { while (position_ < text_.size() && text_.at(position_).isSpace()) ++position_; }
    bool fail(const QString& message) { if (error_.isEmpty()) error_ = message; return false; }

    QString text_;
    double x_ = 0.0;
    QString& error_;
    int position_ = 0;
    double result_ = 0.0;
    double rhs_ = 0.0;
};

} // namespace

DecodeSpec DecodeSpec::fromVariantMap(const QVariantMap& values) {
    DecodeSpec spec;
    spec.name = values.value("name", values.value("measurementName")).toString();
    spec.offset = values.value("valueOffset", values.value("offset", 0)).toInt();
    spec.length = values.value("valueLength", values.value("length", -1)).toInt();
    spec.type = values.value("valueType", values.value("type", "hex")).toString();
    spec.endian = values.value("endian", "big").toString();
    spec.encoding = values.value("encoding", "ascii").toString();
    spec.bitOffset = values.value("bitOffset", 0).toInt();
    spec.bitLength = values.value("bitLength", 0).toInt();
    spec.formula = values.value("formula").toString();
    spec.unit = values.value("unit", values.value("measuredUnit")).toString();
    spec.extractRegex = values.value("extractRegex").toString();
    spec.postType = values.value("postType").toString();
    spec.trim = parseBool(values.value("trim"), true);
    spec.padding = values.value("padding").toString();
    return spec;
}

bool DecodeSpec::hasExplicitDecode() const {
    return offset != 0 || length >= 0 || normalized(type) != "hex" || !formula.isEmpty() ||
           !extractRegex.isEmpty() || !unit.isEmpty() || bitLength > 0;
}

QVariantMap DecodedMeasurement::toVariantMap() const {
    return {{"name", name}, {"rawValue", rawValue}, {"value", value}, {"rawHex", rawHex},
            {"unit", unit}, {"type", type}, {"formula", formula}, {"passed", passed},
            {"error", errorMessage}};
}

QVariantList DecodeResult::toVariantList() const {
    QVariantList result;
    for (const auto& measurement : measurements) result.append(measurement.toVariantMap());
    return result;
}

DecodeResult ResponseDecoder::decode(const QByteArray& payload, const DecodeSpec& spec) {
    DecodeResult result;
    result.payload = payload;
    DecodedMeasurement measurement;
    measurement.name = spec.name;
    measurement.unit = spec.unit;
    measurement.type = normalized(spec.type);
    measurement.formula = spec.formula;

    const int offset = spec.offset;
    const int length = spec.length < 0 ? payload.size() - offset : spec.length;
    if (offset < 0 || length <= 0 || offset > payload.size() || length > payload.size() - offset) {
        measurement.errorMessage = "decode range is outside payload";
    } else {
        const QByteArray selected = payload.mid(offset, length);
        measurement.rawHex = selected.toHex(' ').toUpper();
        const bool little = normalized(spec.endian) == "little";
        const QString type = measurement.type;
        if (type == "hex") measurement.rawValue = measurement.value = measurement.rawHex;
        else if (type == "string") {
            measurement.rawValue = QString::fromUtf8(selected);
            measurement.value = measurement.rawValue;
            if (spec.trim) measurement.value = measurement.value.toString().trimmed();
        } else if (type == "bits") {
            if (spec.bitOffset < 0 || spec.bitLength <= 0 || spec.bitLength > 64 ||
                spec.bitOffset + spec.bitLength > selected.size() * 8) {
                measurement.errorMessage = "invalid bit range";
            } else {
                const quint64 raw = unsignedValue(selected, little);
                const quint64 mask = spec.bitLength == 64 ? ~quint64(0) : ((quint64(1) << spec.bitLength) - 1);
                measurement.rawValue = measurement.value = QVariant::fromValue((raw >> spec.bitOffset) & mask);
            }
        } else {
            QString actualType = type;
            if (actualType == "uint") actualType = QString("uint%1").arg(length * 8);
            if (actualType == "int") actualType = QString("int%1").arg(length * 8);
            if (actualType == "float") actualType = "float32";
            if (actualType.startsWith("uint")) {
                const int expected = actualType.mid(4).toInt() / 8;
                if (expected != length || expected > 8) measurement.errorMessage = "invalid unsigned integer length";
                else measurement.rawValue = measurement.value = QVariant::fromValue(unsignedValue(selected, little));
            } else if (actualType.startsWith("int")) {
                const int expected = actualType.mid(3).toInt() / 8;
                if (expected != length || expected > 8) measurement.errorMessage = "invalid signed integer length";
                else measurement.rawValue = measurement.value = QVariant::fromValue(signedValue(selected, little));
            } else if (actualType == "float32" || actualType == "float64") {
                const int expected = actualType == "float32" ? 4 : 8;
                if (length != expected) measurement.errorMessage = "invalid floating-point length";
                else {
                    QByteArray ordered = selected;
                    if (little) std::reverse(ordered.begin(), ordered.end());
                    double value = 0.0;
                    if (expected == 4) { float f; std::memcpy(&f, ordered.constData(), 4); value = f; }
                    else std::memcpy(&value, ordered.constData(), 8);
                    if (!std::isfinite(value)) measurement.errorMessage = "decoded floating-point value is not finite";
                    else measurement.rawValue = measurement.value = value;
                }
            } else measurement.errorMessage = "unsupported decode type: " + spec.type;
        }
    }

    if (measurement.errorMessage.isEmpty() && !spec.extractRegex.isEmpty()) {
        const auto match = QRegularExpression(spec.extractRegex).match(measurement.value.toString());
        if (!match.hasMatch() || match.lastCapturedIndex() < 1) measurement.errorMessage = "extractRegex did not match";
        else {
            measurement.rawValue = measurement.value = match.captured(1);
            if (!spec.postType.isEmpty()) {
                bool ok = false; const double value = measurement.value.toString().toDouble(&ok);
                if (!ok) measurement.errorMessage = "postType conversion failed";
                else if (normalized(spec.postType).startsWith("int")) measurement.value = qRound64(value);
                else measurement.value = value;
            }
        }
    }

    if (measurement.errorMessage.isEmpty() && !spec.formula.trimmed().isEmpty()) {
        double input = 0.0, output = 0.0;
        if (!numeric(measurement.value, input)) measurement.errorMessage = "formula input is not numeric";
        else if (!applyFormula(input, spec.formula, output, measurement.errorMessage)) {}
        else measurement.value = output;
    }
    measurement.passed = measurement.errorMessage.isEmpty();
    result.measurements.append(measurement);
    result.success = measurement.passed;
    if (!result.success) result.errorMessage = measurement.errorMessage;
    return result;
}

DecodeResult ResponseDecoder::decodeMany(const QByteArray& payload, const QVariantList& specs) {
    DecodeResult result;
    result.payload = payload;
    result.success = true;
    for (const auto& value : specs) {
        const auto decoded = decode(payload, DecodeSpec::fromVariantMap(value.toMap()));
        result.measurements.append(decoded.measurements);
        if (!decoded.success && result.errorMessage.isEmpty()) result.errorMessage = decoded.errorMessage;
        result.success = result.success && decoded.success;
    }
    return result;
}

bool ResponseDecoder::applyFormula(double input, const QString& expression,
                                   double& output, QString& errorMessage) {
    FormulaParser parser(expression, input, errorMessage);
    return parser.parse(output);
}

} // namespace eon::infra
