#include "card_item.h"

#include "../../client/settings/cache_settings.h"
#include "../../game/phase.h"
#include "../../game/player/player_actions.h"
#include "../../game/player/player_logic.h"
#include "../../game/zones/view_zone_logic.h"
#include "../../interface/widgets/tabs/tab_game.h"
#include "../game_scene.h"
#include "../zones/table_zone.h"
#include "../zones/view_zone.h"
#include "arrow_item.h"
#include "card_drag_item.h"

#include <../../client/settings/card_counter_settings.h>
#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <libcockatrice/card/ability/card_effects.h>
#include <libcockatrice/card/card_info.h>
#include <libcockatrice/protocol/pb/serverinfo_card.pb.h>
#include <optional>

CardItem::CardItem(PlayerLogic *_owner,
                   QGraphicsItem *parent,
                   const CardRef &cardRef,
                   int _cardid,
                   CardZoneLogic *_zone)
    : AbstractCardItem(parent, cardRef, _owner, _cardid), state(new CardState(this, _zone)), dragItem(nullptr)
{
    owner->addCard(this);

    connect(&SettingsCache::instance().cardCounters(), &CardCounterSettings::colorChanged, this, [this](int counterId) {
        if (state->getCounters().contains(counterId)) {
            update();
        }
    });
}

void CardItem::prepareDelete()
{
    if (owner != nullptr) {
        if (owner->getGame()->getActiveCard() == this) {
            emit owner->requestCardMenuUpdate(nullptr);
            owner->getGame()->setActiveCard(nullptr);
        }
        owner = nullptr;
    }

    while (!attachedCards.isEmpty()) {
        attachedCards.first()->setZone(nullptr); // so that it won't try to call reorganizeCards()
        attachedCards.first()->setAttachedTo(nullptr);
    }

    if (state->getAttachedTo() != nullptr) {
        state->getAttachedTo()->removeAttachedCard(this);
        state->setAttachedTo(nullptr);
    }
}

void CardItem::deleteLater()
{
    prepareDelete();
    if (scene()) {
        static_cast<GameScene *>(scene())->unregisterAnimationItem(this);
    }
    AbstractCardItem::deleteLater();
}

void CardItem::setZone(CardZoneLogic *_zone)
{
    state->setZone(_zone);
}

void CardItem::retranslateUi()
{
}

/**
 * @brief Parses a plain "power/toughness" string (as sent by AttrPT/AttrEffectivePT -- always a
 * literal integer pair for any card this fork's static-effect display applies to) into (power,
 * toughness), or std::nullopt if either half isn't a plain integer. Deliberately separate from
 * CardItem::parsePT(), which parses "+X"/"-X" deltas for the manual P/T-adjustment shortcuts --
 * this is only ever used to compare two already-resolved P/T strings for the augmented-stats paint
 * coloring below.
 */
static std::optional<QPair<int, int>> parseAbsolutePT(const QString &pt)
{
    const int sep = pt.indexOf('/');
    if (sep <= 0 || sep == pt.size() - 1) {
        return std::nullopt;
    }
    bool powerOk = false;
    bool toughnessOk = false;
    const int power = pt.left(sep).toInt(&powerOk);
    const int toughness = pt.mid(sep + 1).toInt(&toughnessOk);
    if (!powerOk || !toughnessOk) {
        return std::nullopt;
    }
    return QPair<int, int>(power, toughness);
}

void CardItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    auto &cardCounterSettings = SettingsCache::instance().cardCounters();

    painter->save();
    AbstractCardItem::paint(painter, option, widget);

    // The dedicated +1/+1 counter gets its own translucent-square "+N/+N" badge (below) instead of
    // one of the generic lettered counters' small colored circles -- it's excluded from this loop
    // (and from its position/count stacking) entirely so it doesn't fight the generic counters for
    // a grid slot.
    const int plusOneOneValue = state->getCounters().value(PLUS_ONE_ONE_COUNTER_ID, 0);
    const int genericCounterCount = state->getCounters().size() - (plusOneOneValue > 0 ? 1 : 0);
    int i = 0;
    QMapIterator<int, int> counterIterator(state->getCounters());
    while (counterIterator.hasNext()) {
        counterIterator.next();
        if (counterIterator.key() == PLUS_ONE_ONE_COUNTER_ID) {
            continue;
        }
        QColor _color = cardCounterSettings.color(counterIterator.key());
        paintNumberEllipse(counterIterator.value(), 14, _color, i, genericCounterCount, painter);
        ++i;
    }

    QSizeF translatedSize = getTranslatedSize(painter);
    qreal scaleFactor = translatedSize.width() / boundingRect().width();

    if (plusOneOneValue > 0) {
        painter->save();
        // Drawn in transformPainter()'s reset, device-pixel coordinate space (like the P/T text
        // below), not the raw untransformed space the generic counter circles above use -- that
        // space's font size shrinks along with the board's own zoom, which made this badge's text
        // unreadably tiny at a normal zoomed-out table view. This keeps it legible at a fixed
        // apparent size regardless of zoom, the same convention the P/T/annotation text relies on.
        transformPainter(painter, translatedSize, tapAngle);

        const QString badgeText = QStringLiteral("+%1/+%1").arg(plusOneOneValue);
        QFont font = painter->font();
        font.setBold(true);
        QFontMetrics fm(font);

        // Cap the badge to a small corner of the card regardless of the ambient device-constant
        // font size transformPainter() just set -- that size is tuned for the short "N/N" P/T
        // string, and the wider "+N/+N" text (worse still at double/triple-digit counts) could
        // otherwise render larger than intended. Shrink the font to fit a fixed fraction of the
        // card instead of using the ambient size unconditionally.
        const double maxBadgeWidth = translatedSize.width() * 0.3;
        const double maxBadgeHeight = translatedSize.height() * 0.14;
        const double neededWidth = fm.horizontalAdvance(badgeText) * 1.3;
        const double neededHeight = fm.height() * 1.4;
        const double shrink = qMin(1.0, qMin(maxBadgeWidth / neededWidth, maxBadgeHeight / neededHeight));
        if (shrink < 1.0) {
            font.setPixelSize(qMax(8, static_cast<int>(font.pixelSize() * shrink)));
            fm = QFontMetrics(font);
        }

        const double w = fm.horizontalAdvance(badgeText) * 1.3;
        const double h = fm.height() * 1.4;
        // Bottom-left corner -- top-left sits under a real card's own printed name/mana cost, and
        // bottom-right is already the P/T text's spot.
        const QRectF badgeRect(4 * scaleFactor, translatedSize.height() - h - 4 * scaleFactor, w, h);

        // Gray rounded box, no separately-colored border (just a slightly darker/same-tone outline
        // so the shape reads without competing with the card's own colors) -- deliberately muted
        // compared to the earlier bright-green square, which was harder to read text over.
        painter->setPen(QPen(QColor(40, 40, 40, 200), 1.0));
        painter->setBrush(QBrush(QColor(90, 90, 90, 190)));
        const double cornerRadius = qMin(w, h) * 0.25;
        painter->drawRoundedRect(badgeRect, cornerRadius, cornerRadius);

        // White text with a thin black outline (drawn as a stroked+filled path, not two overlaid
        // drawText() calls) so it stays legible over any card color/art behind the badge.
        const double textX = badgeRect.left() + (badgeRect.width() - fm.horizontalAdvance(badgeText)) / 2.0;
        const double textY = badgeRect.top() + (badgeRect.height() + fm.ascent() - fm.descent()) / 2.0;
        QPainterPath textPath;
        textPath.addText(textX, textY, font, badgeText);
        painter->setPen(QPen(Qt::black, qMax(1.0, h * 0.06)));
        painter->setBrush(Qt::white);
        painter->drawPath(textPath);
        painter->restore();
    }

    if (!state->getPT().isEmpty()) {
        painter->save();
        transformPainter(painter, translatedSize, tapAngle);

        // Static/continuous ability slice, client-display extension: if the server has told us this
        // creature's boosted P/T (anthem/lord static effects plus +1/+1 counters -- see
        // AttrEffectivePT) and it actually differs from the base P/T below, show the boosted number
        // instead, colored to flag it as augmented rather than reusing the plain modified/unmodified
        // white-vs-orange distinction below.
        const QString &basePt = state->getPT();
        const QString &effectivePt = state->getEffectivePT();
        const bool hasBoost = !effectivePt.isEmpty() && effectivePt != basePt;

        QString displayedPt = basePt;
        if (hasBoost) {
            displayedPt = effectivePt;
            const auto baseParsed = parseAbsolutePT(basePt);
            const auto effectiveParsed = parseAbsolutePT(effectivePt);
            bool buffed = false;
            if (baseParsed && effectiveParsed) {
                buffed = effectiveParsed->first >= baseParsed->first && effectiveParsed->second >= baseParsed->second;
            }
            painter->setPen(buffed ? QColor(80, 220, 100) /* augmented green */
                                   : QColor(255, 150, 0) /* dark orange, same as a plain modification */);
        } else if (!getFaceDown() && basePt == exactCard.getInfo().getPowTough()) {
            painter->setPen(Qt::white);
        } else {
            painter->setPen(QColor(255, 150, 0)); // dark orange
        }

        painter->setBackground(Qt::black);
        painter->setBackgroundMode(Qt::OpaqueMode);

        painter->drawText(QRectF(4 * scaleFactor, 4 * scaleFactor, translatedSize.width() - 10 * scaleFactor,
                                 translatedSize.height() - 8 * scaleFactor),
                          Qt::AlignRight | Qt::AlignBottom, displayedPt);
        painter->restore();
    }

    if (!state->getAnnotation().isEmpty()) {
        painter->save();

        transformPainter(painter, translatedSize, tapAngle);
        painter->setBackground(Qt::black);
        painter->setBackgroundMode(Qt::OpaqueMode);
        painter->setPen(Qt::white);

        painter->drawText(QRectF(4 * scaleFactor, 4 * scaleFactor, translatedSize.width() - 8 * scaleFactor,
                                 translatedSize.height() - 8 * scaleFactor),
                          Qt::AlignCenter | Qt::TextWrapAnywhere, state->getAnnotation());
        painter->restore();
    }

    if (getBeingPointedAt()) {
        painter->fillPath(shape(), QBrush(QColor(255, 0, 0, 100)));
    }

    if (state->getDoesntUntap()) {
        painter->save();

        painter->setRenderHint(QPainter::Antialiasing, false);

        QPen pen;
        pen.setColor(Qt::magenta);
        pen.setWidth(0); // Cosmetic pen
        painter->setPen(pen);
        painter->drawPath(shape());

        painter->restore();
    }

    // Phase 8 Stage 6 (combat, declare-attacker slice): same outline convention as getDoesntUntap()
    // above, a distinct color so the two states remain visually unambiguous if both are ever true.
    if (state->getAttacking()) {
        painter->save();

        painter->setRenderHint(QPainter::Antialiasing, false);

        QPen pen;
        pen.setColor(QColor(255, 60, 0)); // red-orange
        pen.setWidth(0);                  // Cosmetic pen
        painter->setPen(pen);
        painter->drawPath(shape());

        painter->restore();
    }

    // Phase 8 combat automation, Stage A: a declared blocker, same outline convention, a third
    // distinct color.
    if (state->getBlocking()) {
        painter->save();

        painter->setRenderHint(QPainter::Antialiasing, false);

        QPen pen;
        pen.setColor(QColor(30, 120, 255)); // blue
        pen.setWidth(0);                    // Cosmetic pen
        painter->setPen(pen);
        painter->drawPath(shape());

        painter->restore();
    }

    painter->restore();
}

