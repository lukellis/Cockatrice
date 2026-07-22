/**
 * @file counter_group_box.h
 * @ingroup GameGraphicsPlayers
 */

#ifndef COUNTER_GROUP_BOX_H
#define COUNTER_GROUP_BOX_H

#include <QGraphicsItem>
#include <QList>

class AbstractCounter;

/**
 * A small bordered frame that groups a handful of counter widgets (e.g. Storm + Poison) in a
 * single horizontal row, visually distinguishing them as a set instead of each stacking
 * individually in PlayerGraphicsItem::rearrangeCounters()'s generic vertical column. Purely a
 * layout/visual container -- each child counter keeps its own click/menu/tooltip behavior
 * unchanged, this only reparents and repositions it.
 */
class CounterGroupBox : public QGraphicsItem
{
public:
    explicit CounterGroupBox(QGraphicsItem *parent = nullptr);

    // Reparents @p widget onto this box and appends it to the row, growing the frame to fit.
    void addCounterWidget(AbstractCounter *widget);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    QList<AbstractCounter *> widgets;
    void relayout();
};

#endif
