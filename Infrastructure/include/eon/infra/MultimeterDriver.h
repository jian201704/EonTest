#pragma once

#include <eon/sdk/InstrumentDriver.h>
#include <memory>

namespace eon::infra {

class MultimeterDriver final : public eon::sdk::InstrumentDriver {
    Q_OBJECT
public:
    explicit MultimeterDriver(QObject* parent = nullptr);
    ~MultimeterDriver() override;

    QString driverType() const override { return "measure.voltage"; }

    bool open(eon::sdk::IBackend* backend, const QVariantMap& config, QString& errorMessage) override;
    void close() override;
    bool isOpen() const override;

    bool execute(const QString& command, const QVariantMap& params, QString& errorMessage) override;
    QVariantMap query(const QString& command, const QVariantMap& params, QString& errorMessage) override;

    QString identity() const override;
    QString scpiTrace() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eon::infra
