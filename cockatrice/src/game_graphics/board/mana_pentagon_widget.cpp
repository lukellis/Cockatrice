#include "mana_pentagon_widget.h"

#include "abstract_counter.h"

#include <QPainter>
#include <QtMath>
#include <cmath>

namespace
{
// Layout-only radii -- no border/background is drawn (removed per feedback), these just
// reserve enough space for the five vertex counters plus a small margin around them.
constexpr qreal OUTER_RADIUS = 40;
constexpr qreal VERTEX_RADIUS = 24; // distance from center to each color counter's own center
constexpr qreal CENTER_MARGIN = 4;  // extra clearance so vertex counters aren't clipped at the edge

// W-U-B-R-G in standard color-pie order, clockwise starting at the top.
const QMap<QString, qreal> &vertexAngleDegrees()
{
    static const QMap<QString, qreal> angles = {
        {"w", -90}, {"u", -18}, {"b", 54}, {"r", 126}, {"g", 198},
    };
    return angles;
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
        centers.insert(it.key(),
                       center + QPointF(VERTEX_RADIUS * std::cos(radians), VERTEX_RADIUS * std::sin(radians)));
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

void ManaPentagonWidget::paint(QPainter * /*painter*/,
                               const QStyleOptionGraphicsItem * /*option*/,
                               QWidget * /*widget*/)
{
    // Purely a layout container -- no border/background of its own (removed per feedback); the
    // five vertex counters plus the centered colorless one are all there is to see.
}