void CardItem::setAttacking(bool _attacking)
{
    state->setAttacking(_attacking);
    update();
}

void CardItem::setAttackTargetPlayerId(int _playerId)
{
    state->setAttackTargetPlayerId(_playerId);
    update();
}

void CardItem::setBlocked(int _playerId, int _cardId)
{
    state->setBlocked(_playerId, _cardId);
    update();
}

void CardItem::setCounter(int _id, int _value)
{
    state->setCounter(_id, _value);
    update();
}

void CardItem::setAnnotation(const QString &_annotation)
{
    state->setAnnotation(_annotation);
    update();
}

void CardItem::setDoesntUntap(bool _doesntUntap)
{
    state->setDoesntUntap(_doesntUntap);
    update();
}

void CardItem::setPT(const QString &_pt)
{
    state->setPT(_pt);
    update();
}

void CardItem::setEffectivePT(const QString &_effectivePt)
{
    state->setEffectivePT(_effectivePt);
    update();
}

void CardItem::setAttachedTo(CardItem *_attachedTo)
{
    if (state->getAttachedTo() != nullptr) {
        state->getAttachedTo()->removeAttachedCard(this);
    }

    gridPoint.setX(-1);
    state->setAttachedTo(_attachedTo);
    if (state->getAttachedTo() != nullptr) {
        // If the zone is being torn down, it might already be null by the time a card tries to un-attach all its
        // attached cards
        if (state->getAttachedTo()->getZone() == nullptr) {
            deleteLater();
        } else {
            emit state->getAttachedTo()->getZone()->cardAdded(this);
            state->getAttachedTo()->addAttachedCard(this);
            if (state->getZone() != state->getAttachedTo()->getZone()) {
                state->getAttachedTo()->getZone()->reorganizeCards();
            }
        }
    } else {
        // If the zone is being torn down, it might already be null by the time a card tries to un-attach all its
        // attached cards
        if (state->getZone() == nullptr) {
            deleteLater();
        } else {
            emit state->getZone()->cardAdded(this);
        }
    }

    if (state->getZone() != nullptr) {
        state->getZone()->reorganizeCards();
    }
}

