#include "player_graphics_item.h"

#include "../../game/player/player_actions.h"
#include "../../interface/widgets/tabs/tab_game.h"
#include "../board/abstract_card_item.h"
#include "../board/counter_general.h"
#include "../board/counter_text.h"
#include "../hand_counter.h"
#include "../zones/command_zone.h"
#include "../zones/hand_zone.h"
#include "../zones/pile_zone.h"
#include "../zones/stack_zone.h"
#include "../zones/table_zone.h"
#include "menu/player_menu.h"
#include "player_dialogs.h"

#include <QGraphicsView>
#include <QMessageBox>
#include <libcockatrice/rules/commander_counter_names.h>

PlayerGraphicsItem::PlayerGraphicsItem(PlayerLogic *_player) : player(_player)
{
    connect(&SettingsCache::instance(), &SettingsCache::horizontalHandChanged, this,
            &PlayerGraphicsItem::rearrangeZones);
    connect(&SettingsCache::instance(), &SettingsCache::handJustificationChanged, this,
            &PlayerGraphicsItem::rearrangeZones);
    connect(player, &PlayerLogic::rearrangeCounters, this, &PlayerGraphicsItem::rearrangeCounters);
    connect(player, &PlayerLogic::activeChanged, this, &PlayerGraphicsItem::onPlayerActiveChanged);
    connect(player, &PlayerLogic::concededChanged, this, [this](int, bool c) { setVisible(!c); });
    connect(player, &PlayerLogic::zoneIdChanged, this, [this](int id) { playerArea->setPlayerZoneId(id); });

    connect(player, &PlayerLogic::counterAdded, this, &PlayerGraphicsItem::onCounterAdded);
    connect(player, &PlayerLogic::counterRemoved, this, &PlayerGraphicsItem::onCounterRemoved);

    connect(player->getPlayerEventHandler(), &PlayerEventHandler::logDrawCards, this,
            [this](PlayerLogic *p, int number, bool deckIsEmpty) {
                // Assisted-mode warning for the empty-library draw-loss SBA (rule 104.3b): number
                // == 0 && deckIsEmpty means the draw was attempted but nothing was left to draw.
                if (number == 0 && deckIsEmpty) {
                    QMessageBox::warning(
                        nullptr, tr("Empty library"),
                        tr("%1 attempted to draw from an empty library and has lost the game (rule 104.3b).")
                            .arg(p->getPlayerInfo()->getName()));
                }
            });

    playerMenu = new PlayerMenu(this);

    connect(playerMenu, &PlayerMenu::shortcutsActivated, this, [this]() {
        for (auto *ctr : counterWidgets) {
            ctr->setShortcutsActive();
        }
    });
    connect(playerMenu, &PlayerMenu::shortcutsDeactivated, this, [this]() {
        for (auto *ctr : counterWidgets) {
            ctr->setShortcutsInactive();
        }
    });
    connect(playerMenu, &PlayerMenu::retranslateRequested, this, [this]() {
        for (auto *ctr : counterWidgets) {
            ctr->retranslateUi();
        }
    });

    playerDialogs = new PlayerDialogs(this, player->getPlayerActions());

    connect(playerDialogs, &PlayerDialogs::requestDialogSemaphore, player, &PlayerLogic::setDialogSemaphore);

    playerArea = new PlayerArea(this);

    playerTarget = new PlayerTarget(player, playerArea);
    qreal avatarMargin =
        (counterAreaWidth + CardDimensions::HEIGHT_F + 15 - playerTarget->boundingRect().width()) / 2.0;
    playerTarget->setPos(QPointF(avatarMargin, avatarMargin));
    connect(player, &PlayerLogic::holdsPriorityChanged, playerTarget, &PlayerTarget::setHoldsPriority);

    initializeZones();

    connect(player, &PlayerLogic::addViewCustomZoneActionToCustomZoneMenu, this,
            &PlayerGraphicsItem::onCustomZoneAdded);

    playerMenu->setMenusForGraphicItems();

    connect(tableZoneGraphicsItem, &TableZone::sizeChanged, this, &PlayerGraphicsItem::updateBoundingRect);

    updateBoundingRect();

    rearrangeZones();
    retranslateUi();
}

void PlayerGraphicsItem::retranslateUi()
{
    playerMenu->retranslateUi();

    QMapIterator<QString, CardZoneLogic *> zoneIterator(player->getZones());
    while (zoneIterator.hasNext()) {
        emit zoneIterator.next().value()->retranslateUi();
    }
}

void PlayerGraphicsItem::onPlayerActiveChanged(bool _active)
{
    tableZoneGraphicsItem->setActive(_active);
}

