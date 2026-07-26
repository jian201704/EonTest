#include "eon/sdk/Dut.h"

namespace eon::sdk {

Dut::Dut(const QString& id, const QString& model)
    : dutId_(id)
    , modelName_(model)
{
}

bool Dut::open() {
    connected_ = true;
    return true;
}

void Dut::close() {
    connected_ = false;
}

} // namespace eon::sdk
