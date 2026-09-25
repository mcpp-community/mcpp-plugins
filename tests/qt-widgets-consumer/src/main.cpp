#include "ui_form.h"

#include <QtGui/QGuiApplication>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#include <cstdio>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QWidget window;
    Ui::Form form;
    form.setupUi(&window);
    window.show();
    const QByteArray text = form.label->text().toUtf8();
    std::printf("qt-widgets-consumer: platform %s, label '%s'\n",
                QGuiApplication::platformName().toUtf8().constData(), text.constData());
    return text == "made by uic" ? 0 : 1;
}