void PlayerGraphicsItem::initializeZones()
{
    auto base = QPointF(counterAreaWidth + (CardDimensions::HEIGHT_F - CardDimensions::WIDTH_F + 15) / 2.0,
                        10 + playerTarget->boundingRect().height() + 5 -
                            (CardDimensions::HEIGHT_F - CardDimensions::WIDTH_F) / 2.0);

    // The command zone leads the pile stack (ahead of library/graveyard/exile), since it's the
    // Commander-specific zone a player most needs to find at a glance, and — unlike the other
    // piles below, which PileZone rotates 90° to stack compactly — CommandZone renders the
    // commander portrait/untapped (see CommandZone), so it reserves a full card-height's worth
    // of vertical space rather than the narrower rotated-pile step used by the rest.
    //
    // `base.y()` bakes in a `-(HEIGHT_F - WIDTH_F)/2` rotation-compensation term that's only
    // correct for the rotated piles below (this point used to be deckZoneGraphicsItem's position,
    // pre-dating the command zone) -- applying it to the unrotated CommandZone leaves it ~15px
    // too high, overlapping the bottom of the avatar box above it. Undo just that term here.
    qreal commandZoneY = base.y() + (CardDimensions::HEIGHT_F - CardDimensions::WIDTH_F) / 2.0;
    commandZoneGraphicsItem = new CommandZone(player->getCommandZone(), this);
    commandZoneGraphicsItem->setPos(QPointF(base.x(), commandZoneY));
    qreal commandZoneStep = commandZoneY - base.y() + CardDimensions::HEIGHT_F + 5;

    deckZoneGraphicsItem = new PileZone(player->getDeckZone(), this);
    deckZoneGraphicsItem->setPos(base + QPointF(0, commandZoneStep));

    qreal h = deckZoneGraphicsItem->boundingRect().width() + 5;

    sideboardGraphicsItem = new PileZone(player->getSideboardZone(), this);
    player->getSideboardZone()->setGraphicsVisibility(false);

    auto *handCounter = new HandCounter(playerArea);
    handCounter->setPos(base + QPointF(0, commandZoneStep + h + 10));
    qreal h2 = handCounter->boundingRect().height();

    graveyardZoneGraphicsItem = new PileZone(player->getGraveZone(), this);
    graveyardZoneGraphicsItem->setPos(base + QPointF(0, commandZoneStep + h + h2 + 10));

    rfgZoneGraphicsItem = new PileZone(player->getRfgZone(), this);
    rfgZoneGraphicsItem->setPos(base + QPointF(0, commandZoneStep + 2 * h + h2 + 10));

    tableZoneGraphicsItem = new TableZone(player->getTableZone(), mirrored, this);
    connect(tableZoneGraphicsItem, &TableZone::sizeChanged, this, &PlayerGraphicsItem::updateBoundingRect);
    connect(this, &PlayerGraphicsItem::mirroredChanged, tableZoneGraphicsItem, &TableZone::setMirrored);

    stackZoneGraphicsItem =
        new StackZone(player->getStackZone(), static_cast<int>(tableZoneGraphicsItem->boundingRect().height()), this);

    handZoneGraphicsItem =
        new HandZone(player->getHandZone(), static_cast<int>(tableZoneGraphicsItem->boundingRect().height()), this);
    connect(player->getPlayerActions(), &PlayerActions::requestSortHand, handZoneGraphicsItem, &HandZone::sortHand);

    connect(handZoneGraphicsItem->getLogic(), &HandZoneLogic::cardCountChanged, handCounter,
            &HandCounter::updateNumber);
    connect(handCounter, &HandCounter::showContextMenu, handZoneGraphicsItem, &HandZone::showContextMenu);

    zoneGraphicsItems.insert(player->getDeckZone()->getName(), deckZoneGraphicsItem);
    zoneGraphicsItems.insert(player->getGraveZone()->getName(), graveyardZoneGraphicsItem);
    zoneGraphicsItems.insert(player->getRfgZone()->getName(), rfgZoneGraphicsItem);
    zoneGraphicsItems.insert(player->getCommandZone()->getName(), commandZoneGraphicsItem);
    zoneGraphicsItems.insert(player->getSideboardZone()->getName(), sideboardGraphicsItem);
    zoneGraphicsItems.insert(player->getTableZone()->getName(), tableZoneGraphicsItem);
    zoneGraphicsItems.insert(player->getStackZone()->getName(), stackZoneGraphicsItem);
    zoneGraphicsItems.insert(player->getHandZone()->getName(), handZoneGraphicsItem);
}

void PlayerGraphicsItem::onCustomZoneAdded(QString customZoneName)
{
    zoneGraphicsItems.insert(customZoneName, nullptr); // Custom zone view goes here, if we ever implement it.
}