/**
 * @brief Resets the fields that should be reset after a zone transition
 */
void CardItem::resetState(bool keepAnnotations)
{
    state->resetState(keepAnnotations);
    attachedCards.clear();
    setTapped(false, false);
    setDoesntUntap(false);
    if (scene()) {
        static_cast<GameScene *>(scene())->unregisterAnimationItem(this);
    }
    update();
}

void CardItem::processCardInfo(const ServerInfo_Card &_info)
{
    state->clearCounters();
    const int counterListSize = _info.counter_list_size();
    for (int i = 0; i < counterListSize; ++i) {
        const ServerInfo_CardCounter &counterInfo = _info.counter_list(i);
        state->insertCounter(counterInfo.id(), counterInfo.value());
    }

    setId(_info.id());
    setCardRef({QString::fromStdString(_info.name()), QString::fromStdString(_info.provider_id())});
    setAttacking(_info.attacking());
    setAttackTargetPlayerId(_info.attack_target_player_id());
    setBlocked(_info.blocked_player_id(), _info.blocked_card_id());
    setFaceDown(_info.face_down());
    setPT(QString::fromStdString(_info.pt()));
    setEffectivePT(QString::fromStdString(_info.effective_pt()));
    setAnnotation(QString::fromStdString(_info.annotation()));
    setColor(QString::fromStdString(_info.color()));
    setTapped(_info.tapped());
    setDestroyOnZoneChange(_info.destroy_on_zone_change());
    setDoesntUntap(_info.doesnt_untap());
}

CardDragItem *CardItem::createDragItem(int _id, const QPointF &_pos, const QPointF &_scenePos, bool forceFaceDown)
{
    deleteDragItem();
    dragItem = new CardDragItem(this, _id, _pos, forceFaceDown);
    dragItem->setVisible(false);
    scene()->addItem(dragItem);
    dragItem->updatePosition(_scenePos);
    dragItem->setVisible(true);

    return dragItem;
}

void CardItem::deleteDragItem()
{
    if (dragItem) {
        dragItem->deleteLater();
    }
    dragItem = nullptr;
}

