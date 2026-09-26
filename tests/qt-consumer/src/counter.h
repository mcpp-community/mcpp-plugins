// A meta-object in a header: `moc` reads it and writes `moc_counter.cpp`,
// which the build compiles beside the sources.
#pragma once

#include <QtCore/QObject>

class Counter : public QObject {
    Q_OBJECT
public:
    int value() const { return value_; }

public slots:
    void add(int n) {
        value_ += n;
        emit changed(value_);
    }

signals:
    void changed(int value);

private:
    int value_ = 0;
};
