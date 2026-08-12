// sidplayer_live.cpp — SID-Player Live-Wiedergabe (Qt6 Audio, Pull-Modell)
// Spielt eine SID-Datei in Echtzeit über QAudioSink ab.
// FIX (verifiziert mit Sinustest): isSequential() + bytesAvailable() müssen
// überschrieben sein, sonst bleibt QAudioSink im IdleState und pullt nie.
//
// Build: g++ -std=c++17 -O2 -fPIC -o sidplayer_live sidplayer_live.cpp \
//        -lsidplayfp -lresidfp $(pkg-config --cflags --libs Qt6Multimedia Qt6Gui Qt6Core) -pthread
// Usage: ./sidplayer_live <datei.sid> [subsong]

#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstring>

#include <QGuiApplication>
#include <QAudioSink>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QTimer>

#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/builders/residfp.h>

// Audio-Quelle: QIODevice, das vom QAudioSink gepullt wird
class SIDAudioSource : public QIODevice {
public:
    SIDAudioSource(sidplayfp& player, int outCh, int outBytesPerSample)
        : m_player(player), m_outCh(outCh), m_bps(outBytesPerSample) {
        m_renderBuf.resize(8192);
    }

    void start() { open(QIODevice::ReadOnly | QIODevice::Unbuffered); }
    void stop() { close(); }

    // KRITISCH: Ohne diese beiden bleibt QAudioSink im Idle und ruft readData nie auf!
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 8192; }

    qint64 readData(char* data, qint64 maxlen) override {
        // Wie viele Ausgabe-Samples passen in maxlen?
        qint64 maxOutSamples = maxlen / (m_outCh * m_bps);
        if (maxOutSamples <= 0) return 0;

        qint64 outSamples = 0;
        while (outSamples < maxOutSamples) {
            // Render-Chunk in SID-Samples (PAL-CPU 985248 Hz)
            unsigned int want = static_cast<unsigned int>(std::min<qint64>(m_renderBuf.size(), maxOutSamples - outSamples));
            unsigned int cycles = static_cast<unsigned int>(want * 985248ULL / 48000);
            int produced = m_player.play(cycles);
            if (produced <= 0) break;
            unsigned int mixed = m_player.mix(m_renderBuf.data(), static_cast<unsigned int>(produced));
            if (mixed == 0) break;

            // Mono-Samples → Ausgabeformat konvertieren
            for (unsigned int i = 0; i < mixed; ++i) {
                short s = m_renderBuf[i];
                if (m_outCh == 2) {
                    for (int c = 0; c < 2; ++c) {
                        writeSample(data, (outSamples * m_outCh + c) * m_bps, s);
                    }
                } else {
                    writeSample(data, outSamples * m_bps, s);
                }
                outSamples++;
            }
        }
        return outSamples * m_outCh * m_bps;
    }

    qint64 writeData(const char*, qint64) override { return -1; }

private:
    void writeSample(char* data, qint64 offset, short s) {
        if (m_bps == 2) {
            memcpy(data + offset, &s, 2);
        } else if (m_bps == 4) {
            int32_t v = static_cast<int32_t>(s) << 16;  // 16-bit → 32-bit
            memcpy(data + offset, &v, 4);
        }
    }

    sidplayfp& m_player;
    int m_outCh;
    int m_bps;
    std::vector<short> m_renderBuf;
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <datei.sid> [subsong]" << std::endl;
        return 1;
    }

    QGuiApplication app(argc, argv);

    std::string filename = argv[1];
    int subsong = (argc > 2) ? std::stoi(argv[2]) : 1;

    // ── SID laden ──
    SidTune tune(filename.c_str());
    if (!tune.getStatus()) {
        std::cerr << "FEHLER: SID konnte nicht geladen werden." << std::endl;
        return 1;
    }

    const SidTuneInfo* info = tune.getInfo();
    std::cout << "Titel: " << info->infoString(0)
              << " | Autor: " << info->infoString(1)
              << " | Subsongs: " << info->songs() << std::endl;

    if (subsong >= 1 && subsong <= static_cast<int>(info->songs())) {
        tune.selectSong(subsong - 1);
        std::cout << "Spiele Subsong " << subsong << std::endl;
    }

    // ── Audio-Gerät + Format ──
    const QAudioDevice& outDev = QMediaDevices::defaultAudioOutput();
    if (outDev.isNull()) {
        std::cerr << "FEHLER: Kein Audio-Ausgabegerät." << std::endl;
        return 1;
    }
    std::cout << "Gerät: " << outDev.description().toStdString() << std::endl;

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);  // SPDIF-kompatibel

    if (!outDev.isFormatSupported(format)) {
        std::cout << "16-bit nicht unterstützt, nutze Geräte-preferred." << std::endl;
        format = outDev.preferredFormat();
    }
    std::cout << "Format: " << format.sampleRate() << "Hz, "
              << format.channelCount() << "ch, "
              << (format.sampleFormat() == QAudioFormat::Int32 ? "s32le" :
                  format.sampleFormat() == QAudioFormat::Int16 ? "s16le" : "?")
              << std::endl;

    int outCh = format.channelCount();
    int bps = (format.sampleFormat() == QAudioFormat::Int32) ? 4 : 2;

    // ── Player konfigurieren (48kHz, mono rendern, Ausgabe konvertiert) ──
    ReSIDfpBuilder sidBuilder("reSIDfp");
    sidplayfp player;
    SidConfig config;
    config.sidEmulation = &sidBuilder;
    config.defaultC64Model = SidConfig::PAL;
    config.forceC64Model = true;
    config.defaultSidModel = SidConfig::MOS6581;
    config.forceSidModel = false;
    config.frequency = 48000;
    config.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;

    bool stereo = false;
    if (info->sidChips() >= 2) {
        config.secondSidAddress = 0xD420;
        stereo = true;
        std::cout << "Stereo (2. SID @ 0xD420)" << std::endl;
    }
    if (info->sidChips() >= 3) {
        config.thirdSidAddress = 0xD440;
        std::cout << "Triple-SID (3. SID @ 0xD440)" << std::endl;
    }

    if (!player.config(config) || !player.load(&tune)) {
        std::cerr << "FEHLER: " << player.error() << std::endl;
        return 1;
    }
    player.initMixer(false);  // mono rendern, Stereo-Duplikation im Source

    // ── Audio-Start ──
    SIDAudioSource source(player, outCh, bps);
    QAudioSink audioSink(outDev, format);
    audioSink.setBufferSize(65536);
    source.start();
    audioSink.start(&source);

    std::cout << "▶ Wiedergabe läuft... (Ctrl+C zum Beenden)" << std::endl;

    // Event-Loop: QAudioSink pullt nur mit laufendem exec()!
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, []() {});
    timer.start(500);
    return app.exec();
}