void CardItem::drawArrow(const QColor &arrowColor)
{
    if (owner->getGame()->getPlayerManager()->isSpectator()) {
        return;
    }

    auto *game = owner->getGame();
    PlayerLogic *arrowOwner = game->getPlayerManager()->getActiveLocalPlayer(game->getGameState()->getActivePlayer());
    int phase = 0; // 0 means to not set the phase
    if (SettingsCache::instance().getDoNotDeleteArrowsInSubPhases()) {
        int currentPhase = game->getGameState()->getCurrentPhase();
        phase = Phases::getLastSubphase(currentPhase) + 1;
    }
    ArrowDragItem *arrow = new ArrowDragItem(arrowOwner, this, arrowColor, phase);
    scene()->addItem(arrow);
    arrow->grabMouse();

    for (const auto &item : scene()->selectedItems()) {
        CardItem *card = qgraphicsitem_cast<CardItem *>(item);
        if (card == nullptr || card == this) {
            continue;
        }
        if (card->getZone() != state->getZone()) {
            continue;
        }

        ArrowDragItem *childArrow = new ArrowDragItem(arrowOwner, card, arrowColor, phase);
        scene()->addItem(childArrow);
        arrow->addChildArrow(childArrow);
    }
}

void CardItem::drawAttachArrow()
{
    if (owner->getGame()->getPlayerManager()->isSpectator()) {
        return;
    }

    auto *arrow = new ArrowAttachItem(this);
    scene()->addItem(arrow);
    arrow->grabMouse();

    for (const auto &item : scene()->selectedItems()) {
        CardItem *card = qgraphicsitem_cast<CardItem *>(item);
        if (card == nullptr) {
            continue;
        }
        if (card->getZone() != state->getZone()) {
            continue;
        }

        ArrowAttachItem *childArrow = new ArrowAttachItem(card);
        scene()->addItem(childArrow);
        arrow->addChildArrow(childArrow);
    }
}

void CardItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->buttons().testFlag(Qt::RightButton)) {
        if ((event->screenPos() - event->buttonDownScreenPos(Qt::RightButton)).manhattanLength() <
            2 * QApplication::startDragDistance()) {
            return;
        }

        QColor arrowColor = Qt::red;
        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            arrowColor = Qt::yellow;
        } else if (event->modifiers().testFlag(Qt::AltModifier)) {
            arrowColor = Qt::blue;
        } else if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            arrowColor = Qt::green;
        }

        drawArrow(arrowColor);
    } else if (event->buttons().testFlag(Qt::LeftButton)) {
        if ((event->screenPos() - event->buttonDownScreenPos(Qt::LeftButton)).manhattanLength() <
            2 * QApplication::startDragDistance()) {
            return;
        }
        if (const ZoneViewZoneLogic *view = qobject_cast<const ZoneViewZoneLogic *>(state->getZone())) {
            if (view->getRevealZone() && !view->getWriteableRevealZone()) {
                return;
            }
        } else if (!owner->getPlayerInfo()->getLocalOrJudge()) {
            return;
        }

        bool forceFaceDown = event->modifiers().testFlag(Qt::ShiftModifier);

        // Use the buttonDownPos to align the hot spot with the position when
        // the user originally clicked
        createDragItem(id, event->buttonDownPos(Qt::LeftButton), event->scenePos(), forceFaceDown);
        dragItem->grabMouse();

        int childIndex = 0;
        for (const auto &item : scene()->selectedItems()) {
            CardItem *card = static_cast<CardItem *>(item);
            if ((card == this) || (card->getZone() != state->getZone())) {
                continue;
            }
            ++childIndex;
            QPointF childPos;
            if (state->getZone()->getHasCardAttr()) {
                childPos = card->pos() - pos();
            } else {
                childPos = QPointF(childIndex * CardDimensions::WIDTH_HALF_F, 0);
            }
            CardDragItem *drag =
                new CardDragItem(card, card->getId(), childPos, card->getFaceDown() || forceFaceDown, dragItem);
            drag->setPos(dragItem->pos() + childPos);
            scene()->addItem(drag);
        }
    }
    setCursor(Qt::OpenHandCursor);
}

void CardItem::playCard(bool faceDown)
{
    // Do nothing if the card belongs to another player
    if (!owner->getPlayerInfo()->getLocalOrJudge()) {
        return;
    }

    TableZoneLogic *tz = qobject_cast<TableZoneLogic *>(state->getZone());
    if (tz) {
        emit tz->toggleTapped();
    } else {
        if (SettingsCache::instance().getClickPlaysAllSelected()) {
            if (faceDown) {
                emit playSelectedFaceDown(this);
            } else {
                emit playSelected(this);
            }
        } else {
            state->getZone()->getPlayer()->getPlayerActions()->playCard(this, faceDown);
        }
    }
}

