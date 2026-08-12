// sidplayer_backend.h — C++-Backend für die QML-UI
// Kapselt libsidplayfp als QObject: Properties für QML, Live-Audio via QAudioSink,
// Wellenform-Daten als Signal für die UI, Stereo/Mono-Umschaltung, Playlist.

#ifndef SIDPLAYER_BACKEND_H
#define SIDPLAYER_BACKEND_H

#include <QObject>
#include <QVariant>
#include <QAudioSink>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QTimer>
#include <QVector>
#include <QFile>
#include <QUrl>
#include <QByteArray>
#include <QStringList>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QColor>
#include <QFile>
#include <QUrl>
#include <QRegularExpression>
#include <QProcess>
#include <QtConcurrent>
#include <QMetaObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>

#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/builders/residfp.h>

// Audio-Quelle (QIODevice für QAudioSink-Pull) — RENDER-THREAD + RINGPUFFER
// Ein Hintergrund-Thread rendert kontinuierlich SID-Samples in einen großen
// Ringpuffer; readData() liest nur noch daraus → keine Underruns, kein Stocken.
class SIDAudioSource : public QIODevice {
public:
    SIDAudioSource(sidplayfp& player, int outCh, int outBytesPerSample, bool stereoMode)
        : m_player(player), m_outCh(outCh), m_bps(outBytesPerSample), m_stereoMode(stereoMode) {
        m_renderBuf.resize(8192);
        // Ringpuffer: 32 Blöcke à 4096 Frames ≈ 2.7s @48kHz — Reserve für Scheduling-Lücken
        m_ring.resize(32 * 4096);
        // Einträge pro Audio-Frame: Stereo = L+R interleaved (2), Mono = 1
        m_ringPerFrame = m_stereoMode ? 2 : 1;
        // Ziel-Reserve: ~200ms Audio im Ring (kein Voraus-Rendern, kein Verhungern)
        m_ringTarget = 200 * 48000 / 1000 * m_ringPerFrame;
    }

    ~SIDAudioSource() {
        stopRenderThread();
    }

    void start() {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        m_ringRead = 0;
        m_ringWrite = 0;
        m_ringFull = false;
        m_renderRunning = true;
        m_renderThread = std::thread(&SIDAudioSource::renderLoop, this);
    }

    void stop() {
        stopRenderThread();
        close();
    }

    void stopRenderThread() {
        if (m_renderThread.joinable()) {
            m_renderRunning = false;
            m_renderThread.join();
        }
    }

    // KRITISCH: Ohne diese beiden bleibt QAudioSink im Idle und ruft readData nie auf!
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 8192; }

    // Wellenform-Callback (lock-frei, nur memcpy)
    std::function<void(const float*, int)> onWaveform;

    // ── DSP-Effekte (Reverb/Echo/Spatial) — nach dem mix(), vor dem Ring ──
    // Parameter (0.0–1.0): werden vom Backend gesetzt, vom Render-Thread gelesen
    std::atomic<float> fxReverb{0.0f};   // Feedback-Reverb-Menge
    std::atomic<float> fxEcho{0.0f};     // Echo/Delay-Menge
    std::atomic<float> fxSpatial{0.0f};  // Stereo-Widening (Haas)

    // Delay-Puffer (Echo + Reverb-Feedback), 1.5s @48kHz, 2 Kanäle
    std::vector<float> m_fxDelayL = std::vector<float>(72000, 0.0f);  // 1.5s
    std::vector<float> m_fxDelayR = std::vector<float>(72000, 0.0f);
    int m_fxPos = 0;

    // Spatial-Delay (Haas): 12ms Versatz, 2 Kanäle
    std::vector<float> m_haasL = std::vector<float>(576, 0.0f);  // 12ms
    std::vector<float> m_haasR = std::vector<float>(576, 0.0f);
    int m_haasPos = 0;

    // Effekt-Parameter setzen (thread-safe via atomics)
    void setEffects(float reverb, float echo, float spatial) {
        fxReverb.store(qBound(0.0f, reverb, 1.0f));
        fxEcho.store(qBound(0.0f, echo, 1.0f));
        fxSpatial.store(qBound(0.0f, spatial, 1.0f));
    }

    // DSP-Verarbeitung eines Sample-Paars (L, R in-place)
    void applyFx(float& l, float& r) {
        const float reverb = fxReverb.load();
        const float echo = fxEcho.load();
        const float spatial = fxSpatial.load();
        if (reverb < 0.001f && echo < 0.001f && spatial < 0.001f) return;

        // Delay-Lese (Echo + Reverb-Feedback) — 300ms Echo, 200ms Reverb-Feedback
        const int echoDelay = 48000 * 3 / 10;       // 300ms
        const int reverbDelay = 48000 * 2 / 10;     // 200ms
        int eIdx = m_fxPos - echoDelay;
        if (eIdx < 0) eIdx += 72000;
        float eL = m_fxDelayL[eIdx];
        float eR = m_fxDelayR[eIdx];
        int rIdx = m_fxPos - reverbDelay;
        if (rIdx < 0) rIdx += 72000;
        float fbL = m_fxDelayL[rIdx];
        float fbR = m_fxDelayR[rIdx];

        // Echo: Eingang + Feedback (Dämpfung 0.35)
        float outL = l + echo * (eL + 0.35f * fbL);
        float outR = r + echo * (eR + 0.35f * fbR);

        // Reverb: Feedback-Delay mit Dämpfung, auf das Signal mischen
        if (reverb > 0.001f) {
            outL += reverb * (0.55f * fbL);
            outR += reverb * (0.55f * fbR);
        }

        // In Delay-Puffer schreiben (mit Reverb-Feedback)
        m_fxDelayL[m_fxPos] = l + 0.5f * reverb * fbL;
        m_fxDelayR[m_fxPos] = r + 0.5f * reverb * fbR;
        m_fxPos = (m_fxPos + 1) % 72000;

        // Spatial (Haas): 12ms Verzögerung, leichtes Cross-Mix → breiter
        if (spatial > 0.001f) {
            float hL = m_haasL[m_haasPos];
            float hR = m_haasR[m_haasPos];
            m_haasL[m_haasPos] = outL;
            m_haasR[m_haasPos] = outR;
            m_haasPos = (m_haasPos + 1) % 576;
            // Original + verzögertes Gegen-Kanal-Signal (Haas-Effekt)
            outL = outL + spatial * 0.5f * hR;
            outR = outR + spatial * 0.5f * hL;
        }

        // Sanfte Begrenzung (kein Clipping-Kratzen)
        const float lim = 1.0f;
        outL = qBound(-lim, outL, lim);
        outR = qBound(-lim, outR, lim);

        l = outL;
        r = outR;
    }

    // ── Render-Thread: rendert SID-Samples NACH, wenn die Reserve sinkt ──
    void renderLoop() {
        while (m_renderRunning) {
            // Wieviel Audio-Reserve ist im Ring (in Einträgen)?
            int used = ringUsed();

            if (used >= m_ringTarget) {
                // Genug vorgerendert → schlafe proportional zur Reserve (kein Busy-Loop!)
                // z.B. 200ms Reserve = 200ms schlafen; minimal 2ms, max 250ms
                int reserveMs = (used - m_ringTarget) * 1000 / (48000 * m_ringPerFrame);
                reserveMs = qBound(2, reserveMs, 250);
                std::this_thread::sleep_for(std::chrono::milliseconds(reserveMs));
                continue;
            }

            // Reserve unter Ziel → einen Block nachrendern
            unsigned int want = (unsigned int)m_renderBuf.size();
            unsigned int cycles = static_cast<unsigned int>(want * 985248ULL / 48000);
            int produced = m_player.play(cycles);
            if (produced <= 0) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
            unsigned int mixed = m_player.mix(m_renderBuf.data(), (unsigned int)produced);
            if (mixed == 0) break;

            // DSP-Effekte auf die Samples anwenden (L/R-Paare)
            const bool stereo = m_stereoMode && m_outCh >= 2;
            if (stereo) {
                for (unsigned int i = 0; i + 1 < mixed; i += 2) {
                    float l = static_cast<float>(m_renderBuf[i]) / 32768.0f;
                    float r = static_cast<float>(m_renderBuf[i+1]) / 32768.0f;
                    applyFx(l, r);
                    m_renderBuf[i] = static_cast<short>(qBound(-32768.0f, l * 32768.0f, 32767.0f));
                    m_renderBuf[i+1] = static_cast<short>(qBound(-32768.0f, r * 32768.0f, 32767.0f));
                }
            } else {
                // Mono: Effekt auf beide (identische) Samples
                for (unsigned int i = 0; i < mixed; ++i) {
                    float s = static_cast<float>(m_renderBuf[i]) / 32768.0f;
                    float l = s, r = s;
                    applyFx(l, r);
                    m_renderBuf[i] = static_cast<short>(qBound(-32768.0f, ((l + r) * 0.5f) * 32768.0f, 32767.0f));
                }
            }

            // In Ringpuffer schreiben
            for (unsigned int i = 0; i < mixed; ++i) {
                ringPush(m_renderBuf[i]);
            }
        }
    }

    qint64 readData(char* data, qint64 maxlen) override {
        qint64 maxFrames = maxlen / (m_outCh * m_bps);
        if (maxFrames <= 0) return 0;

        qint64 frames = 0;
        int waveIdx = 0;

        if (m_stereoMode && m_outCh >= 2) {
            // Echtes Stereo: Ring enthält L/R-interleaved Samples → L auf links, R auf rechts
            while (frames < maxFrames) {
                if (ringEmpty()) break;
                short l = ringPop();
                if (ringEmpty()) break;  // kein zweites Sample mehr — sauber stoppen, kein Müll
                short r = ringPop();
                writeSample(data, (frames * m_outCh + 0) * m_bps, l);
                writeSample(data, (frames * m_outCh + 1) * m_bps, r);
                if (onWaveform && (frames % 16 == 0) && waveIdx < 1024) {
                    m_waveTmp[waveIdx++] = (static_cast<float>(l) + static_cast<float>(r)) / 65536.0f;
                }
                frames++;
            }
        } else {
            // Mono zentriert: gleiches Signal auf beide Kanäle
            while (frames < maxFrames) {
                if (ringEmpty()) break;
                short s = ringPop();
                if (onWaveform && (frames % 16 == 0) && waveIdx < 1024) {
                    m_waveTmp[waveIdx++] = static_cast<float>(s) / 32768.0f;
                }
                for (int c = 0; c < m_outCh; ++c) {
                    writeSample(data, (frames * m_outCh + c) * m_bps, s);
                }
                frames++;
            }
        }

        if (waveIdx > 0 && onWaveform) {
            onWaveform(m_waveTmp, waveIdx);
        }
        return frames * m_outCh * m_bps;
    }

    qint64 writeData(const char*, qint64) override { return -1; }

