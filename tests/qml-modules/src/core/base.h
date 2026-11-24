#pragma once

#include <QtCore/QObject>
#include <QtQml/qqmlregistration.h>

class Base : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int v READ v CONSTANT)
public:
    explicit Base(QObject* parent = nullptr) : QObject(parent) {}
    int v() const { return 7; }
};