QRectF PlayerGraphicsItem::boundingRect() const
{
    return bRect;
}

qreal PlayerGraphicsItem::getMinimumWidth() const
{
    qreal result = tableZoneGraphicsItem->getMinimumWidth() + CardDimensions::HEIGHT_F + 15 + counterAreaWidth +
                   stackZoneGraphicsItem->boundingRect().width();
    if (!SettingsCache::instance().getHorizontalHand()) {
        result += handZoneGraphicsItem->boundingRect().width();
    }
    return result;
}

void PlayerGraphicsItem::paint(QPainter * /*painter*/,
                               const QStyleOptionGraphicsItem * /*option*/,
                               QWidget * /*widget*/)
{
}

void PlayerGraphicsItem::processSceneSizeChange(int newPlayerWidth)
{
    // Extend table (and hand, if horizontal) to accommodate the new player width.
    qreal tableWidth = newPlayerWidth - CardDimensions::HEIGHT_F - 15 - counterAreaWidth -
                       stackZoneGraphicsItem->boundingRect().width();
    if (!SettingsCache::instance().getHorizontalHand()) {
        tableWidth -= handZoneGraphicsItem->boundingRect().width();
    }

    tableZoneGraphicsItem->setWidth(tableWidth);
    handZoneGraphicsItem->setWidth(tableWidth + stackZoneGraphicsItem->boundingRect().width());
}

void PlayerGraphicsItem::setMirrored(bool _mirrored)
{
    if (mirrored != _mirrored) {
        mirrored = _mirrored;
        emit mirroredChanged(mirrored);
        rearrangeZones();
    }
}

void PlayerGraphicsItem::onCounterAdded(CounterState *state)
{
    AbstractCounter *widget;
    if (state->getName() == "life") {
        widget = playerTarget->addCounter(state);
    } else if (state->getName() == "storm") {
        widget = new TextCounter(state, player, this);
    } else if (CommanderCounterNames::isTaxCounter(state->getName())) {
        // Rendered as a small badge on the command zone itself rather than in the generic
        // counter column -- it conceptually belongs to that zone (rule 903.9). shownInCounterArea
        // = false excludes it from rearrangeCounters()'s vertical stacking, the same mechanism
        // that already excludes "life" (handled by playerTarget above) from that stack.
        widget = new GeneralCounter(state, player, /*useNameForShortcut=*/false, commandZoneGraphicsItem,
                                    /*shownInCounterArea=*/false);
        // Top-right corner: the center is taken by the card-count ellipse (paintNumberEllipse's
        // position=-1 in CommandZone::paint()) and bottom-center by the "CMD" label.
        QRectF zoneRect = commandZoneGraphicsItem->boundingRect();
        QRectF widgetRect = widget->boundingRect();
        widget->setPos(zoneRect.right() - widgetRect.width() - 4, zoneRect.top() + 4);
    } else {
        widget = new GeneralCounter(state, player, true, this);
    }
    counterWidgets.insert(state->getId(), widget);

    if (playerMenu->getCountersMenu() && widget->getMenu()) {
        playerMenu->getCountersMenu()->addMenu(widget->getMenu());
    }

    if (playerMenu->getShortcutsActive()) {
        widget->setShortcutsActive();
    }

    if (state->getName() == "life") {
        // Assisted-mode warning for the life-total loss SBA (rule 104.3a). Only fires on the
        // crossing, same dedup approach as the commander-damage warning below.
        connect(state, &CounterState::valueChanged, this, [this](int oldValue, int newValue) {
            if (oldValue > 0 && newValue <= 0) {
                QMessageBox::warning(nullptr, tr("Life total"),
                                     tr("%1's life total has reached %2 and they have lost the game "
                                        "(rule 104.3a).")
                                         .arg(player->getPlayerInfo()->getName())
                                         .arg(newValue));
            }
        });
    }

    if (state->getName() == CommanderCounterNames::poisonCounterName()) {
        // Assisted-mode warning for the poison-counter loss SBA (rule 104.3c). Only ever created
        // for Commander games server-side (see Server_Player::setupZones()), so no client-side
        // isCommanderGame() gate is needed here -- mirrors the damage-counter check below.
        connect(state, &CounterState::valueChanged, this, [this](int oldValue, int newValue) {
            if (oldValue < CommanderCounterNames::LETHAL_POISON_COUNTERS &&
                newValue >= CommanderCounterNames::LETHAL_POISON_COUNTERS) {
                QMessageBox::warning(nullptr, tr("Poison counters"),
                                     tr("%1 has %2 poison counters and has lost the game (rule 104.3c).")
                                         .arg(player->getPlayerInfo()->getName())
                                         .arg(newValue));
            }
        });
    }

    if (CommanderCounterNames::isDamageCounter(state->getName())) {
        // Assisted-mode warning: commander damage is tracked manually (like life), so flag the
        // lethal threshold rather than silently letting it pass. Only fires on the crossing.
        connect(state, &CounterState::valueChanged, this, [this, state](int oldValue, int newValue) {
            if (oldValue < CommanderCounterNames::LETHAL_COMMANDER_DAMAGE &&
                newValue >= CommanderCounterNames::LETHAL_COMMANDER_DAMAGE) {
                QMessageBox::warning(nullptr, tr("Commander damage"),
                                     tr("%1 has taken %2 damage from %3 and has lost the game (rule 704.5g).")
                                         .arg(player->getPlayerInfo()->getName())
                                         .arg(newValue)
                                         .arg(CommanderCounterNames::commanderNameFromDamageCounter(state->getName())));
            }
        });
    }

    rearrangeCounters();
}

