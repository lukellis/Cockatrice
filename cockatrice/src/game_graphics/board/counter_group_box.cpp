#include "counter_group_box.h"

#include "abstract_counter.h"

#include <QPainter>

namespace
{
constexpr qreal PADDING = 4;
constexpr qreal SPACING = 4;
} // namespace

CounterGroupBox::CounterGroupBox(QGraphicsItem *parent) : QGraphicsItem(parent)
{
}

void CounterGroupBox::addCounterWidget(AbstractCounter *widget)
{
    prepareGeometryChange();
    widget->setParentItem(this);
    widgets.append(widget);
    relayout();
}

void CounterGroupBox::relayout()
{
    prepareGeometryChange();
    qreal x = PADDING;
    for (auto *w : widgets) {
        w->setPos(x, PADDING);
        x += w->boundingRect().width() + SPACING;
    }
    update();
}

QRectF CounterGroupBox::boundingRect() const
{
    if (widgets.isEmpty()) {
        return QRectF(0, 0, 0, 0);
    }
    qreal width = PADDING;
    qreal height = 0;
    for (auto *w : widgets) {
        width += w->boundingRect().width() + SPACING;
        height = qMax(height, w->boundingRect().height());
    }
    width += PADDING - SPACING;
    height += 2 * PADDING;
    return QRectF(0, 0, width, height);
}

void CounterGroupBox::paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/)
{
    if (widgets.isEmpty()) {
        return;
    }
    painter->save();
    painter->setPen(QPen(QColor(255, 255, 255, 60), 1));
    painter->setBrush(QColor(0, 0, 0, 60));
    painter->drawRoundedRect(boundingRect().adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    painter->restore();
}
