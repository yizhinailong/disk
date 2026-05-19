#pragma once

#include <QObject>
#include <QString>

namespace disk::utils {

class ClipboardBridge final : public QObject {
    Q_OBJECT

public:
    explicit ClipboardBridge(QObject* parent = nullptr);

    Q_INVOKABLE void setText(const QString& text);
};

} // namespace disk::utils
