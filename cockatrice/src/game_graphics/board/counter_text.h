/**
 * @file counter_text.h
 * @ingroup GameGraphicsPlayers
 */

#ifndef COUNTER_TEXT_H
#define COUNTER_TEXT_H

#include "abstract_counter.h"

/**
 * A read-only, plain-text counter display ("<name>: <value>"), for counters that are
 * server-auto-managed rather than something a player manually adjusts (e.g. the Storm counter --
 * see Server_Player::onCardBeingMoved()/resetStormCount()). Unlike GeneralCounter's colored
 * circle-plus-icon rendering, this has no TearOffMenu and doesn't respond to clicks (passes
 * interactive=false to AbstractCounter).
 */
class TextCounter : public AbstractCounter
{
    Q_OBJECT

public:
    TextCounter(CounterState *state, PlayerLogic *player, QGraphicsItem *parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
};

#endif