QVariantList CardItem::parsePT(const QString &pt)
{
    QVariantList ptList = QVariantList();
    if (!pt.isEmpty()) {
        int sep = pt.indexOf('/');
        if (sep == 0) {
            ptList.append(QVariant(pt.mid(1))); // cut off starting '/' and take full string
        } else {
            int start = 0;
            for (;;) {
                QString item = pt.mid(start, sep - start);
                if (item.isEmpty()) {
                    ptList.append(QVariant(QString()));
                } else if (item[0] == '+') {
                    ptList.append(QVariant(item.mid(1).toInt())); // add as int
                } else if (item[0] == '-') {
                    ptList.append(QVariant(item.toInt())); // add as int
                } else {
                    ptList.append(QVariant(item)); // add as qstring
                }
                if (sep == -1) {
                    break;
                }
                start = sep + 1;
                sep = pt.indexOf('/', start);
            }
        }
    }
    return ptList;
}

/**
 * @brief returns true if the zone is a unwritable reveal zone view (eg a card reveal window). Will return false if zone
 * is nullptr.
 */
static bool isUnwritableRevealZone(CardZoneLogic *zone)
{
    if (auto *view = qobject_cast<ZoneViewZoneLogic *>(zone)) {
        return view->getRevealZone() && !view->getWriteableRevealZone();
    }
    return false;
}

/**
 * This method is called when a "click to play" is done on the card.
 * This is either triggered by a single click or double click, depending on the settings.
 *
 * @param shiftHeld if the shift key was held during the click
 */
void CardItem::handleClickedToPlay(bool shiftHeld)
{
    if (isUnwritableRevealZone(state->getZone())) {
        if (SettingsCache::instance().getClickPlaysAllSelected()) {
            emit hideSelected(this);
        } else {
            state->getZone()->removeCard(this);
        }
    } else {
        playCard(shiftHeld);
    }
}

void CardItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::RightButton && owner != nullptr) {
        emit rightClicked(this, event->screenPos());
        return;
    }
    if ((event->modifiers() != Qt::AltModifier) && (event->button() == Qt::LeftButton) &&
        (!SettingsCache::instance().getDoubleClickToPlay())) {
        handleClickedToPlay(event->modifiers().testFlag(Qt::ShiftModifier));
    }
    if (owner != nullptr) {
        setCursor(Qt::OpenHandCursor);
    }
    AbstractCardItem::mouseReleaseEvent(event);
}

void CardItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    if ((event->modifiers() != Qt::AltModifier) && (event->buttons() == Qt::LeftButton) &&
        (SettingsCache::instance().getDoubleClickToPlay())) {
        handleClickedToPlay(event->modifiers().testFlag(Qt::ShiftModifier));
    }
    event->accept();
}

bool CardItem::animationEvent()
{
    int rotation = ROTATION_DEGREES_PER_FRAME;
    bool animationIncomplete = true;
    if (!tapped) {
        rotation *= -1;
    }

    tapAngle += rotation;
    if (tapped && (tapAngle > 90)) {
        tapAngle = 90;
        animationIncomplete = false;
    }
    if (!tapped && (tapAngle < 0)) {
        tapAngle = 0;
        animationIncomplete = false;
    }

    setTransform(QTransform()
                     .translate(CardDimensions::WIDTH_HALF_F, CardDimensions::HEIGHT_HALF_F)
                     .rotate(tapAngle)
                     .translate(-CardDimensions::WIDTH_HALF_F, -CardDimensions::HEIGHT_HALF_F));
    setHovered(false);
    update();

    return animationIncomplete;
}

QVariant CardItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if ((change == ItemSelectedHasChanged) && owner != nullptr) {
        bool selected = value.toBool();

        if (selected) {
            owner->getGame()->setActiveCard(this);
        }

        emit selectionChanged(this, selected);
    }

    return AbstractCardItem::itemChange(change, value);
}