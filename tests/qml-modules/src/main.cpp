#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>

#include <cstdio>

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    if (!qEnvironmentVariableIsSet("QT_QUICK_BACKEND")) qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule("app.ui", "Main");
    if (component.isError()) {
        for (const auto& e : component.errors()) std::printf("qml-modules: %s\n", qPrintable(e.toString()));
        return 1;
    }
    QObject* root = component.create();
    if (!root) return 1;
    // `viaBase` reads a property declared in the other module's C++ base class.
    std::printf("sum=%d viaBase=%d label=%s\n", root->property("sum").toInt(), root->property("viaBase").toInt(),
                qPrintable(root->property("label").toString()));
    return 0;
}
