#pragma once

#include "../core/base.h"

// Derived from a type of another module: `qmltyperegistrar` is given the other
// module's type description as foreign types, so the base class is found.
class Derived : public Base {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int w READ w CONSTANT)
public:
    explicit Derived(QObject* parent = nullptr) : Base(parent) {}
    int w() const { return 8; }
};
