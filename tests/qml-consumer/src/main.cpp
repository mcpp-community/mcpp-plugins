#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QTranslator>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>

#include <cstdio>

int main(int argc, char** argv) {
    // Headless on every host, unless a caller asks for something else.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    if (!qEnvironmentVariableIsSet("QT_QUICK_BACKEND")) qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);

    QTranslator translator;
    translator.load("qml_consumer_de", QCoreApplication::applicationDirPath() + "/translations");
    QCoreApplication::installTranslator(&translator);

    QFile resource(":/qt/qml/Probe/data/hello.txt");
    if (!resource.open(QIODevice::ReadOnly)) {
        std::puts("qml-consumer: the resource :/qt/qml/Probe/data/hello.txt is missing");
        return 1;
    }

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule("Probe", "Main");
    if (component.isError()) {
        for (const auto& e : component.errors()) std::printf("qml-consumer: %s\n", qPrintable(e.toString()));
        return 1;
    }
    QObject* root = component.create();
    if (!root) {
        std::puts("qml-consumer: the component did not instantiate");
        return 1;
    }
    std::printf("cpp=%d singleton=%s js=%d nested=%d resource=%s theme=%d\n",
                root->property("viaCpp").toInt(), qPrintable(root->property("viaSingleton").toString()),
                root->property("viaJs").toInt(), root->property("viaNested").toInt(),
                resource.readAll().trimmed().constData(), root->property("viaTheme").toInt());
    std::printf("tr=%s controls=%d\n", qPrintable(root->property("viaTr").toString()),
                root->property("controls").value<QObject*>() != nullptr);
    return 0;
}
