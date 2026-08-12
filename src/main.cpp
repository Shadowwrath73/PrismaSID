// main.cpp — SID-Player App-Start (Qt6 QML)
// Lädt das Backend und die QML-Oberfläche.
//
// Build: g++ -std=c++17 -O2 -fPIC -o sidplayer main.cpp \
//        -lsidplayfp -lresidfp $(pkg-config --cflags --libs Qt6Multimedia Qt6Gui Qt6Qml Qt6Quick Qt6Core) -pthread
// Run:   QT_QPA_PLATFORM=wayland ./sidplayer [datei.sid]

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QFileInfo>

#include "sidplayer_backend.h"
#include "waveform_item.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    qmlRegisterType<WaveformItem>("SidPlayer", 1, 0, "WaveformItem");

    SIDPlayerBackend backend;

    // Optional: SID als Argument laden + direkt abspielen
    if (argc > 1) {
        backend.loadFile(QString::fromLocal8Bit(argv[1]));
        backend.play();
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("sidBackend", &backend);

    // QML relativ zum Binary suchen: qml/main.qml neben src/
    QString qmlPath = QFileInfo(QCoreApplication::applicationDirPath() + "/../qml/main.qml").absoluteFilePath();
    if (!QFileInfo::exists(qmlPath)) {
        qmlPath = QFileInfo(QCoreApplication::applicationDirPath() + "/qml/main.qml").absoluteFilePath();
    }
    if (!QFileInfo::exists(qmlPath)) {
        qmlPath = QFileInfo("qml/main.qml").absoluteFilePath();
    }
    if (!QFileInfo::exists(qmlPath)) {
        qmlPath = QFileInfo("main.qml").absoluteFilePath();
    }

    engine.load(QUrl::fromLocalFile(qmlPath));
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
