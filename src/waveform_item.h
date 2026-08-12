// waveform_item.h — Visualisierung (Scene-Graph / GPU)
// QSGGeometryNode mit Vertex-Farben: Rainbow direkt auf der GPU,
// kein QPainter-Software-Umweg → ~0% CPU. Drei Modi: Wave, Bars, Mix.
#ifndef WAVEFORM_ITEM_H
#define WAVEFORM_ITEM_H

#include <QQuickItem>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QSGFlatColorMaterial>
#include <QColor>
#include <QByteArray>
#include <QMutex>
#include <cmath>
#include <cstring>

class WaveformItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QColor colorTop READ colorTop WRITE setColorTop NOTIFY colorChanged)
    Q_PROPERTY(QColor colorBottom READ colorBottom WRITE setColorBottom NOTIFY colorChanged)
    Q_PROPERTY(int vizMode READ vizMode WRITE setVizMode NOTIFY vizModeChanged)
    Q_PROPERTY(qreal hueShift READ hueShift WRITE setHueShift NOTIFY hueShiftChanged)

signals:
    void colorChanged();
    void vizModeChanged();
    void hueShiftChanged();

public:
    explicit WaveformItem(QQuickItem* parent = nullptr) : QQuickItem(parent) {
        setFlag(ItemHasContents, true);
    }

    QColor colorTop() const { return m_colorTop; }
    void setColorTop(const QColor& c) { m_colorTop = c; emit colorChanged(); update(); }
    QColor colorBottom() const { return m_colorBottom; }
    void setColorBottom(const QColor& c) { m_colorBottom = c; emit colorChanged(); update(); }
    int vizMode() const { return m_vizMode; }
    void setVizMode(int m) { if (m != m_vizMode) { m_vizMode = m; emit vizModeChanged(); update(); } }
    qreal hueShift() const { return m_hueShift; }
    void setHueShift(qreal h) { if (h != m_hueShift) { m_hueShift = h; emit hueShiftChanged(); update(); } }

    // Rohe Float-Samples — vom GUI-Thread aufgerufen, kurz gesperrt kopiert
    Q_INVOKABLE void setSamples(const QByteArray& raw) {
        {
            QMutexLocker lock(&m_mutex);
            m_raw = raw;
        }
        update();
    }

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override {
        // Samples unter Lock kopieren (kurz)
        QVector<float> samples;
        {
            QMutexLocker lock(&m_mutex);
            const int n = m_raw.size() / (int)sizeof(float);
            if (n < 2) {
                delete oldNode;
                return nullptr;
            }
            samples.resize(n);
            memcpy(samples.data(), m_raw.constData(), m_raw.size());
        }

        // Alten Baum komplett verwerfen — frischer Aufbau pro Frame (einfach & sicher)
        delete oldNode;
        oldNode = nullptr;

        QSGNode* root = new QSGNode;
        const qreal w = width();
        const qreal h = height();
        const qreal mid = h / 2.0;

        if (m_vizMode == 1) {
            appendBars(root, w, h, mid, samples);
        } else if (m_vizMode == 2) {
            appendBars(root, w, h, mid, samples);
            appendWave(root, w, h, mid, samples, h * 0.55, 0.5f);
        } else {
            appendWave(root, w, h, mid, samples, h * 0.85, 1.0f);
        }
        return root;
    }