private:
    // ── SPSC-Ringpuffer (lock-frei, ein Producer / ein Consumer) ──
    std::vector<short> m_ring;
    std::atomic<int> m_ringRead{0};
    std::atomic<int> m_ringWrite{0};
    std::atomic<bool> m_ringFull{false};
    int m_ringTarget = 0;
    int m_ringPerFrame = 1;
    float m_waveTmp[1024];

    int ringUsed() const {
        if (m_ringFull.load(std::memory_order_acquire)) return (int)m_ring.size();
        int r = m_ringRead.load(std::memory_order_acquire);
        int w = m_ringWrite.load(std::memory_order_acquire);
        return (w - r + (int)m_ring.size()) % (int)m_ring.size();
    }
    int ringSpace() const {
        if (m_ringFull.load(std::memory_order_acquire)) return 0;
        int r = m_ringRead.load(std::memory_order_acquire);
        int w = m_ringWrite.load(std::memory_order_acquire);
        int used = (w - r + (int)m_ring.size()) % (int)m_ring.size();
        return (int)m_ring.size() - used;
    }
    bool ringEmpty() const {
        return !m_ringFull.load(std::memory_order_acquire)
            && m_ringRead.load(std::memory_order_acquire) == m_ringWrite.load(std::memory_order_acquire);
    }
    void ringPush(short s) {
        int w = m_ringWrite.load(std::memory_order_relaxed);
        m_ring[w] = s;
        int next = (w + 1) % (int)m_ring.size();
        if (next == m_ringRead.load(std::memory_order_acquire)) {
            m_ringFull.store(true, std::memory_order_release);
            // Überlauf: ältestes Sample überspringen (Consumer liest nach)
        }
        m_ringWrite.store(next, std::memory_order_release);
    }
    short ringPop() {
        int r = m_ringRead.load(std::memory_order_relaxed);
        short s = m_ring[r];
        int next = (r + 1) % (int)m_ring.size();
        m_ringRead.store(next, std::memory_order_release);
        m_ringFull.store(false, std::memory_order_release);
        return s;
    }

    void writeSample(char* data, qint64 offset, short s) {
        if (m_bps == 2) {
            memcpy(data + offset, &s, 2);
        } else if (m_bps == 4) {
            int32_t v = static_cast<int32_t>(s) << 16;
            memcpy(data + offset, &v, 4);
        }
    }

    sidplayfp& m_player;
    int m_outCh;
    int m_bps;
    bool m_stereoMode;
    std::vector<short> m_renderBuf;
    std::thread m_renderThread;
    std::atomic<bool> m_renderRunning{false};
};

