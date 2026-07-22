/**
 * @file player_target.h
 * @ingroup GameGraphicsPlayers
 */
//! \todo Document this file.

#ifndef PLAYERTARGET_H
#define PLAYERTARGET_H

#include "../board/abstract_counter.h"
#include "../board/arrow_target.h"
#include "../board/graphics_item_type.h"

#include <QPixmap>

class PlayerLogic;

class PlayerCounter : public AbstractCounter
{
    Q_OBJECT
public:
    PlayerCounter(CounterState *state, PlayerLogic *player, QGraphicsItem *parent);
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
};

/**
 * A small numeric badge for one opponent's commander-damage-against-this-player counter,
 * shown in a compact row above the life total (see PlayerTarget::addDamageCounter()) -- like
 * PlayerCounter but smaller, with a plain (not corner-cut) rounded rect, since several sit
 * side by side.
 */
class DamageBadge : public AbstractCounter
{
    Q_OBJECT
public:
    DamageBadge(CounterState *state, PlayerLogic *player, QGraphicsItem *parent);
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
};

class PlayerTarget : public ArrowTarget
{
    Q_OBJECT
private:
    QPixmap fullPixmap;
    PlayerCounter *playerCounter;
    QList<DamageBadge *> damageBadges;
    bool holdsPriority = false;

    void relayoutDamageBadges();
public slots:
    void counterDeleted();

public:
    enum
    {
        Type = typePlayerTarget
    };
    int type() const override
    {
        return Type;
    }

    explicit PlayerTarget(PlayerLogic *_player = nullptr, QGraphicsItem *parentItem = nullptr);
    ~PlayerTarget() override;
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    AbstractCounter *addCounter(CounterState *state);

    // One small badge per opponent commander's damage-against-this-player counter (see
    // CommanderCounterNames::damage()) -- variable count (as few as 1 in a 2-player game),
    // laid out right-to-left along the top edge (above where the life badge sits, bottom-right),
    // re-flowed as each one arrives. Deliberately kept inside the existing fixed 160x64 box
    // rather than growing it, so nothing else in PlayerGraphicsItem's layout (which reads
    // playerTarget->boundingRect() to position the command zone/piles) needs to react to it.
    AbstractCounter *addDamageCounter(CounterState *state);
public slots:
    /**
     * @brief Commander-only priority-holder indicator, visible to every player/spectator (unlike
     * the Pass Priority toolbar button's highlight, which is local-only). Deliberately a
     * different visual channel (badge border color) than the active-turn highlight on TableZone,
     * since priority and the active turn are tracked separately (see PlayerLogic::holdsPriorityChanged).
     */
    void setHoldsPriority(bool _holdsPriority);
};

#endif
