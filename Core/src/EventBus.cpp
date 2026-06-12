#include "eon/core/EventBus.h"

namespace eon::core {

EventBus::EventBus(QObject* parent)
    : QObject(parent) {}

void EventBus::subscribe(const QString& topic, Handler handler) {
    handlersByTopic_[topic].append(std::move(handler));
}

void EventBus::publish(const QString& topic, const QVariantMap& payload) {
    const auto handlers = handlersByTopic_.value(topic);
    for (const auto& handler : handlers) {
        handler(payload);
    }
}

} // namespace eon::core