// ── Backend: verbindet libsidplayfp mit QML ──
class SIDPlayerBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY songChanged)
    Q_PROPERTY(QString author READ author NOTIFY songChanged)
    Q_PROPERTY(QString copyright READ copyright NOTIFY songChanged)
    Q_PROPERTY(int subsongs READ subsongs NOTIFY songChanged)
    Q_PROPERTY(int currentSubsong READ currentSubsong NOTIFY subsongChanged)
    Q_PROPERTY(int sidChips READ sidChips NOTIFY songChanged)
    Q_PROPERTY(QString chipModel READ chipModel NOTIFY chipModelChanged)
    Q_PROPERTY(QString c64Model READ c64Model NOTIFY songChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY songChanged)
    Q_PROPERTY(int tuneLengthSec READ tuneLengthSec NOTIFY songChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY songChanged)
    Q_PROPERTY(bool stereoMode READ stereoMode WRITE setStereoMode NOTIFY stereoModeChanged)
    Q_PROPERTY(int playlistCount READ playlistCount NOTIFY playlistChanged)
    Q_PROPERTY(int playlistIndex READ playlistIndex NOTIFY playlistChanged)
    Q_PROPERTY(QStringList playlist READ playlist NOTIFY playlistChanged)
    Q_PROPERTY(QString currentDir READ currentDir NOTIFY dirChanged)
    Q_PROPERTY(QVariantList dirEntries READ dirEntries NOTIFY dirChanged)
    Q_PROPERTY(QVariantList places READ places NOTIFY placesChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool coverMode READ coverMode WRITE setCoverMode NOTIFY coverModeChanged)
    // DSP-Effekte (0.0–1.0)
    Q_PROPERTY(float fxReverb READ fxReverb WRITE setFxReverb NOTIFY fxChanged)
    Q_PROPERTY(float fxEcho READ fxEcho WRITE setFxEcho NOTIFY fxChanged)
    Q_PROPERTY(float fxSpatial READ fxSpatial WRITE setFxSpatial NOTIFY fxChanged)

public:
    explicit SIDPlayerBackend(QObject* parent = nullptr)
        : QObject(parent), m_tune(nullptr), m_audioSource(nullptr) {
        m_waveTimer = new QTimer(this);
        m_waveTimer->setInterval(33);  // ~30 FPS
        connect(m_waveTimer, &QTimer::timeout, this, [this]() {
            // Letzte 512 Samples aus dem Wellenform-Ring lesen (immer volle Kurve)
            const int TARGET = 512;
            QByteArray out;
            out.resize(TARGET * (int)sizeof(float));
            float* dst = reinterpret_cast<float*>(out.data());
            {
                std::lock_guard<std::mutex> lock(m_waveMutex);
                int avail = m_waveRingCount;
                if (avail < TARGET) {
                    // Noch nicht genug Samples: vorn mit Nullen auffüllen
                    int start = avail;
                    for (int i = 0; i < TARGET - avail; ++i) dst[i] = 0.0f;
                    for (int i = 0; i < avail; ++i) {
                        dst[TARGET - avail + i] = m_waveRing[(m_waveRingStart + i) % WAVE_RING_SIZE];
                    }
                    (void)start;
                } else {
                    int start = (m_waveRingStart + avail - TARGET) % WAVE_RING_SIZE;
                    for (int i = 0; i < TARGET; ++i) {
                        dst[i] = m_waveRing[(start + i) % WAVE_RING_SIZE];
                    }
                }
            }
            emit waveformReady(out);
        });
        m_waveTimer->start();
        loadPlaces();  // Plasma-Orte aus user-places.xbel laden
        loadPlaylist();  // Letzte Playlist wiederherstellen

        // Aktives Audiogerät überwachen: Wechsel (Kopfhörer→Lautsprecher etc.)
        // → Wiedergabe automatisch aufs neue Gerät umziehen
        m_devices = new QMediaDevices(this);
        connect(m_devices, &QMediaDevices::audioOutputsChanged, this, [this]() {
            const QAudioDevice& dev = QMediaDevices::defaultAudioOutput();
            if (dev.isNull()) return;
            if (m_lastDeviceId != dev.id() && m_isPlaying) {
                m_lastDeviceId = dev.id();
                // Wiedergabe auf neuem Gerät neu starten (aktuellen Song behalten)
                QString cur = m_filePath;
                bool wasPlaying = m_isPlaying;
                stop();
                if (wasPlaying && !cur.isEmpty()) {
                    loadFile(cur);
                    play();
                }
            } else {
                m_lastDeviceId = dev.id();
            }
        });
        m_lastDeviceId = QMediaDevices::defaultAudioOutput().id();
    }

    ~SIDPlayerBackend() {
        stop();
        delete m_tune;
    }

    QString title() const { return m_title; }
    QString author() const { return m_author; }
    QString copyright() const { return m_copyright; }
    int subsongs() const { return m_subsongs; }
    int currentSubsong() const { return m_currentSubsong; }
    int sidChips() const { return m_sidChips; }
    QString chipModel() const { return m_chipModel; }
    bool isPlaying() const { return m_isPlaying; }
    QString filePath() const { return m_filePath; }
    QString fileName() const { return m_fileName; }
    QString c64Model() const { return m_c64Model; }
    int tuneLengthSec() const { return m_tuneLengthSec; }
    bool stereoMode() const { return m_stereoMode; }
    int playlistCount() const { return m_playlist.size(); }
    int playlistIndex() const { return m_playlistIndex; }
    QStringList playlist() const { return m_playlist; }

    void setStereoMode(bool on) {
        if (m_stereoMode == on) return;
        m_stereoMode = on;
        emit stereoModeChanged();
        if (m_isPlaying) {
            stop();
            play();
        }
    }

    QString currentDir() const { return m_currentDir; }
    QVariantList dirEntries() const { return m_dirEntries; }
    QVariantList places() const { return m_places; }
    bool loading() const { return m_loading; }
    bool coverMode() const { return m_coverMode; }
    void setCoverMode(bool on) {
        if (on != m_coverMode) {
            m_coverMode = on;
            emit coverModeChanged();
            if (!m_currentDir.isEmpty()) {
                // Ordner neu laden, damit Cover erscheinen/verschwinden
                if (m_currentDir.startsWith("smb://")) openPlace(m_currentDir);
                else setDir(m_currentDir);
            }
        }
    }

    // ── Plasma-Orte laden (user-places.xbel — KDE/Dolphin) ──
    Q_INVOKABLE void loadPlaces() {
        m_places.clear();
        m_places.append(QVariantMap{{"name", "🏠 Home"}, {"path", QDir::homePath()}});

        // KIO verfügbar? (kioclient5 = KDE-Komponente — auf GNOME/anderen Desktops fehlt sie)
        const bool haveKio = !QStandardPaths::findExecutable("kioclient5").isEmpty();

        // Benutzer-Orte aus KDEs xbel-Datei (file:// UND smb:// — smb nur mit KIO)
        const QString xbel = QDir::homePath() + "/.local/share/user-places.xbel";
        QFile f(xbel);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray data = f.readAll();
            f.close();
            QRegularExpression re("<bookmark[^>]*href=\"([^\"]+)\"[^>]*>\\s*<title>([^<]+)</title>");
            QRegularExpressionMatchIterator it = re.globalMatch(QString::fromUtf8(data));
            while (it.hasNext()) {
                QRegularExpressionMatch m = it.next();
                QString href = m.captured(1);
                QString name = m.captured(2);
                QString path = href;
                if (href.startsWith("file://")) path = href.mid(7);
                else if (href.startsWith("smb://")) {
                    if (!haveKio) continue;  // Samba-Orte ohne KIO nicht anbieten (würden fehlschlagen)
                }
                else continue;  // nur file+smb

                // Duplikate vermeiden
                bool dup = false;
                for (const QVariant& p : m_places) {
                    if (p.toMap()["path"] == path) { dup = true; break; }
                }
                if (!dup) {
                    QVariantMap entry{{"name", name}, {"path", path}};
                    if (href.startsWith("smb://")) entry["isSmb"] = true;
                    m_places.append(entry);
                }
            }
        }

        // NAS als System-Ort (falls gemountet)
        if (QDir("/mnt/nas").exists()) {
            bool hasNas = false;
            for (const QVariant& p : m_places) {
                if (p.toMap()["path"] == "/mnt/nas") { hasNas = true; break; }
            }
            if (!hasNas) m_places.append(QVariantMap{{"name", "🖴 NAS"}, {"path", "/mnt/nas"}});
        }
        emit placesChanged();
    }

    // ── smb://-Ort öffnen über KIO — ASYNCHRON im Worker-Thread (UI bleibt flüssig) ──
    float fxReverb() const { return m_fxReverb; }
    void setFxReverb(float v) { setFx(m_fxReverb, qBound(0.0f, v, 1.0f)); }
    float fxEcho() const { return m_fxEcho; }
    void setFxEcho(float v) { setFx(m_fxEcho, qBound(0.0f, v, 1.0f)); }
    float fxSpatial() const { return m_fxSpatial; }
    void setFxSpatial(float v) { setFx(m_fxSpatial, qBound(0.0f, v, 1.0f)); }

