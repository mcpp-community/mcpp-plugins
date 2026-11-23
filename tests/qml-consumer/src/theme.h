#pragma once

#include <QtCore/QObject>
#include <QtQml/qqmlregistration.h>

// A singleton declared in C++: it needs no line in the qmldir.
class Theme : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int radius READ radius CONSTANT)
public:
    explicit Theme(QObject* parent = nullptr) : QObject(parent) {}
    int radius() const { return 9; }
};
