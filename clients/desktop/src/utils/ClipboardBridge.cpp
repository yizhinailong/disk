#include "utils/ClipboardBridge.hpp"

#include <QClipboard>
#include <QGuiApplication>

namespace disk::utils {

ClipboardBridge::ClipboardBridge(QObject* parent) : QObject(parent) {}

void ClipboardBridge::setText(const QString& text) {
    QGuiApplication::clipboard()->setText(text);
}

} // namespace disk::utils
