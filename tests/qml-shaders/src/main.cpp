#include <QtCore/QFile>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>

#include <cstdio>

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    if (!qEnvironmentVariableIsSet("QT_QUICK_BACKEND")) qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);

    // The .qsb `qsb` wrote is in the module's resources.
    QFile qsb(":/qt/qml/Shade/shaders/wave.frag.qsb");
    if (!qsb.open(QIODevice::ReadOnly)) {
        std::puts("qml-shaders: the resource :/qt/qml/Shade/shaders/wave.frag.qsb is missing");
        return 1;
    }
    const QByteArray bytes = qsb.readAll();

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule("Shade", "Main");
    if (component.isError()) {
        for (const auto& e : component.errors()) std::printf("qml-shaders: %s\n", qPrintable(e.toString()));
        return 1;
    }
    QObject* root = component.create();
    if (!root) return 1;
    std::printf("qsb=%lld bytes shader=%s\n", static_cast<long long>(bytes.size()),
                qPrintable(root->property("shader").toUrl().toString()));
    return 0;
}