private:
    void setFx(float& member, float v) {
        if (qFuzzyCompare(member, v)) return;
        member = v;
        if (m_audioSource) m_audioSource->setEffects(m_fxReverb, m_fxEcho, m_fxSpatial);
        emit fxChanged();
    }

public:
    // WICHTIG: Q_INVOKABLE! Ohne das kann QML openPlace nicht aufrufen (TypeError)
    Q_INVOKABLE void openPlace(const QString& path) {
        if (path.startsWith("smb://")) {
            m_currentDir = path;
            m_dirEntries.clear();
            m_loading = true;
            emit loadingChanged();
            emit dirChanged();

            QtConcurrent::run([this, path]() {
                QVariantList entries = loadSmbEntries(path);
                // Ergebnis sicher in den GUI-Thread zurückholen
                QMetaObject::invokeMethod(this, [this, entries]() {
                    m_dirEntries = entries;
                    m_loading = false;
                    emit loadingChanged();
                    emit dirChanged();
                }, Qt::QueuedConnection);
            });
        } else {
            setDir(path);
        }
    }

    // Läuft im Worker-Thread — darf NICHT die UI berühren
    QVariantList loadSmbEntries(const QString& path) {
        QVariantList entries;
        // kioclient5 ls — listet mit KIO (nutzt KWallet-Credentials)
        QProcess p;
        p.start("kioclient5", QStringList() << "ls" << path);
        p.waitForFinished(20000);
        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        const QString err = QString::fromUtf8(p.readAllStandardError());

        if (p.exitCode() != 0 && out.isEmpty()) {
            QMetaObject::invokeMethod(this, [this, path, err]() {
                emit errorOccurred("Netzlaufwerk nicht erreichbar:\n" + path + "\n" + err.trimmed());
            }, Qt::QueuedConnection);
            return entries;
        }

        const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            QString name = line.trimmed();
            if (name.isEmpty()) continue;
            QString url = path.endsWith('/') ? path + name : path + "/" + name;
            bool isDir = false;
            QProcess probe;
            probe.start("kioclient5", QStringList() << "stat" << url);
            probe.waitForFinished(3000);
            if (probe.exitCode() == 0) {
                const QString statOut = QString::fromUtf8(probe.readAllStandardOutput());
                // FILE_TYPE 0040000 = Verzeichnis, 0100000 = Datei
                isDir = statOut.contains("FILE_TYPE") && statOut.contains("0040000");
            }
            QVariantMap entry{
                {"name", name}, {"path", url}, {"isDir", isDir}, {"isSmb", true}
            };
            entry["coverColor"] = entryColor(name);
            entry["coverInitials"] = entryInitials(name);
            entries.append(entry);
        }
        sortDirEntries(entries);
        return entries;
    }

    // ── smb-Datei lokal kopieren und abspielen ──
    Q_INVOKABLE void openSmbFile(const QString& url) {
        QString tmp = "/tmp/sid_smb_tmp.sid";
        QProcess p;
        p.start("kioclient5", QStringList() << "cp" << url << tmp);
        p.waitForFinished(20000);
        if (p.exitCode() != 0 || !QFileInfo::exists(tmp)) {
            emit errorOccurred("Datei konnte nicht geladen werden:\n" + url);
            return;
        }
        loadFile(tmp);
        play();
    }

    // ── smb-Datei zur Playlist hinzufügen (kopiert nach ~/sid_playlist_cache/) ──
    Q_INVOKABLE void addSmbToPlaylist(const QString& url) {
        QDir cache(QDir::homePath() + "/sid_playlist_cache");
        if (!cache.exists()) QDir().mkpath(cache.absolutePath());
        QString name = url.section('/', -1);
        QString tmp = cache.absolutePath() + "/" + name;
        QProcess p;
        p.start("kioclient5", QStringList() << "cp" << url << tmp);
        p.waitForFinished(20000);
        if (p.exitCode() != 0 || !QFileInfo::exists(tmp)) {
            emit errorOccurred("Datei konnte nicht geladen werden:\n" + url);
            return;
        }
        addToPlaylist(tmp);
    }

    // ── Ganzen Ordner (samt Unterordner) zur Playlist hinzufügen ──
    Q_INVOKABLE void addFolderRecursive(const QString& path) {
        if (path.startsWith("smb://")) {
            // Netzwerkordner per KIO komplett in den Cache kopieren (kopiert rekursiv)
            QDir cache(QDir::homePath() + "/sid_playlist_cache");
            if (!cache.exists()) QDir().mkpath(cache.absolutePath());
            QString name = path.section('/', -1);
            if (name.isEmpty()) name = "nas_ordner";
            QString target = cache.absolutePath() + "/" + name;
            if (!QFileInfo::exists(target)) {
                QProcess p;
                p.start("kioclient5", QStringList() << "cp" << path << target);
                p.waitForFinished(120000);  // große Ordner brauchen Zeit
                if (p.exitCode() != 0 && !QDir(target).exists()) {
                    emit errorOccurred("Ordner konnte nicht geladen werden:\n" + path
                                       + "\n" + QString::fromUtf8(p.readAllStandardError()).trimmed());
                    return;
                }
            }
            addSidsRecursive(target);
        } else {
            addSidsRecursive(path);
        }
    }

    // Rekursiv alle .sid/.mus im Ordnerbaum einsammeln und zur Playlist
    void addSidsRecursive(const QString& dirPath) {
        QDirIterator it(dirPath, QStringList() << "*.sid" << "*.mus" << "*.SID" << "*.MUS",
                        QDir::Files, QDirIterator::Subdirectories);
        int added = 0;
        while (it.hasNext()) {
            const QString file = it.next();
            if (!m_playlist.contains(file)) {
                m_playlist.append(file);
                ++added;
            }
        }
        if (added > 0) savePlaylist();
        emit playlistChanged();
        emit infoMessage(QString("%1 Song(s) aus Ordner hinzugefügt").arg(added));
    }

    // ── Datei-Browser ──
    // Einträge sortieren: Ordner zuerst, dann Dateien — jeweils alphabetisch
    void sortDirEntries(QVariantList& entries) {
        std::sort(entries.begin(), entries.end(), [](const QVariant& a, const QVariant& b) {
            const QVariantMap& ma = a.toMap();
            const QVariantMap& mb = b.toMap();
            if (ma.value("isDir").toBool() != mb.value("isDir").toBool())
                return ma.value("isDir").toBool();  // Ordner vor Dateien
            return ma.value("name").toString().compare(
                       mb.value("name").toString(), Qt::CaseInsensitive) < 0;
        });
    }

    // Cover-Erkennung: Bild im Ordner NUR wenn coverMode aktiv (sonst zu teuer bei großen Sammlungen)
    void enrichDirEntry(QVariantMap& entry) {
        const QString path = entry.value("path").toString();
        if (!entry.value("isDir").toBool() || path.startsWith("smb://")) {
            // Bei smb kein Cover-Check (teuer) — generierte Farbe reicht
            entry["coverColor"] = entryColor(entry.value("name").toString());
            entry["coverInitials"] = entryInitials(entry.value("name").toString());
            return;
        }
        if (m_coverMode) {
            QDir d(path);
            static const QStringList imgFilters = {"cover.jpg", "cover.png", "cover.jpeg", "cover.webp",
                                                   "folder.jpg", "folder.png", "front.jpg", "front.png",
                                                   "Cover.jpg", "Cover.png", "COVER.jpg", "COVER.png"};
            for (const QString& img : imgFilters) {
                if (QFileInfo::exists(d.filePath(img))) {
                    entry["coverImage"] = "file://" + d.filePath(img);
                    break;
                }
            }
        }
        entry["coverColor"] = entryColor(entry.value("name").toString());
        entry["coverInitials"] = entryInitials(entry.value("name").toString());
    }

    // Stabile Farbe aus dem Namen (Hash → HSL)
    QString entryColor(const QString& name) const {
        const QByteArray bytes = name.toUtf8();
        uint h = 2166136261u;
        for (char c : bytes) { h ^= (unsigned char)c; h *= 16777619u; }
        const qreal hue = (h % 360) / 360.0;
        const QColor c = QColor::fromHslF(hue, 0.55, 0.42, 1.0);
        return c.name(QColor::HexRgb);
    }

    // Initialen: erste Buchstaben der ersten 2 Wörter
    QString entryInitials(const QString& name) const {
        const QStringList words = name.split(QRegularExpression("[^A-Za-z0-9äöüÄÖÜ]+"), Qt::SkipEmptyParts);
        QString initials;
        for (int i = 0; i < qMin(2, words.size()); ++i) {
            if (!words[i].isEmpty()) initials += words[i].at(0).toUpper();
        }
        return initials.isEmpty() ? "?" : initials;
    }

    Q_INVOKABLE void setDir(const QString& path) {
        QDir dir(path);
        if (!dir.exists()) return;
        m_currentDir = dir.absolutePath();
        m_dirEntries.clear();
        // Erst Unterordner (sortiert), dann SID-Dateien
        const QStringList dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& d : dirs) {
            QVariantMap entry{{"name", d}, {"isDir", true}, {"path", dir.filePath(d)}};
            enrichDirEntry(entry);
            m_dirEntries.append(entry);
        }
        const QStringList files = dir.entryList(QStringList() << "*.sid" << "*.mus" << "*.SID" << "*.MUS",
                                                QDir::Files, QDir::Name);
        for (const QString& f : files) {
            m_dirEntries.append(QVariantMap{{"name", f}, {"isDir", false}, {"path", dir.filePath(f)}});
        }
        sortDirEntries(m_dirEntries);
        emit dirChanged();
    }

    Q_INVOKABLE void goUp() {
        if (m_currentDir.startsWith("smb://")) {
            // smb-URL verkürzen: smb://host/share/ordner → smb://host/share
            QString up = m_currentDir;
            while (up.endsWith('/')) up.chop(1);
            const int lastSlash = up.lastIndexOf('/');
            if (lastSlash > 7) {  // nicht über smb://host hinaus
                up = up.left(lastSlash);
                openPlace(up);
            }
            return;
        }
        QDir dir(m_currentDir);
        if (dir.cdUp()) setDir(dir.absolutePath());
    }

    Q_INVOKABLE void goHome() { setDir(QDir::homePath()); }

    Q_INVOKABLE void openDirOrFile(const QString& path) {
        QFileInfo fi(path);
        if (fi.isDir()) {
            setDir(path);
        } else if (fi.isFile()) {
            loadFile(path);
            play();
        }
    }

    Q_INVOKABLE void addDirOrFile(const QString& path) {
        QFileInfo fi(path);
        if (fi.isDir()) {
            setDir(path);
        } else if (fi.isFile()) {
            addToPlaylist(path);
        }
    }

    Q_INVOKABLE bool loadFile(const QString& path) {
        stop();
        delete m_tune;
        m_filePathStd = path.toStdString();
        m_tune = new SidTune(m_filePathStd.c_str());
        if (!m_tune->getStatus()) {
            emit errorOccurred("SID konnte nicht geladen werden: " + path);
            return false;
        }
        const SidTuneInfo* info = m_tune->getInfo();
        // WICHTIG: SID-Tag-Strings sind Latin-1 (ISO-8859-1), NICHT UTF-8!
        // fromUtf8 würde ü/ä/ö (0xFC etc.) zu ungültigen Zeichen machen (Diamant-Symbol)
        m_title = QString::fromLatin1(info->infoString(0));
        m_author = QString::fromLatin1(info->infoString(1));
        m_copyright = QString::fromLatin1(info->infoString(2));
        m_subsongs = info->songs();
        m_currentSubsong = info->currentSong() + 1;
        m_sidChips = info->sidChips();
        m_filePath = path;

        m_tune->selectSong(0);  // WICHTIG: Song explizit wählen (sonst Knattern!)

        auto model = info->sidModel(0);
        m_chipModel = (model == SidTuneInfo::SIDMODEL_6581) ? "MOS6581"
                     : (model == SidTuneInfo::SIDMODEL_8580) ? "MOS8580" : "AUTO";
        m_c64Model = (info->clockSpeed() == SidTuneInfo::CLOCK_PAL) ? "PAL" : "NTSC";
        m_tuneLengthSec = 0;  // Songlängen nicht in libsidplayfp 3.x verfügbar
        m_fileName = QFileInfo(path).fileName();
        emit songChanged();
        return true;
    }

    // ── Playlist ──
    Q_INVOKABLE void addToPlaylist(const QString& path) {
        if (!m_playlist.contains(path)) {
            m_playlist.append(path);
            savePlaylist();
            emit playlistChanged();
        }
    }

    Q_INVOKABLE void clearPlaylist() {
        m_playlist.clear();
        m_playlistIndex = -1;
        savePlaylist();
        emit playlistChanged();
    }

    Q_INVOKABLE void removeFromPlaylist(int index) {
        if (index < 0 || index >= m_playlist.size()) return;
        m_playlist.removeAt(index);
        if (m_playlistIndex >= m_playlist.size()) m_playlistIndex = m_playlist.size() - 1;
        savePlaylist();
        emit playlistChanged();
    }

    // ── Playlist-Persistenz (letzte Playlist beim Start wiederherstellen) ──
    QString playlistFile() const {
        return QDir::homePath() + "/.config/sidplayer/playlist.json";
    }

    void savePlaylist() {
        QJsonArray arr;
        for (const QString& p : m_playlist) arr.append(p);
        QJsonObject obj;
        obj["playlist"] = arr;
        obj["index"] = m_playlistIndex;
        QSaveFile f(playlistFile());
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
            f.commit();
        }
    }

    void loadPlaylist() {
        QFile f(playlistFile());
        if (!f.open(QIODevice::ReadOnly)) return;
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonArray arr = obj["playlist"].toArray();
        m_playlist.clear();
        for (const QJsonValue& v : arr) m_playlist.append(v.toString());
        m_playlistIndex = obj["index"].toInt(-1);
        if (m_playlistIndex >= m_playlist.size()) m_playlistIndex = m_playlist.size() - 1;
        if (!m_playlist.isEmpty()) emit playlistChanged();
    }

    Q_INVOKABLE void playFromPlaylist(int index) {
        if (index < 0 || index >= m_playlist.size()) return;
        m_playlistIndex = index;
        emit playlistChanged();
        loadFile(m_playlist[index]);
        play();
    }

    Q_INVOKABLE void nextTrack() {
        if (m_playlist.isEmpty()) return;
        int next = (m_playlistIndex + 1) % m_playlist.size();
        playFromPlaylist(next);
    }

    Q_INVOKABLE void prevTrack() {
        if (m_playlist.isEmpty()) return;
        int prev = (m_playlistIndex - 1 + m_playlist.size()) % m_playlist.size();
        playFromPlaylist(prev);
    }

    Q_INVOKABLE bool play() {
        if (!m_tune) return false;
        if (m_isPlaying) return true;

        const SidTuneInfo* info = m_tune->getInfo();

        m_sidBuilder = new ReSIDfpBuilder("reSIDfp");
        m_player = new sidplayfp();
        SidConfig config;
        config.sidEmulation = m_sidBuilder;
        config.defaultC64Model = SidConfig::PAL;
        config.forceC64Model = true;
        config.defaultSidModel = (m_chipModel == "MOS8580") ? SidConfig::MOS8580 : SidConfig::MOS6581;
        config.forceSidModel = false;
        config.frequency = 48000;
        config.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;

        if (info->sidChips() >= 2) {
            config.secondSidAddress = 0xD420;
        }
        if (info->sidChips() >= 3) {
            config.thirdSidAddress = 0xD440;
        }

        if (!m_player->config(config) || !m_player->load(m_tune)) {
            emit errorOccurred(QString::fromUtf8(m_player->error()));
            delete m_player; m_player = nullptr;
            delete m_sidBuilder; m_sidBuilder = nullptr;
            return false;
        }
        // Echtes Stereo nur bei ≥2 SIDs; sonst Mono zentriert (Duplikation im Source)
        bool effectiveStereo = m_stereoMode && info->sidChips() >= 2;
        m_player->initMixer(effectiveStereo);

        const QAudioDevice& outDev = QMediaDevices::defaultAudioOutput();
        if (outDev.isNull()) {
            emit errorOccurred("Kein Audio-Ausgabegerät");
            return false;
        }
        QAudioFormat format;
        format.setSampleRate(48000);
        format.setChannelCount(2);  // IMMER 2 Kanäle ans Gerät — Mono wird zentriert (beide Kanäle gleiches Signal)
        format.setSampleFormat(QAudioFormat::Int16);
        if (!outDev.isFormatSupported(format)) {
            format = outDev.preferredFormat();
        }
        int outCh = format.channelCount();
        int bps = (format.sampleFormat() == QAudioFormat::Int32) ? 4 : 2;

        m_audioSource = new SIDAudioSource(*m_player, outCh, bps, effectiveStereo);
        m_audioSource->onWaveform = [this](const float* samples, int count) {
            // Audio-Thread: Samples in Wellenform-Ring schreiben (kurzer Mutex)
            std::lock_guard<std::mutex> lock(m_waveMutex);
            for (int i = 0; i < count; ++i) {
                int pos = (m_waveRingStart + m_waveRingCount) % WAVE_RING_SIZE;
                m_waveRing[pos] = samples[i];
                if (m_waveRingCount < WAVE_RING_SIZE) {
                    m_waveRingCount++;
                } else {
                    m_waveRingStart = (m_waveRingStart + 1) % WAVE_RING_SIZE;
                }
            }
        };
        m_audioSink = new QAudioSink(outDev, format);
        // Debug: Was nutzt der Sink wirklich?
        qDebug() << "SID-DEBUG device:" << outDev.id() << "|" << outDev.description();
        qDebug() << "SID-DEBUG format:" << format.sampleRate() << "Hz" << format.channelCount() << "ch"
                 << format.sampleFormat();
        qDebug() << "SID-DEBUG isFormatSupported(Int16 48k stereo):" << outDev.isFormatSupported(format);
        qDebug() << "SID-DEBUG preferredFormat:" << outDev.preferredFormat().sampleRate()
                 << outDev.preferredFormat().channelCount() << outDev.preferredFormat().sampleFormat();
        QObject::connect(m_audioSink, &QAudioSink::stateChanged, [](QAudio::State st) {
            qDebug() << "SID-DEBUG sink state:" << st;
        });
        // Kleiner Puffer = geringer Versatz zwischen Wellenform und hörbarem Sound.
        // 65536 Bytes ≈ 340ms Vorlauf (Wellenform tanzte vor dem Beat!) — jetzt ~85ms.
        m_audioSink->setBufferSize(8192);
        m_audioSource->start();
        m_audioSink->start(m_audioSource);

        m_isPlaying = true;
        emit playingChanged();
        return true;
    }

    Q_INVOKABLE void stop() {
        if (m_audioSink) {
            m_audioSink->stop();
            delete m_audioSink; m_audioSink = nullptr;
        }
        if (m_audioSource) {
            m_audioSource->stop();
            delete m_audioSource; m_audioSource = nullptr;
        }
        if (m_player) {
            delete m_player; m_player = nullptr;
        }
        if (m_sidBuilder) {
            delete m_sidBuilder; m_sidBuilder = nullptr;
        }
        m_isPlaying = false;
        emit playingChanged();
    }

    // ── WAV-Export: rendert alle Subsongs (je 3 Min) offline in eine WAV-Datei ──
    // Asynchron (QtConcurrent) — UI bleibt flüssig, Fortschritt via exportProgress
    Q_INVOKABLE void exportWav(const QString& path) {
        if (m_tune == nullptr || path.isEmpty()) return;
        // QML-FileDialog liefert ein QUrl (file:///C:/... auf Windows) — robust wandeln
        QString target = path;
        if (target.startsWith("file://")) target = QUrl(target).toLocalFile();
        if (target.isEmpty()) return;
        // Windows: QUrl.toLocalFile() kann "/C:/..." liefern — führenden Slash entfernen
        if (target.startsWith("/C:/") || target.startsWith("/c:/")) target = target.mid(1);
        stop();

        const SidTuneInfo* info = m_tune->getInfo();
        const int totalSubs = qMax(1, static_cast<int>(info->songs()));
        const int currentSong = m_currentSubsong;  // merken für Wiederherstellung
        const int sidChips = static_cast<int>(info->sidChips());

        // Effekt-Parameter für den Export (gleiche Stufe wie Live!)
        const float fxR = m_fxReverb, fxE = m_fxEcho, fxS = m_fxSpatial;
        const bool stereo = m_stereoMode && sidChips >= 2;

        emit infoMessage("WAV-Export läuft…");

        QtConcurrent::run([this, target, totalSubs, currentSong, sidChips, fxR, fxE, fxS, stereo]() {
            constexpr int SAMPLE_RATE = 48000;
            constexpr int DURATION_SEC = 180;  // 3 Min pro Subsong
            constexpr int BLOCK = 8192;        // Samples pro Render-Schritt

            // Eigene Player-Instanz für den Export (greift die Live-Wiedergabe nicht an)
            ReSIDfpBuilder* builder = new ReSIDfpBuilder("export");
            sidplayfp* player = new sidplayfp();
            SidConfig config;
            config.sidEmulation = builder;
            config.defaultC64Model = SidConfig::PAL;
            config.forceC64Model = true;
            config.defaultSidModel = (m_chipModel == "MOS8580") ? SidConfig::MOS8580 : SidConfig::MOS6581;
            config.forceSidModel = false;
            config.frequency = SAMPLE_RATE;
            config.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;
            if (sidChips >= 2) config.secondSidAddress = 0xD420;
            if (sidChips >= 3) config.thirdSidAddress = 0xD440;

            QFile out(target);
            const bool ok = out.open(QIODevice::WriteOnly);
            if (!ok || !player->config(config) || !player->load(m_tune)) {
                emit errorOccurred(QString::fromUtf8(player ? player->error() : "Datei konnte nicht geöffnet werden"));
                delete player; delete builder;
                emit exportFinished(false, target);
                return;
            }
            // WICHTIG: initMixer NACH config/load (vorher → SIGILL in libsidplayfp!)
            player->initMixer(stereo);

            // WAV-Header (44 Bytes, wird am Ende mit Größe gefüllt)
            auto writeWavHeader = [&](quint32 dataSize) {
                out.seek(0);
                QByteArray h(44, 0);
                h[0]='R'; h[1]='I'; h[2]='F'; h[3]='F';
                qToLittleEndian<quint32>(36 + dataSize, reinterpret_cast<uchar*>(h.data()+4));
                h[8]='W'; h[9]='A'; h[10]='V'; h[11]='E';
                h[12]='f'; h[13]='m'; h[14]='t'; h[15]=' ';
                qToLittleEndian<quint32>(16, reinterpret_cast<uchar*>(h.data()+16));  // fmt-chunk
                qToLittleEndian<quint16>(1, reinterpret_cast<uchar*>(h.data()+20));  // PCM
                qToLittleEndian<quint16>(2, reinterpret_cast<uchar*>(h.data()+22));  // Kanäle
                qToLittleEndian<quint32>(SAMPLE_RATE, reinterpret_cast<uchar*>(h.data()+24));
                qToLittleEndian<quint32>(SAMPLE_RATE * 2 * 2, reinterpret_cast<uchar*>(h.data()+28));  // ByteRate
                qToLittleEndian<quint16>(2 * 2, reinterpret_cast<uchar*>(h.data()+32));  // BlockAlign
                qToLittleEndian<quint16>(16, reinterpret_cast<uchar*>(h.data()+34));  // Bits
                h[36]='d'; h[37]='a'; h[38]='t'; h[39]='a';
                qToLittleEndian<quint32>(dataSize, reinterpret_cast<uchar*>(h.data()+40));
                out.write(h);
            };
            writeWavHeader(0);
            out.seek(44);  // Daten beginnen nach Header

            // DSP-Puffer für den Export (gleiche Effekte wie Live)
            std::vector<float> fxDelayL(72000, 0.0f), fxDelayR(72000, 0.0f);
            int fxPos = 0;
            std::vector<float> haasL(576, 0.0f), haasR(576, 0.0f);
            int haasPos = 0;

            auto applyFxExport = [&](float& l, float& r) {
                if (fxR < 0.001f && fxE < 0.001f && fxS < 0.001f) return;
                const int echoDelay = 14400, reverbDelay = 9600;
                int eIdx = fxPos - echoDelay; if (eIdx < 0) eIdx += 72000;
                int rIdx = fxPos - reverbDelay; if (rIdx < 0) rIdx += 72000;
                float eL = fxDelayL[eIdx], eR = fxDelayR[eIdx];
                float fbL = fxDelayL[rIdx], fbR = fxDelayR[rIdx];
                float outL = l + fxE * (eL + 0.35f * fbL);
                float outR = r + fxE * (eR + 0.35f * fbR);
                if (fxR > 0.001f) { outL += fxR * (0.55f * fbL); outR += fxR * (0.55f * fbR); }
                fxDelayL[fxPos] = l + 0.5f * fxR * fbL;
                fxDelayR[fxPos] = r + 0.5f * fxR * fbR;
                fxPos = (fxPos + 1) % 72000;
                if (fxS > 0.001f) {
                    float hL = haasL[haasPos], hR = haasR[haasPos];
                    haasL[haasPos] = outL; haasR[haasPos] = outR;
                    haasPos = (haasPos + 1) % 576;
                    outL += fxS * 0.5f * hR;
                    outR += fxS * 0.5f * hL;
                }
                outL = qBound(-1.0f, outL, 1.0f);
                outR = qBound(-1.0f, outR, 1.0f);
                l = outL; r = outR;
            };

            // Eigene Player-Instanz pro Subsong (wie die Live-App: stop()+play()).
            // WICHTIG: selectSong+load auf DEMselben sidplayfp liefert Müll (Null-Samples);
            // nur mit frischem Player pro Subsong ist der Render sauber.
            std::vector<short> buf(BLOCK * 2);
            for (int s = 1; s <= totalSubs; ++s) {
                m_tune->selectSong(s - 1);

                ReSIDfpBuilder* subBuilder = new ReSIDfpBuilder("export");
                sidplayfp* subPlayer = new sidplayfp();
                SidConfig subConfig;
                subConfig.sidEmulation = subBuilder;
                subConfig.defaultC64Model = SidConfig::PAL;
                subConfig.forceC64Model = true;
                subConfig.defaultSidModel = (m_chipModel == "MOS8580") ? SidConfig::MOS8580 : SidConfig::MOS6581;
                subConfig.forceSidModel = false;
                subConfig.frequency = SAMPLE_RATE;
                subConfig.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;
                if (sidChips >= 2) subConfig.secondSidAddress = 0xD420;
                if (sidChips >= 3) subConfig.thirdSidAddress = 0xD440;
                if (!subPlayer->config(subConfig) || !subPlayer->load(m_tune)) {
                    delete subPlayer; delete subBuilder;
                    emit errorOccurred("Subsong konnte nicht geladen werden");
                    emit exportFinished(false, target);
                    out.close();
                    return;
                }
                subPlayer->initMixer(stereo);
                emit exportProgress(s, totalSubs);

                const quint64 totalFrames = SAMPLE_RATE * DURATION_SEC;
                quint64 framesDone = 0;
                while (framesDone < totalFrames) {
                    unsigned int want = BLOCK;
                    unsigned int cycles = static_cast<unsigned int>(want * 985248ULL / SAMPLE_RATE);
                    int produced = subPlayer->play(cycles);
                    if (produced <= 0) break;
                    unsigned int mixed = subPlayer->mix(buf.data(), (unsigned int)produced);
                    if (mixed == 0) break;

                    for (unsigned int i = 0; i < mixed; ++i) {
                        if (stereo && (i + 1) < mixed && (i % 2) == 0) {
                            // Stereo: L/R-Paar
                            float l = buf[i] / 32768.0f, r = buf[i+1] / 32768.0f;
                            applyFxExport(l, r);
                            buf[i] = static_cast<short>(qBound(-32768.0f, l * 32768.0f, 32767.0f));
                            buf[i+1] = static_cast<short>(qBound(-32768.0f, r * 32768.0f, 32767.0f));
                        } else if (!stereo) {
                            // Mono: jedes Sample einzeln (Mixer liefert 1 Sample/Frame!)
                            float s = buf[i] / 32768.0f;
                            float l = s, r = s;
                            applyFxExport(l, r);
                            buf[i] = static_cast<short>(qBound(-32768.0f, ((l + r) * 0.5f) * 32768.0f, 32767.0f));
                        }
                    }
                    // Stereo-Interleaved schreiben (immer 2 Kanäle)
                    if (stereo) {
                        out.write(reinterpret_cast<const char*>(buf.data()), mixed * 2);
                    } else {
                        // Mono → beide Kanäle gleich
                        QByteArray monoBuf;
                        monoBuf.reserve(mixed * 2);
                        for (unsigned int i = 0; i < mixed; ++i) {
                            char ls = static_cast<char>(buf[i] & 0xFF);
                            char hs = static_cast<char>((buf[i] >> 8) & 0xFF);
                            monoBuf.append(ls); monoBuf.append(hs);
                            monoBuf.append(ls); monoBuf.append(hs);
                        }
                        out.write(monoBuf);
                    }
                    framesDone += (stereo ? mixed / 2 : mixed);
                }
                delete subPlayer; delete subBuilder;
            }

            // Header mit echter Größe aktualisieren
            const qint64 finalSize = out.size() - 44;
            writeWavHeader(static_cast<quint32>(finalSize));
            out.close();

            // Zustand wiederherstellen (Subsong, den der Nutzer gehört hat)
            m_tune->selectSong(currentSong - 1);

            emit exportFinished(true, target);
        });
    }

    Q_INVOKABLE void selectSubsong(int song) {
        if (!m_tune || song < 1 || song > m_subsongs) return;
        m_tune->selectSong(song - 1);
        m_currentSubsong = song;
        emit subsongChanged();
        if (m_isPlaying) {
            stop();
            play();
        }
    }

    Q_INVOKABLE void selectChip(const QString& model) {
        if (model == m_chipModel) return;
        m_chipModel = model;
        emit chipModelChanged();
        if (m_isPlaying) {
            stop();
            play();
        }
    }

