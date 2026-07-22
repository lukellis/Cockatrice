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

CounterGroupBox::~CounterGroupBox()
{
    // A widget can be destroyed (AbstractCounter::delCounter() -> deleteLater()) independently
    // of this box, e.g. PlayerGraphicsItem::onCounterRemoved() during a counter resync
    // (PlayerLogic::processPlayerInfo()'s clearCounters()+rebuild) -- addCounterWidget() below
    // reacts to that via the widget's destroyed() signal. But letting ~QGraphicsItem() auto-
    // delete our own remaining `widgets` later (after this destructor returns and our vtable
    // has unwound to QGraphicsItem's abstract base) would run that same handler's relayout()
    // -> boundingRect() call on a half-destroyed `this` and abort with "pure virtual method
    // called". Deleting them explicitly here, while `this` is still fully CounterGroupBox, is
    // safe -- same reasoning as PlayerTarget::~PlayerTarget(). Snapshot into a temporary first:
    // the destroyed() handler mutates `widgets` itself (removeAll()).
    const QList<AbstractCounter *> widgetsToDelete = widgets;
    widgets.clear();
    qDeleteAll(widgetsToDelete);
}

void CounterGroupBox::addCounterWidget(AbstractCounter *widget)
{
    prepareGeometryChange();
    widget->setParentItem(this);
    widgets.append(widget);
    // See ~CounterGroupBox() for why a widget can be destroyed out from under us -- without
    // this, boundingRect()/paint() below can dereference a dangling pointer left behind in
    // `widgets` (this is what actually crashed: a stale storm/poison GeneralCounter from a
    // counter resync, only ever exercised with two real players' worth of setup events in
    // flight at once).
    connect(widget, &QObject::destroyed, this, [this, widget]() {
        widgets.removeAll(widget);
        relayout();
    });
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
