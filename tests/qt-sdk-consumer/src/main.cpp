// Prints the version of the QtCore it loaded.
#include <QtCore/QtGlobal>

#include <cstdio>

int main() {
    std::printf("qt-sdk-consumer: Qt %s\n", qVersion());
    return 0;
}
