/**
 * @file mana_pentagon_widget.h
 * @ingroup GameGraphicsPlayers
 */

#ifndef MANA_PENTAGON_WIDGET_H
#define MANA_PENTAGON_WIDGET_H

#include <QGraphicsItem>
#include <QMap>
#include <QString>

class AbstractCounter;

/**
 * Compact layout for the six mana-pool counters (w/u/b/r/g/x): a bordered circle with the five
 * color counters arranged at pentagon vertices (standard color-pie order W-U-B-R-G, clockwise
 * from the top) and the colorless counter smaller in the center. Purely a layout + background-
 * watermark decoration around the existing counters -- each child keeps its own
 * CounterState-backed identity/click-to-adjust/tooltip behavior (GeneralCounter instances,
 * reparented here instead of PlayerGraphicsItem::rearrangeCounters()'s generic vertical stack).
 */
class ManaPentagonWidget : public QGraphicsItem
{
public:
    explicit ManaPentagonWidget(QGraphicsItem *parent = nullptr);

    // Reparents @p widget onto this pentagon at the vertex/center position for @p colorName
    // ("w"/"u"/"b"/"r"/"g"/"x" -- see Rules::RulesEngine::manaCounterNames()).
    void addManaCounter(const QString &colorName, AbstractCounter *widget);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    QMap<QString, QPointF> slotCenters() const;
};

#endif
