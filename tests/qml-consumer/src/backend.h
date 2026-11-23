#pragma once

#include <QtCore/QObject>
#include <QtQml/qqmlregistration.h>

// A C++ type of the Probe module: registered by `qmltyperegistrar` from the
// description `moc --output-json` wrote.
class Backend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int n READ n CONSTANT)
public:
    explicit Backend(QObject* parent = nullptr) : QObject(parent) {}
    int n() const { return 42; }
    Q_INVOKABLE int twice(int v) const { return 2 * v; }
};