signals:
    void songChanged();
    void subsongChanged();
    void chipModelChanged();
    void playingChanged();
    void stereoModeChanged();
    void playlistChanged();
    void dirChanged();
    void placesChanged();
    void loadingChanged();
    void coverModeChanged();
    void fxChanged();
    void exportProgress(int current, int total);
    void exportFinished(bool success, const QString& filePath);
    void waveformReady(const QByteArray& samples);
    void errorOccurred(const QString& message);
    void infoMessage(const QString& message);

private:
    SidTune* m_tune = nullptr;
    sidplayfp* m_player = nullptr;
    ReSIDfpBuilder* m_sidBuilder = nullptr;
    QAudioSink* m_audioSink = nullptr;
    SIDAudioSource* m_audioSource = nullptr;
    QTimer* m_waveTimer = nullptr;
    QMediaDevices* m_devices = nullptr;
    QString m_lastDeviceId;
    static constexpr int WAVE_RING_SIZE = 4096;   // ~85ms @48kHz Samples
    float m_waveRing[WAVE_RING_SIZE] = {0.0f};
    int m_waveRingStart = 0;
    int m_waveRingCount = 0;
    std::mutex m_waveMutex;

    QString m_title, m_author, m_copyright, m_chipModel = "AUTO", m_filePath;
    QString m_c64Model = "PAL", m_fileName;
    int m_tuneLengthSec = 0;
    std::string m_filePathStd;
    QString m_currentDir;
    QVariantList m_dirEntries;
    QVariantList m_places;
    bool m_loading = false;
    bool m_coverMode = false;
    float m_fxReverb = 0.0f;
    float m_fxEcho = 0.0f;
    float m_fxSpatial = 0.0f;
    int m_subsongs = 0, m_currentSubsong = 1, m_sidChips = 1;
    bool m_isPlaying = false;
    bool m_stereoMode = false;
    QStringList m_playlist;
    int m_playlistIndex = -1;
};

#endif // SIDPLAYER_BACKEND_H
