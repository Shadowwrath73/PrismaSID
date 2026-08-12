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

    // QML relativ zum Binary suchen: qml/main.qml neben src/, dann installierter/AppDir-Pfad
    QString qmlPath;
    const QStringList candidates = {
        QFileInfo(QCoreApplication::applicationDirPath() + "/../qml/main.qml").absoluteFilePath(),
        QFileInfo(QCoreApplication::applicationDirPath() + "/qml/main.qml").absoluteFilePath(),
        QFileInfo(QCoreApplication::applicationDirPath() + "/../share/prismasid/qml/main.qml").absoluteFilePath(),  // AppImage-Layout
        QFileInfo("qml/main.qml").absoluteFilePath(),
        QFileInfo("main.qml").absoluteFilePath(),
        QStringLiteral("/usr/share/prismasid/qml/main.qml"),
        QStringLiteral("/usr/local/share/prismasid/qml/main.qml")
    };
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c)) {
            qmlPath = c;
            break;
        }
    }
    if (qmlPath.isEmpty()) {
        qWarning("PrismaSID: main.qml nicht gefunden — Pfade geprüft, breche ab.");
        return -2;
    }

    engine.load(QUrl::fromLocalFile(qmlPath));
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
