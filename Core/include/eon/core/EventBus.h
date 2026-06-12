#pragma once

#include <functional>

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace eon::core {

class EventBus : public QObject {
    Q_OBJECT

public:
    using Handler = std::function<void(const QVariantMap&)>;

    explicit EventBus(QObject* parent = nullptr);

    void subscribe(const QString& topic, Handler handler);
    void publish(const QString& topic, const QVariantMap& payload = {});

private:
    QHash<QString, QList<Handler>> handlersByTopic_;
};

} // namespace eon::core
