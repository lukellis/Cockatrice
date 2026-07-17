#ifndef ABILITYTARGETPICKER_H
#define ABILITYTARGETPICKER_H

#include "../../game/player/player_actions.h"
#include "arrow_item.h"

class PlayerLogic;
class QGraphicsSceneMouseEvent;
class QKeyEvent;

/**
 * @brief Phase 7 Stage 2 (targeting) of the card-ability execution engine -- the first genuinely
 * new UI interaction mode in this fork (see COMMANDER_IMPLEMENTATION_STATUS.md's Phase 7 section).
 *
 * Modeled directly on ArrowDragItem's hover/highlight/resolve mechanics (arrow_item.{h,cpp}) --
 * same scene()->items(pos) + qgraphicsitem_cast<CardItem*>/qgraphicsitem_cast<PlayerTarget*>
 * topmost-candidate loop, same setBeingPointedAt() hover highlight, same drawn arrow visual via
 * the shared ArrowItem base -- but decoupled from Command_CreateArrow: on a successful pick, this
 * emits a resolved AbilityTarget instead of sending any command itself, leaving the caller
 * (PlayerActions) to build and batch the real Command_SetCardAttr + Command_ActivateTargetedEffect
 * pair. Unlike ArrowDragItem (which begins mid-drag, with a mouse button already held from the
 * gesture that spawned it), this is constructed from a context-menu action with no button
 * currently held -- so the first fresh click's press+release cycle is what resolves or cancels
 * the pick, rather than a drag's release alone.
 *
 * Only CardItems on the TABLE zone (i.e. permanents) and PlayerTargets are valid targets --
 * matches TargetKind::AnyTarget's "a permanent or a player" scope. Right-click, Escape, or
 * releasing over nothing/an invalid candidate cancels (emits targetCancelled(), sends nothing) --
 * same atomic, no-partial-state precedent as the mana-ability-choice dialog's Cancel button.
 */
class AbilityTargetPicker : public ArrowItem
{
    Q_OBJECT
public:
    AbilityTargetPicker(PlayerLogic *owner, ArrowTarget *startItem);

signals:
    void targetChosen(AbilityTarget target);
    void targetCancelled();

protected:
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QMetaObject::Connection positionConnection;

    void cancel();
};

#endif