private:
    // Regenbogen-Farbe (Hue rotiert um m_hueShift)
    void rainbowAt(qreal t, unsigned char* rgba, int alpha = 255) const {
        const qreal hue = std::fmod(t + m_hueShift, 1.0);
        QColor c = QColor::fromHslF(hue, 0.85, 0.58, 1.0);
        rgba[0] = (unsigned char)c.red();
        rgba[1] = (unsigned char)c.green();
        rgba[2] = (unsigned char)c.blue();
        rgba[3] = (unsigned char)alpha;
    }

    // ── Wellenform: gefüllte Fläche als Triangle-Strip (oben+unten gespiegelt) ──
    void appendWave(QSGNode* root, qreal w, qreal h, qreal mid,
                    const QVector<float>& samples, qreal ampScale, float alpha) {
        const int n = samples.size();
        const qreal bw = w / (n - 1);
        const int verts = n * 2;

        // Kern (Rainbow via Vertex-Farben)
        QSGGeometryNode* core = new QSGGeometryNode;
        QSGGeometry* geo = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), verts);
        geo->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        QSGGeometry::ColoredPoint2D* pts = geo->vertexDataAsColoredPoint2D();
        for (int i = 0; i < n; ++i) {
            const qreal t = (qreal)i / (n - 1);
            unsigned char rgba[4];
            rainbowAt(t, rgba, (int)(255 * alpha));
            const qreal amp = qBound<qreal>(0.0, qAbs(samples[i]) * ampScale, h * 0.9);
            pts[i * 2].set(i * bw, mid - amp, rgba[0], rgba[1], rgba[2], rgba[3]);
            pts[i * 2 + 1].set(i * bw, mid + amp, rgba[0], rgba[1], rgba[2], (int)(rgba[3] * 0.85f));
        }
        core->setGeometry(geo);
        core->setFlag(QSGNode::OwnsGeometry, true);
        core->setMaterial(new QSGVertexColorMaterial);
        root->appendChildNode(core);

        // Glow (breiter, halbtransparent weiß)
        QSGGeometryNode* glow = new QSGGeometryNode;
        QSGGeometry* gGeo = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), verts);
        gGeo->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        QSGGeometry::Point2D* gPts = gGeo->vertexDataAsPoint2D();
        for (int i = 0; i < n; ++i) {
            const qreal amp = qBound<qreal>(0.0, qAbs(samples[i]) * ampScale * 1.18, h * 0.95);
            gPts[i * 2].set(i * bw, mid - amp);
            gPts[i * 2 + 1].set(i * bw, mid + amp);
        }
        glow->setGeometry(gGeo);
        glow->setFlag(QSGNode::OwnsGeometry, true);
        QSGFlatColorMaterial* gm = new QSGFlatColorMaterial;
        gm->setColor(QColor(255, 255, 255, (int)(30 * alpha)));
        glow->setMaterial(gm);
        root->appendChildNode(glow);
    }

    // ── Balken: Quads (2 Dreiecke pro Balken) mit Rainbow ──
    void appendBars(QSGNode* root, qreal w, qreal h, qreal mid,
                    const QVector<float>& samples) {
        const int BARS = 72;
        const qreal gap = 3.0;
        const qreal bw = (w - gap * (BARS - 1)) / BARS;
        const int n = samples.size();
        const int perBin = qMax(1, n / BARS);
        const int verts = BARS * 6;

        if (m_falloff.size() != BARS) m_falloff.fill(0.0, BARS);

        QSGGeometryNode* node = new QSGGeometryNode;
        QSGGeometry* geo = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), verts);
        geo->setDrawingMode(QSGGeometry::DrawTriangles);
        QSGGeometry::ColoredPoint2D* pts = geo->vertexDataAsColoredPoint2D();

        int vi = 0;
        for (int b = 0; b < BARS; ++b) {
            const int start = b * perBin;
            const int end = qMin(start + perBin, n);
            qreal peak = 0.0;
            for (int i = start; i < end; ++i) peak = qMax(peak, qAbs(samples[i]));
            peak = qMax(peak, m_falloff[b] * 0.82f);
            m_falloff[b] = (float)peak;

            const qreal amp = qBound<qreal>(0.0, peak * h * 0.9, h * 0.9);
            const qreal x = b * (bw + gap);
            const qreal y0 = mid - amp;
            const qreal y1 = mid + amp;
            unsigned char rgba[4];
            rainbowAt((qreal)b / BARS, rgba, 230);

            pts[vi].set(x, y0, rgba[0], rgba[1], rgba[2], rgba[3]); vi++;
            pts[vi].set(x + bw, y0, rgba[0], rgba[1], rgba[2], rgba[3]); vi++;
            pts[vi].set(x, y1, rgba[0], rgba[1], rgba[2], rgba[3]); vi++;
            pts[vi].set(x, y1, rgba[0], rgba[1], rgba[2], rgba[3]); vi++;
            pts[vi].set(x + bw, y0, rgba[0], rgba[1], rgba[2], rgba[3]); vi++;
            pts[vi].set(x + bw, y1, rgba[0], rgba[1], rgba[2], rgba[3]); vi++;
        }

        node->setGeometry(geo);
        node->setFlag(QSGNode::OwnsGeometry, true);
        node->setMaterial(new QSGVertexColorMaterial);
        root->appendChildNode(node);
    }

    QByteArray m_raw;
    mutable QMutex m_mutex;
    QColor m_colorTop = QColor("#4fc3f7");
    QColor m_colorBottom = QColor("#b388ff");
    int m_vizMode = 0;
    qreal m_hueShift = 0.0;
    QVector<qreal> m_falloff;
};

#endif // WAVEFORM_ITEM_H
