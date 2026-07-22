#include "mana_pentagon_widget.h"

#include "abstract_counter.h"

#include <QPainter>
#include <QSvgRenderer>
#include <QtMath>
#include <cmath>

namespace
{
constexpr qreal OUTER_RADIUS = 40;
constexpr qreal VERTEX_RADIUS = 24; // distance from center to each color counter's own center
constexpr qreal CENTER_MARGIN = 4;  // half-diagonal clearance so the outer circle doesn't clip

// W-U-B-R-G in standard color-pie order, clockwise starting at the top.
const QMap<QString, qreal> &vertexAngleDegrees()
{
    static const QMap<QString, qreal> angles = {
        {"w", -90}, {"u", -18}, {"b", 54}, {"r", 126}, {"g", 198},
    };
    return angles;
}

// Faint background watermark of the color's own mana pip (the real WUBRG symbol, distinct from
// counters/*.svg's plain colored-glass icon) -- loaded directly via QSvgRenderer since it's
// requested by a literal "theme:icons/mana/<Letter>.svg" path, not through
// CounterPixmapGenerator's counters/-scoped lookup.
QPixmap loadManaPipWatermark(const QString &upperLetter, int size)
{
    QSvgRenderer renderer(QStringLiteral("theme:icons/mana/%1.svg").arg(upperLetter));
    if (!renderer.isValid()) {
        return {};
    }
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0, 0, size, size));
    return pixmap;
}
} // namespace

ManaPentagonWidget::ManaPentagonWidget(QGraphicsItem *parent) : QGraphicsItem(parent)
{
}

QMap<QString, QPointF> ManaPentagonWidget::slotCenters() const
{
    QMap<QString, QPointF> centers;
    const QRectF br = boundingRect();
    const QPointF center = br.center();
    for (auto it = vertexAngleDegrees().constBegin(); it != vertexAngleDegrees().constEnd(); ++it) {
        const qreal radians = qDegreesToRadians(it.value());
        centers.insert(it.key(), center + QPointF(VERTEX_RADIUS * std::cos(radians), VERTEX_RADIUS * std::sin(radians)));
    }
    centers.insert("x", center);
    return centers;
}

void ManaPentagonWidget::addManaCounter(const QString &colorName, AbstractCounter *widget)
{
    prepareGeometryChange();
    widget->setParentItem(this);
    const QPointF center = slotCenters().value(colorName, boundingRect().center());
    const QRectF wr = widget->boundingRect();
    widget->setPos(center.x() - wr.width() / 2.0, center.y() - wr.height() / 2.0);
    update();
}

QRectF ManaPentagonWidget::boundingRect() const
{
    const qreal side = 2 * (OUTER_RADIUS + CENTER_MARGIN);
    return QRectF(0, 0, side, side);
}

void ManaPentagonWidget::paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/)
{
    const QRectF br = boundingRect();
    const QPointF center = br.center();

    painter->save();
    painter->setPen(QPen(QColor(255, 255, 255, 60), 1));
    painter->setBrush(QColor(0, 0, 0, 60));
    painter->drawEllipse(center, OUTER_RADIUS, OUTER_RADIUS);
    painter->restore();

    static const QMap<QString, QString> letters = {{"w", "W"}, {"u", "U"}, {"b", "B"}, {"r", "R"}, {"g", "G"}};
    const QMap<QString, QPointF> centers = slotCenters();
    painter->save();
    painter->setOpacity(0.35);
    const int watermarkSize = static_cast<int>(VERTEX_RADIUS * 1.3);
    for (auto it = letters.constBegin(); it != letters.constEnd(); ++it) {
        const QPixmap pip = loadManaPipWatermark(it.value(), watermarkSize);
        if (pip.isNull()) {
            continue;
        }
        const QPointF pos = centers.value(it.key());
        painter->drawPixmap(QPointF(pos.x() - watermarkSize / 2.0, pos.y() - watermarkSize / 2.0), pip);
    }
    painter->restore();
}
