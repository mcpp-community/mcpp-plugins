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
    // Qt's own strings, from the qt_de.qm rules-qt combined: it loads only
    // when no catalog it depends on is missing.
    QTranslator qtTranslator;
    if (!qtTranslator.load("qt_de", dir)) {
        std::printf("qt-consumer: translations/qt_de.qm does not load\n");
        return 1;
    }
    QCoreApplication::installTranslator(&qtTranslator);
    const QString hello  = QCoreApplication::translate("main", "hello");
    // Through variables, so lupdate does not take Qt's own string into this
    // program's .ts: it extracts literals only.
    const char* qtContext = "QProgressDialog";
    const char* qtSource  = "Cancel";
    const QString cancel = QCoreApplication::translate(qtContext, qtSource);

    std::printf("qt-consumer: signal %d, resource '%s', translation '%s', Qt %s, Qt's own '%s'\n",
                relay.seen, text.constData(), hello.toUtf8().constData(), qVersion(),
                cancel.toUtf8().constData());
    return relay.seen == 42 && text == "greetings from rcc" && hello == "hallo" &&
           cancel == "Abbrechen" ? 0 : 1;
}

#include "main.moc"
