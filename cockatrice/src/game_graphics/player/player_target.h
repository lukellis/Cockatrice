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

class PlayerTarget : public ArrowTarget
{
    Q_OBJECT
private:
    QPixmap fullPixmap;
    PlayerCounter *playerCounter;
    bool holdsPriority = false;
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