void PlayerGraphicsItem::onCounterRemoved(int counterId)
{
    auto *widget = counterWidgets.take(counterId);
    if (!widget) {
        return;
    }
    if (playerMenu->getCountersMenu() && widget->getMenu()) {
        playerMenu->getCountersMenu()->removeAction(widget->getMenu()->menuAction());
    }
    widget->delCounter();
    rearrangeCounters();
}

void PlayerGraphicsItem::rearrangeCounters()
{
    qreal ySize = boundingRect().y() + 80;
    constexpr qreal padding = 5;
    for (auto *ctr : counterWidgets.values()) {
        if (!ctr->getShownInCounterArea()) {
            continue;
        }
        QRectF br = ctr->boundingRect();
        ctr->setPos((counterAreaWidth - br.width()) / 2, ySize);
        ySize += br.height() + padding;
    }
}

void PlayerGraphicsItem::rearrangeZones()
{
    auto base = QPointF(CardDimensions::HEIGHT_F + counterAreaWidth + 15, 0);
    if (SettingsCache::instance().getHorizontalHand()) {
        if (mirrored) {
            if (player->getHandZone()->contentsKnown()) {
                handVisible = true;
                handZoneGraphicsItem->setPos(base);
                base += QPointF(0, handZoneGraphicsItem->boundingRect().height());
            } else {
                handVisible = false;
            }

            stackZoneGraphicsItem->setPos(base);
            base += QPointF(stackZoneGraphicsItem->boundingRect().width(), 0);

            tableZoneGraphicsItem->setPos(base);
        } else {
            stackZoneGraphicsItem->setPos(base);

            tableZoneGraphicsItem->setPos(base.x() + stackZoneGraphicsItem->boundingRect().width(), 0);
            base += QPointF(0, tableZoneGraphicsItem->boundingRect().height());

            if (player->getHandZone()->contentsKnown()) {
                handVisible = true;
                handZoneGraphicsItem->setPos(base);
            } else {
                handVisible = false;
            }
        }
        handZoneGraphicsItem->setWidth(tableZoneGraphicsItem->getWidth() +
                                       stackZoneGraphicsItem->boundingRect().width());
    } else {
        handVisible = true;

        handZoneGraphicsItem->setPos(base);
        base += QPointF(handZoneGraphicsItem->boundingRect().width(), 0);

        stackZoneGraphicsItem->setPos(base);
        base += QPointF(stackZoneGraphicsItem->boundingRect().width(), 0);

        tableZoneGraphicsItem->setPos(base);
    }
    handZoneGraphicsItem->setVisible(handVisible);
    handZoneGraphicsItem->updateOrientation();
    tableZoneGraphicsItem->reorganizeCards();
    updateBoundingRect();
    rearrangeCounters();
}

void PlayerGraphicsItem::updateBoundingRect()
{
    prepareGeometryChange();
    qreal width = CardDimensions::HEIGHT_F + 15 + counterAreaWidth + stackZoneGraphicsItem->boundingRect().width();
    if (SettingsCache::instance().getHorizontalHand()) {
        qreal handHeight = handVisible ? handZoneGraphicsItem->boundingRect().height() : 0;
        bRect = QRectF(0, 0, width + tableZoneGraphicsItem->boundingRect().width(),
                       tableZoneGraphicsItem->boundingRect().height() + handHeight);
    } else {
        bRect = QRectF(
            0, 0, width + handZoneGraphicsItem->boundingRect().width() + tableZoneGraphicsItem->boundingRect().width(),
            tableZoneGraphicsItem->boundingRect().height());
    }
    playerArea->setSize(CardDimensions::HEIGHT_F + counterAreaWidth + 15, bRect.height());

    emit sizeChanged();
}
