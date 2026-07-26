#pragma once

#include <QByteArray>
#include <QVariantList>
#include <QString>

#include "eon/infra/DecodeTypes.h"

namespace eon::infra {

/// Protocol-independent extraction and conversion of an already normalized payload.
class ResponseDecoder {
public:
    static DecodeResult decode(const QByteArray& payload,
                               const DecodeSpec& spec);
    static DecodeResult decodeMany(const QByteArray& payload,
                                   const QVariantList& specs);

private:
    static bool applyFormula(double input, const QString& expression,
                             double& output, QString& errorMessage);
};

} // namespace eon::infra
