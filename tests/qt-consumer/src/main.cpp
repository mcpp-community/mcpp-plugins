#include "counter.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QTranslator>

#include <cstdio>

// A meta-object declared in a source file: `moc` writes `main.moc`, which this
// file includes at its end.
class Relay : public QObject {
    Q_OBJECT
public:
    int seen = 0;
public slots:
    void take(int v) { seen = v; }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    Counter counter;
    Relay relay;
    QObject::connect(&counter, &Counter::changed, &relay, &Relay::take);
    counter.add(40);
    counter.add(2);

    QFile greeting(":/greeting.txt");
    if (!greeting.open(QIODevice::ReadOnly)) {
        std::puts("qt-consumer: the resource :/greeting.txt is missing");
        return 1;
    }
    const QByteArray text = greeting.readAll().trimmed();

    QTranslator translator;
    const QString dir = QCoreApplication::applicationDirPath() + "/translations";
    if (!translator.load("qt_consumer_de", dir)) {
        std::printf("qt-consumer: no translations/qt_consumer_de.qm under %s\n",
                    dir.toLocal8Bit().constData());
        return 1;
    }
    QCoreApplication::installTranslator(&translator);
    const QString hello = QCoreApplication::translate("main", "hello");

    std::printf("qt-consumer: signal %d, resource '%s', translation '%s', Qt %s\n",
                relay.seen, text.constData(), hello.toUtf8().constData(), qVersion());
    return relay.seen == 42 && text == "greetings from rcc" && hello == "hallo" ? 0 : 1;
}

#include "main.moc"
