#include "ability_target_picker.h"

#include "../../game/player/player_logic.h"
#include "../player/player_target.h"
#include "../zones/card_zone.h"
#include "card_item.h"

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <libcockatrice/utility/zone_names.h>

namespace
{
// Distinct from the ordinary arrow-draw feature's team-colored arrows, so a player can tell "I'm
// targeting an ability" apart from "I'm drawing an informational arrow" at a glance.
const QColor TARGETING_ARROW_COLOR = QColor(220, 30, 30);
} // namespace

AbilityTargetPicker::AbilityTargetPicker(PlayerLogic *owner, ArrowTarget *startItem)
    : ArrowItem(QSharedPointer<ArrowData>::create(ArrowData{.creatorId = owner->getPlayerInfo()->getId(),
                                                            .isLocalCreator = true,
                                                            .id = -1,
                                                            .color = TARGETING_ARROW_COLOR}),
               startItem,
               nullptr)
{
    setFlag(QGraphicsItem::ItemIsFocusable);
    setFocus();
}

void AbilityTargetPicker::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (!startItem) {
        return;
    }

    const QPointF endPos = event->scenePos();

    ArrowTarget *cursorItem = nullptr;
    qreal cursorItemZ = -1;
    for (auto *item : scene()->items(endPos)) {
        ArrowTarget *candidate = nullptr;
        if (auto *card = qgraphicsitem_cast<CardItem *>(item)) {
            // Only permanents (table-zone cards) are valid "any target" candidates -- a card in
            // hand/library/graveyard/exile/command isn't a legal target for a damage ability.
            if (card->getZone() && card->getZone()->getName() == ZoneNames::TABLE) {
                candidate = card;
            }
        } else if (auto *pt = qgraphicsitem_cast<PlayerTarget *>(item)) {
            candidate = pt;
        }

        if (candidate && candidate->zValue() > cursorItemZ) {
            cursorItem = candidate;
            cursorItemZ = candidate->zValue();
        }
    }

    if (cursorItem != targetItem) {
        if (targetItem) {
            disconnect(positionConnection);
            targetItem->setBeingPointedAt(false);
        }

        targetItem = cursorItem;
        fullColor = (cursorItem != nullptr);

        if (cursorItem && cursorItem != startItem) {
            cursorItem->setBeingPointedAt(true);
            positionConnection =
                connect(cursorItem, &ArrowTarget::scenePositionChanged, this, [this]() { updatePath(); });
        }
    }

    targetItem ? updatePath() : updatePath(endPos);
    update();
}

void AbilityTargetPicker::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        event->accept();
        cancel();
        return;
    }
    event->accept();
}

void AbilityTargetPicker::mouseReleaseEvent(QGraphicsSceneMouseEvent * /*event*/)
{
    if (!targetItem || targetItem == startItem) {
        cancel();
        return;
    }

    AbilityTarget target;
    if (auto *targetCard = qgraphicsitem_cast<CardItem *>(targetItem)) {
        CardZoneLogic *targetZone = targetCard->getZone();
        target.isPlayer = false;
        target.targetPlayerId = targetZone->getPlayer()->getPlayerInfo()->getId();
        target.targetZone = targetZone->getName();
        target.targetCardId = targetCard->getId();
    } else if (auto *targetPlayer = qgraphicsitem_cast<PlayerTarget *>(targetItem)) {
        target.isPlayer = true;
        target.targetPlayerId = targetPlayer->getOwner()->getPlayerInfo()->getId();
    } else {
        cancel();
        return;
    }

    emit targetChosen(target);
    delArrow();
}

void AbilityTargetPicker::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        cancel();
        return;
    }
    event->ignore();
}

void AbilityTargetPicker::cancel()
{
    emit targetCancelled();
    delArrow();
}
