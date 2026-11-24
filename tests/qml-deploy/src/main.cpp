#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>

#include <cstdio>

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    if (!qEnvironmentVariableIsSet("QT_QUICK_BACKEND")) qputenv("QT_QUICK_BACKEND", "software");
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE")) qputenv("QT_QUICK_CONTROLS_STYLE", "Material");
    QGuiApplication app(argc, argv);

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule("App", "Main");
    if (component.isError()) {
        for (const auto& e : component.errors()) std::printf("qml-deploy: %s\n", qPrintable(e.toString()));
        return 1;
    }
    QObject* root = component.create();
    if (!root) {
        for (const auto& e : component.errors()) std::printf("qml-deploy: %s\n", qPrintable(e.toString()));
        return 1;
    }
    std::printf("layout=%d gadget=%d shape=%d button=%d\n", root->property("layoutWidth").toInt(),
                root->property("gadget").toInt(), root->property("shape").value<QObject*>() != nullptr,
                root->property("button").value<QObject*>() != nullptr);
    return 0;
}
