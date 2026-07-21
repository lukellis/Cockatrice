#include "player_actions.h"

#include "../../game_graphics/board/ability_target_picker.h"
#include "../../game_graphics/dialogs/dlg_choose_generic_mana_payment.h"
#include "../../game_graphics/dialogs/dlg_choose_variable_mana_cost.h"
#include "../../game_graphics/dialogs/dlg_move_top_cards_until.h"
#include "../../game_graphics/dialogs/dlg_roll_dice.h"
#include "../../game_graphics/player/card_menu_action_type.h"
#include "../../game_graphics/zones/hand_zone.h"
#include "../../game_graphics/zones/table_zone.h"
#include "../../interface/widgets/tabs/tab_game.h"
#include "../../interface/widgets/utility/get_text_with_max.h"
#include "../board/counter_state.h"
#include "../zones/view_zone_logic.h"

#include <QGraphicsScene>
#include <QMessageBox>
#include <libcockatrice/card/ability/activated_abilities.h>
#include <libcockatrice/card/ability/card_keywords.h>
#include <libcockatrice/card/ability/mana_abilities.h>
#include <libcockatrice/card/ability/spell_mana_cost.h>
#include <libcockatrice/card/ability/static_abilities.h>
#include <libcockatrice/card/ability/triggered_abilities.h>
#include <libcockatrice/card/database/card_database_manager.h>
#include <libcockatrice/card/relation/card_relation.h>
#include <libcockatrice/protocol/pb/command_activate_ability.pb.h>
#include <libcockatrice/protocol/pb/command_attach_card.pb.h>
#include <libcockatrice/protocol/pb/command_change_zone_properties.pb.h>
#include <libcockatrice/protocol/pb/command_create_token.pb.h>
#include <libcockatrice/protocol/pb/command_draw_cards.pb.h>
#include <libcockatrice/protocol/pb/command_flip_card.pb.h>
#include <libcockatrice/protocol/pb/command_game_say.pb.h>
#include <libcockatrice/protocol/pb/command_inc_counter.pb.h>
#include <libcockatrice/protocol/pb/command_move_card.pb.h>
#include <libcockatrice/protocol/pb/command_mulligan.pb.h>
#include <libcockatrice/protocol/pb/command_reveal_cards.pb.h>
#include <libcockatrice/protocol/pb/command_roll_die.pb.h>
#include <libcockatrice/protocol/pb/command_set_card_attr.pb.h>
#include <libcockatrice/protocol/pb/command_set_card_counter.pb.h>
#include <libcockatrice/protocol/pb/command_shuffle.pb.h>
#include <libcockatrice/protocol/pb/command_undo_draw.pb.h>
#include <libcockatrice/protocol/pb/context_move_card.pb.h>
#include <libcockatrice/rules/rules_engine.h>
#include <libcockatrice/utility/clamped_arithmetic.h>
#include <libcockatrice/utility/counter_limits.h>
#include <libcockatrice/utility/expression.h>
#include <libcockatrice/utility/zone_names.h>
#include <optional>

// milliseconds in between triggers of the move top cards until action
static constexpr int MOVE_TOP_CARD_UNTIL_INTERVAL = 100;

PlayerActions::PlayerActions(PlayerLogic *_player)
    : QObject(_player), player(_player), lastTokenTableRow(0), movingCardsUntil(false)
{
    connect(this, &PlayerActions::requestZoneViewToggle, player, &PlayerLogic::onRequestZoneViewToggle);

    moveTopCardTimer = new QTimer(this);
    moveTopCardTimer->setInterval(MOVE_TOP_CARD_UNTIL_INTERVAL);
    moveTopCardTimer->setSingleShot(true);
    connect(moveTopCardTimer, &QTimer::timeout, [this]() { actMoveTopCardToPlay(); });
}

void PlayerActions::playCard(CardItem *card, bool faceDown)
{
    if (card == nullptr) {
        return;
    }

    QList<const ::google::protobuf::Message *> manaPaymentCommands;
    if (!gateManaCostForHandPlay(card, faceDown, manaPaymentCommands)) {
        return; // unaffordable, or the player cancelled a color-choice dialog -- nothing sent
    }

    Command_MoveCard cmd;
    cmd.set_start_player_id(card->getZone()->getPlayer()->getPlayerInfo()->getId());
    cmd.set_start_zone(card->getZone()->getName().toStdString());
    cmd.set_target_player_id(player->getPlayerInfo()->getId());
    CardToMove *cardToMove = cmd.mutable_cards_to_move()->add_card();
    cardToMove->set_card_id(card->getId());

    ExactCard exactCard = card->getCard();
    if (!exactCard) {
        return;
    }

    const CardInfo &info = exactCard.getInfo();

    int tableRow = info.getUiAttributes().tableRow;
    bool playToStack = SettingsCache::instance().getPlayToStack();
    QString currentZone = card->getZone()->getName();
    if (!faceDown && currentZone == ZoneNames::STACK && tableRow == 3) {
        cmd.set_target_zone(ZoneNames::GRAVE);
        cmd.set_x(0);
        cmd.set_y(0);
    } else if (!faceDown && ((!playToStack && tableRow == 3) ||
                             ((playToStack && tableRow != 0) && currentZone != ZoneNames::STACK))) {
        cmd.set_target_zone(ZoneNames::STACK);
        cmd.set_x(-1);
        cmd.set_y(0);
    } else {
        tableRow = faceDown ? 2 : info.getUiAttributes().tableRow;
        QPoint gridPoint = QPoint(-1, TableZone::tableRowToGridY(tableRow));
        cardToMove->set_face_down(faceDown);
        if (!faceDown) {
            cardToMove->set_pt(info.getPowTough().toStdString());
            cardToMove->set_keywords(
                QStringList(CardKeywords::parse(info).values()).join(QStringLiteral(",")).toStdString());
            cardToMove->set_static_abilities(StaticAbilities::serialize(StaticAbilities::parse(info)).toStdString());
        }
        cardToMove->set_tapped(!faceDown && info.getUiAttributes().cipt);
        if (tableRow != 3) {
            cmd.set_target_zone(ZoneNames::TABLE);
        }
        cmd.set_x(gridPoint.x());
        cmd.set_y(gridPoint.y());
    }
    if (manaPaymentCommands.isEmpty()) {
        sendGameCommand(cmd);
    } else {
        // prepareGameCommand(QList<const Message*>) deletes every pointer it's given, so the
        // batched send needs its own heap copy of cmd rather than &cmd (a stack object) --
        // sendGameCommand(cmd) above is fine as-is since that overload copies via CopyFrom and
        // never takes ownership of the reference.
        QList<const ::google::protobuf::Message *> allCommands{new Command_MoveCard(cmd)};
        allCommands.append(manaPaymentCommands);
        sendGameCommand(prepareGameCommand(allCommands));
    }
}

/**
 * Like {@link PlayerActions::playCard}, but forces the card to be played to the table zone.
 * Cards with tablerow 3 (the stack) will be played to tablerow 1 (the noncreatures row).
 */
void PlayerActions::playCardToTable(const CardItem *card, bool faceDown)
{
    if (card == nullptr) {
        return;
    }

    Command_MoveCard cmd;
    cmd.set_start_player_id(card->getZone()->getPlayer()->getPlayerInfo()->getId());
    cmd.set_start_zone(card->getZone()->getName().toStdString());
    cmd.set_target_player_id(player->getPlayerInfo()->getId());
    CardToMove *cardToMove = cmd.mutable_cards_to_move()->add_card();
    cardToMove->set_card_id(card->getId());

    ExactCard exactCard = card->getCard();
    if (!exactCard) {
        return;
    }

    const CardInfo &info = exactCard.getInfo();

    int tableRow = faceDown ? 2 : info.getUiAttributes().tableRow;
    QPoint gridPoint = QPoint(-1, TableZone::tableRowToGridY(tableRow));
    cardToMove->set_face_down(faceDown);
    if (!faceDown) {
        cardToMove->set_pt(info.getPowTough().toStdString());
        cardToMove->set_keywords(
            QStringList(CardKeywords::parse(info).values()).join(QStringLiteral(",")).toStdString());
        cardToMove->set_static_abilities(StaticAbilities::serialize(StaticAbilities::parse(info)).toStdString());
    }
    cardToMove->set_tapped(!faceDown && info.getUiAttributes().cipt);
    cmd.set_target_zone(ZoneNames::TABLE);
    cmd.set_x(gridPoint.x());
    cmd.set_y(gridPoint.y());
    sendGameCommand(cmd);
}

void PlayerActions::actViewLibrary()
{
    emit requestZoneViewToggle(ZoneNames::DECK, -1);
}

void PlayerActions::actViewHand()
{
    emit requestZoneViewToggle(ZoneNames::HAND, -1);
}

/**
 * @brief The sortHand actions only pass along a single SortOption in its data.
 * This method fills out the rest of the sort priority list given that option.
 * @param option The single sort option
 * @return The sort priority list
 */
static QList<CardList::SortOption> expandSortOption(CardList::SortOption option)
{
    switch (option) {
        case CardList::SortByName:
            return {};
        case CardList::SortByMainType:
            return {CardList::SortByMainType, CardList::SortByManaValue};
        case CardList::SortByManaValue:
            return {CardList::SortByManaValue, CardList::SortByColors};
        default:
            return {option};
    }
}

void PlayerActions::actSortHand()
{
    auto *action = qobject_cast<QAction *>(sender());
    CardList::SortOption option = static_cast<CardList::SortOption>(action->data().toInt());

    QList<CardList::SortOption> sortOptions = expandSortOption(option);

    static QList defaultOptions = {CardList::SortByName, CardList::SortByPrinting};

    emit requestSortHand(sortOptions + defaultOptions);
}

void PlayerActions::actRequestViewTopCardsDialog()
{
    emit requestViewTopCardsDialog(defaultNumberTopCards, player->getDeckZone()->getCards().size());
}

void PlayerActions::actViewTopCards(int number)
{
    defaultNumberTopCards = number;
    emit requestZoneViewToggle(ZoneNames::DECK, number);
}

void PlayerActions::actRequestViewBottomCardsDialog()
{
    emit requestViewBottomCardsDialog(defaultNumberBottomCards, player->getDeckZone()->getCards().size());
}

void PlayerActions::actViewBottomCards(int number)
{
    defaultNumberBottomCards = number;
    emit requestZoneViewToggle(ZoneNames::DECK, number, true);
}

void PlayerActions::actAlwaysRevealTopCard(bool alwaysRevealTopCard)
{
    Command_ChangeZoneProperties cmd;
    cmd.set_zone_name(ZoneNames::DECK);
    cmd.set_always_reveal_top_card(alwaysRevealTopCard);

    sendGameCommand(cmd);
}

void PlayerActions::actAlwaysLookAtTopCard(bool alwaysRevealTopCard)
{
    Command_ChangeZoneProperties cmd;
    cmd.set_zone_name(ZoneNames::DECK);
    cmd.set_always_look_at_top_card(alwaysRevealTopCard);

    sendGameCommand(cmd);
}

void PlayerActions::actOpenDeckInDeckEditor()
{
    emit player->openDeckEditor({.deckList = player->getDeck()});
}

void PlayerActions::actViewGraveyard()
{
    emit requestZoneViewToggle(ZoneNames::GRAVE, -1);
}

void PlayerActions::actViewRfg()
{
    emit requestZoneViewToggle(ZoneNames::EXILE, -1);
}

void PlayerActions::actViewSideboard()
{
    emit requestZoneViewToggle(ZoneNames::SIDEBOARD, -1);
}

void PlayerActions::actShuffle()
{
    sendGameCommand(Command_Shuffle());
}

void PlayerActions::actRequestShuffleTopDialog()
{
    const int maxCards = player->getDeckZone()->getCards().size();
    if (maxCards == 0) {
        return;
    }

    emit requestShuffleTopDialog(defaultNumberTopCards, maxCards);
}

void PlayerActions::actShuffleTop(int number)
{
    const int maxCards = player->getDeckZone()->getCards().size();
    if (maxCards == 0) {
        return;
    }

    if (number > maxCards) {
        number = maxCards;
    }

    defaultNumberTopCards = number;

    Command_Shuffle cmd;
    cmd.set_zone_name(ZoneNames::DECK);
    cmd.set_start(0);
    cmd.set_end(number - 1); // inclusive, the indexed card at end will be shuffled

    sendGameCommand(cmd);
}

void PlayerActions::actRequestShuffleBottomDialog()
{
    const int maxCards = player->getDeckZone()->getCards().size();
    if (maxCards == 0) {
        return;
    }

    emit requestShuffleBottomDialog(defaultNumberBottomCards, maxCards);
}

void PlayerActions::actShuffleBottom(int number)
{
    const int maxCards = player->getDeckZone()->getCards().size();
    if (maxCards == 0) {
        return;
    }

    if (number > maxCards) {
        number = maxCards;
    }

    defaultNumberBottomCards = number;

    Command_Shuffle cmd;
    cmd.set_zone_name(ZoneNames::DECK);
    cmd.set_start(-number);
    cmd.set_end(-1);

    sendGameCommand(cmd);
}

void PlayerActions::actDrawCard()
{
    Command_DrawCards cmd;
    cmd.set_number(1);
    sendGameCommand(cmd);
}

void PlayerActions::actRequestMulliganDialog()
{
    int startSize = SettingsCache::instance().getStartingHandSize();
    int handSize = player->getHandZone()->getCards().size();
    int deckSize = player->getDeckZone()->getCards().size() + handSize;

    emit requestMulliganDialog(startSize, handSize, deckSize);
}

void PlayerActions::actMulligan(int number)
{
    int handSize = player->getHandZone()->getCards().size();

    if (number < 1) {
        number = handSize + number;
    }

    doMulligan(number);
    SettingsCache::instance().setStartingHandSize(number);
}

void PlayerActions::actMulliganSameSize()
{
    int handSize = player->getHandZone()->getCards().size();
    doMulligan(handSize);
}

void PlayerActions::actMulliganMinusOne()
{
    int handSize = player->getHandZone()->getCards().size();
    int targetSize = qMax(1, handSize - 1);
    doMulligan(targetSize);
}

void PlayerActions::doMulligan(int number)
{
    if (number < 1) {
        return;
    }

    Command_Mulligan cmd;
    cmd.set_number(number);
    sendGameCommand(cmd);
}

void PlayerActions::actRequestDrawCardsDialog()
{
    int deckSize = player->getDeckZone()->getCards().size();

    emit requestDrawCardsDialog(defaultNumberTopCards, deckSize);
}

void PlayerActions::actDrawCards(int number)
{
    defaultNumberTopCards = number;
    Command_DrawCards cmd;
    cmd.set_number(static_cast<google::protobuf::uint32>(number));
    sendGameCommand(cmd);
}

void PlayerActions::actUndoDraw()
{
    sendGameCommand(Command_UndoDraw());
}

void PlayerActions::cmdSetTopCard(Command_MoveCard &cmd)
{
    cmd.set_start_zone(ZoneNames::DECK);
    auto *cardToMove = cmd.mutable_cards_to_move()->add_card();
    cardToMove->set_card_id(0);
    cmd.set_target_player_id(player->getPlayerInfo()->getId());
}

void PlayerActions::cmdSetBottomCard(Command_MoveCard &cmd)
{
    CardZoneLogic *zone = player->getDeckZone();
    int lastCard = zone->getCards().size() - 1;
    cmd.set_start_zone(ZoneNames::DECK);
    auto *cardToMove = cmd.mutable_cards_to_move()->add_card();
    cardToMove->set_card_id(lastCard);
    cmd.set_target_player_id(player->getPlayerInfo()->getId());
}

void PlayerActions::actMoveTopCardToGrave()
{
    if (player->getDeckZone()->getCards().empty()) {
        return;
    }

    Command_MoveCard cmd;
    cmdSetTopCard(cmd);
    cmd.set_target_zone(ZoneNames::GRAVE);
    cmd.set_x(0);
    cmd.set_y(0);

    sendGameCommand(cmd);
}

void PlayerActions::actMoveTopCardToExile()
{
    if (player->getDeckZone()->getCards().empty()) {
        return;
    }

    Command_MoveCard cmd;
    cmdSetTopCard(cmd);
    cmd.set_target_zone(ZoneNames::EXILE);
    cmd.set_x(0);
    cmd.set_y(0);

    sendGameCommand(cmd);
}

void PlayerActions::actMoveTopCardsToGrave()
{
    actRequestMoveTopCardsToDialog(ZoneNames::GRAVE, tr("grave"), false);
}

void PlayerActions::actMoveTopCardsToGraveFaceDown()
{
    actRequestMoveTopCardsToDialog(ZoneNames::GRAVE, tr("grave"), true);
}

void PlayerActions::actMoveTopCardsToExile()
{
    actRequestMoveTopCardsToDialog(ZoneNames::EXILE, tr("exile"), false);
}

void PlayerActions::actMoveTopCardsToExileFaceDown()
{
    actRequestMoveTopCardsToDialog(ZoneNames::EXILE, tr("exile"), true);
}

void PlayerActions::actRequestMoveTopCardsToDialog(const QString &targetZone,
                                                   const QString &zoneDisplayName,
                                                   bool faceDown)
{
    const int maxCards = player->getDeckZone()->getCards().size();
    if (maxCards == 0) {
        return;
    }

    emit requestMoveTopCardsToDialog(defaultNumberTopCards, maxCards, targetZone, zoneDisplayName, faceDown);
}

void PlayerActions::moveTopCardsTo(int number, const QString &targetZone, bool faceDown)
{
    const int maxCards = player->getDeckZone()->getCards().size();
    if (maxCards == 0) {
        return;
    }

    if (number > maxCards) {
        number = maxCards;
    }
    defaultNumberTopCards = number;

    Command_MoveCard cmd;
    cmd.set_start_zone(ZoneNames::DECK);
    cmd.set_target_player_id(player->getPlayerInfo()->getId());
    cmd.set_target_zone(targetZone.toStdString());
    cmd.set_x(0);
    cmd.set_y(0);

    for (int i = number - 1; i >= 0; --i) {
        auto card = cmd.mutable_cards_to_move()->add_card();
        card->set_card_id(i);
        if (faceDown) {
            card->set_face_down(true);
        }
    }

    sendGameCommand(cmd);
}

void PlayerActions::actRequestMoveTopCardsUntilDialog()
{
    stopMoveTopCardsUntil();

    emit requestMoveTopCardsUntilDialog(movingCardsUntilOptions);
}

void PlayerActions::moveTopCardsUntil(const QString &expr, MoveTopCardsUntilOptions options)
{
    movingCardsUntilOptions = options;

    if (player->getDeckZone()->getCards().empty()) {
        stopMoveTopCardsUntil();
    } else {
        movingCardsUntilFilter = FilterString(expr);
        movingCardsUntilCounter = movingCardsUntilOptions.numberOfHits;
        movingCardsUntil = true;
        actMoveTopCardToPlay();
    }
}

void PlayerActions::moveOneCardUntil(CardItem *card)
{
    moveTopCardTimer->stop();

    const bool isMatch = card && movingCardsUntilFilter.check(card->getCard().getCardPtr());

    if (isMatch && movingCardsUntilOptions.autoPlay) {
        // Directly calling playCard will deadlock, since we are already in the middle of processing an event.
        // Use QTimer::singleShot to queue up the playCard on the event loop.
        QTimer::singleShot(0, this, [card, this] { playCard(card, false); });
    }

    if (player->getDeckZone()->getCards().empty() || !card) {
        stopMoveTopCardsUntil();
    } else if (isMatch) {
        --movingCardsUntilCounter;
        if (movingCardsUntilCounter > 0) {
            moveTopCardTimer->start();
        } else {
            stopMoveTopCardsUntil();
        }
    } else {
        moveTopCardTimer->start();
    }
}

/**
 * @brief Immediately stops any ongoing `play top card to stack until...` process, resetting all variables involved.
 */
void PlayerActions::stopMoveTopCardsUntil()
{
    moveTopCardTimer->stop();
    movingCardsUntilCounter = 0;
    movingCardsUntil = false;
}

void PlayerActions::actMoveTopCardToBottom()
{
    if (player->getDeckZone()->getCards().empty()) {
        return;
    }

    Command_MoveCard cmd;
    cmdSetTopCard(cmd);
    cmd.set_target_zone(ZoneNames::DECK);
    cmd.set_x(-1); // bottom of deck
    cmd.set_y(0);

    sendGameCommand(cmd);
}

void PlayerActions::actMoveTopCardToPlay()
{
    if (player->getDeckZone()->getCards().empty()) {
        return;
    }

    Command_MoveCard cmd;
    cmdSetTopCard(cmd);
    cmd.set_target_zone(ZoneNames::STACK);
    cmd.set_x(-1);
    cmd.set_y(0);

    sendGameCommand(cmd);
}

void PlayerActions::actMoveTopCardToPlayFaceDown()
{
    if (player->getDeckZone()->getCards().empty()) {
        return;
    }

    Command_MoveCard cmd;
    cmd.set_start_zone(ZoneNames::DECK);
    CardToMove *cardToMove = cmd.mutable_cards_to_move()->add_card();
    cardToMove->set_card_id(0);
    cardToMove->set_face_down(true);
    cmd.set_target_player_id(player->getPlayerInfo()->getId());
    cmd.set_target_zone(ZoneNames::TABLE);
    cmd.set_x(-1);
    cmd.set_y(0);

    sendGameCommand(cmd);
}

void PlayerActions::actMoveBottomCardToGrave()
{
    if (player->getDeckZone()->getCards().empty()) {
        return;
    }

    Command_MoveCard cmd;
    cmdSetBottomCard(cmd);
    cmd.set_target_zone(ZoneNames::GRAVE);
    cmd.set_x(0);
    cmd.set_y(0);

    sendGameCommand(cmd);
}

void PlayerActions::actMoveBottomCardToExile()
{
    if (player->getDeckZone()->getCards().empty()) {
        return;
    }

    Command_MoveCard cmd;
    cmdSetBottomCard(cmd);
    cmd.set_target_zone(ZoneNames::EXILE);
    cmd.set_x(0);
    cmd.set_y(0);

    sendGameCommand(cmd);
}

void PlayerActions::actMoveBottomCardsToGrave()
{
    actRequestMoveBottomCardsToDialog(ZoneNames::GRAVE, tr("grave"), false);
}

void PlayerActions::actMoveBottomCardsToGraveFaceDown()
{
    actRequestMoveBottomCardsToDialog(ZoneNames::GRAVE, tr("grave"), true);
}

void PlayerActions::actMoveBottomCardsToExile()
{
    actRequestMoveBottomCardsToDialog(ZoneNames::EXILE, tr("exile"), false);
}

void PlayerActions::actMoveBottomCardsToExileFaceDown()
{
    actRequestMoveBottomCardsToDialog(ZoneNames::EXILE, tr("exile"), true);
}

void PlayerActions::actRequestMoveBottomCardsToDialog(const QString &targetZone,
                                                      const QString &zoneDisplayName,
                                                      bool faceDown)
{
    const int maxCards = player->getDeckZone()->getCards().size();
    if (maxCards == 0) {
        return;
    }

    emit requestMoveBottomCardsToDialog(defaultNumberBottomCards, maxCards, targetZone, zoneDisplayName, faceDown);
}

void PlayerActions::moveBottomCardsTo(int number, const QString &targetZone, bool faceDown)
{
    const int maxCards = player->getDeckZone()->getCards().size();
    if (maxCards == 0) {
        return;
    }

    if (number > maxCards) {
        number = maxCards;
    }
    defaultNumberBottomCards = number;

    Command_MoveCard cmd;
    cmd.set_start_zone(ZoneNames::DECK);
    cmd.set_target_player_id(player->getPlayerInfo()->getId());
    cmd.set_target_zone(targetZone.toStdString());
    cmd.set_x(0);
    cmd.set_y(0);

    for (int i = maxCards - number; i < maxCards; ++i) {
        auto card = cmd.mutable_cards_to_move()->add_card();
        card->set_card_id(i);
        if (faceDown) {
            card->set_face_down(true);
        }
    }

    sendGameCommand(cmd);
}

void PlayerActions::actMoveBottomCardToTop()
{
    if (player->getDeckZone()->getCards().empty()) {
        return;
    }

    Command_MoveCard cmd;
    cmdSetBottomCard(cmd);
    cmd.set_target_zone(ZoneNames::DECK);
    cmd.set_x(0); // top of deck
    cmd.set_y(0);

    sendGameCommand(cmd);
}

/**
 * Selects all cards in the given zone.
 *
 * @param zone The zone to select from
 * @param filter A predicate to filter which cards are selected. Defaults to always returning true.
 */
static void selectCardsInZone(
    const CardZoneLogic *zone,
    std::function<bool(const CardItem *)> filter = [](const CardItem *) { return true; })
{
    if (!zone) {
        return;
    }

    for (auto &cardItem : zone->getCards()) {
        if (cardItem && filter(cardItem)) {
            cardItem->setSelected(true);
        }
    }
}

void PlayerActions::actSelectAll()
{
    const CardItem *card = player->getGame()->getActiveCard();
    if (!card) {
        return;
    }

    selectCardsInZone(card->getZone());
}

void PlayerActions::actSelectRow()
{
    const CardItem *card = player->getGame()->getActiveCard();
    if (!card) {
        return;
    }

    auto isSameRow = [card](const CardItem *cardItem) {
        return qAbs(card->scenePos().y() - cardItem->scenePos().y()) < 50;
    };
    selectCardsInZone(card->getZone(), isSameRow);
}

void PlayerActions::actSelectColumn()
{
    const CardItem *card = player->getGame()->getActiveCard();
    if (!card) {
        return;
    }

    auto isSameColumn = [card](const CardItem *cardItem) { return cardItem->x() == card->x(); };
    selectCardsInZone(card->getZone(), isSameColumn);
}

void PlayerActions::actDrawBottomCard()
{
    if (player->getDeckZone()->getCards().empty()) {
        return;
    }

    Command_MoveCard cmd;
    cmdSetBottomCard(cmd);
    cmd.set_target_zone(ZoneNames::HAND);
    cmd.set_x(0);
    cmd.set_y(0);

    sendGameCommand(cmd);
}

void PlayerActions::actRequestDrawBottomCardsDialog()
{
    const int maxCards = player->getDeckZone()->getCards().size();
    if (maxCards == 0) {
        return;
    }

    emit requestDrawBottomCardsDialog(defaultNumberBottomCards, maxCards);
}

void PlayerActions::actDrawBottomCards(int number)
{
    const int maxCards = player->getDeckZone()->getCards().size();
    if (maxCards == 0) {
        return;
    }

    if (number > maxCards) {
        number = maxCards;
    }
    defaultNumberBottomCards = number;

    Command_MoveCard cmd;
    cmd.set_start_zone(ZoneNames::DECK);
    cmd.set_target_player_id(player->getPlayerInfo()->getId());
    cmd.set_target_zone(ZoneNames::HAND);
    cmd.set_x(0);
    cmd.set_y(0);

    for (int i = maxCards - number; i < maxCards; ++i) {
        cmd.mutable_cards_to_move()->add_card()->set_card_id(i);
    }

    sendGameCommand(cmd);
}

void PlayerActions::actMoveBottomCardToPlay()
{
    if (player->getDeckZone()->getCards().empty()) {
        return;
    }

    Command_MoveCard cmd;
    cmdSetBottomCard(cmd);
    cmd.set_target_zone(ZoneNames::STACK);
    cmd.set_x(-1);
    cmd.set_y(0);

    sendGameCommand(cmd);
}

void PlayerActions::actMoveBottomCardToPlayFaceDown()
{
    if (player->getDeckZone()->getCards().empty()) {
        return;
    }

    CardZoneLogic *zone = player->getDeckZone();
    int lastCard = zone->getCards().size() - 1;

    Command_MoveCard cmd;
    cmd.set_start_zone(ZoneNames::DECK);
    auto *cardToMove = cmd.mutable_cards_to_move()->add_card();
    cardToMove->set_card_id(lastCard);
    cardToMove->set_face_down(true);

    cmd.set_target_player_id(player->getPlayerInfo()->getId());
    cmd.set_target_zone(ZoneNames::TABLE);
    cmd.set_x(-1);
    cmd.set_y(0);

    sendGameCommand(cmd);
}

void PlayerActions::actUntapAll()
{
    Command_SetCardAttr cmd;
    cmd.set_zone(ZoneNames::TABLE);
    cmd.set_attribute(AttrTapped);
    cmd.set_attr_value("0");

    sendGameCommand(cmd);
}

void PlayerActions::actRequestRollDieDialog()
{
    emit requestRollDieDialog();
}

void PlayerActions::actRollDie(int sides, int count)
{
    Command_RollDie cmd;
    cmd.set_sides(sides);
    cmd.set_count(count);
    sendGameCommand(cmd);
}

void PlayerActions::actFlipCoin()
{
    Command_RollDie cmd;
    cmd.set_sides(2);
    cmd.set_count(1);
    sendGameCommand(cmd);
}

void PlayerActions::actRequestCreateTokenDialog(const QStringList &predefinedTokens)
{
    emit requestCreateTokenDialog(predefinedTokens);
}

void PlayerActions::actCreateToken(TokenInfo tokenToCreate)
{
    lastTokenInfo = tokenToCreate;

    ExactCard correctedCard = CardDatabaseManager::query()->guessCard({lastTokenInfo.name, lastTokenInfo.providerId});
    if (correctedCard) {
        lastTokenInfo.name = correctedCard.getName();
        int tableRow = lastTokenInfo.faceDown ? 2 : correctedCard.getInfo().getUiAttributes().tableRow;
        lastTokenTableRow = TableZone::tableRowToGridY(tableRow);
        if (lastTokenInfo.pt.isEmpty()) {
            lastTokenInfo.pt = correctedCard.getInfo().getPowTough();
        }
    }

    emit requestEnableAndSetCreateAnotherTokenAction(lastTokenInfo.name);
    actCreateAnotherToken();
}

void PlayerActions::actCreateAnotherToken()
{
    if (lastTokenInfo.name.isEmpty()) {
        return;
    }

    Command_CreateToken cmd;
    cmd.set_zone(ZoneNames::TABLE);
    cmd.set_card_name(lastTokenInfo.name.toStdString());
    cmd.set_card_provider_id(lastTokenInfo.providerId.toStdString());
    cmd.set_color(lastTokenInfo.color.toStdString());
    cmd.set_pt(lastTokenInfo.pt.toStdString());
    cmd.set_annotation(lastTokenInfo.annotation.toStdString());
    cmd.set_destroy_on_zone_change(lastTokenInfo.destroy);
    cmd.set_face_down(lastTokenInfo.faceDown);
    cmd.set_x(-1);
    cmd.set_y(lastTokenTableRow);

    sendGameCommand(cmd);
}

void PlayerActions::setLastToken(CardInfoPtr cardInfo)
{
    if (cardInfo == nullptr) {
        return;
    }

    emit requestSetLastToken(cardInfo);
}

void PlayerActions::setLastTokenInfo(CardInfoPtr cardInfo)
{
    if (cardInfo == nullptr) {
        return;
    }

    lastTokenInfo = {.name = cardInfo->getName(),
                     .color = cardInfo->getColors().isEmpty() ? QString() : cardInfo->getColors().left(1).toLower(),
                     .pt = cardInfo->getPowTough(),
                     .annotation = SettingsCache::instance().getAnnotateTokens() ? cardInfo->getText() : "",
                     .destroy = true,
                     .providerId =
                         SettingsCache::instance().cardOverrides().getCardPreferenceOverride(cardInfo->getName())};

    lastTokenTableRow = TableZone::tableRowToGridY(cardInfo->getUiAttributes().tableRow);

    emit requestEnableAndSetCreateAnotherTokenAction(lastTokenInfo.name);
}

void PlayerActions::actCreatePredefinedToken()
{
    auto *action = static_cast<QAction *>(sender());
    CardInfoPtr cardInfo = CardDatabaseManager::query()->getCardInfo(action->text());
    if (!cardInfo) {
        return;
    }

    setLastToken(cardInfo);

    actCreateAnotherToken();
}

void PlayerActions::actCreateRelatedCard()
{
    const CardItem *sourceCard = player->getGame()->getActiveCard();

    if (!sourceCard) {
        return;
    }

    auto *action = static_cast<QAction *>(sender());
    // If there is a better way of passing a CardRelation through a QAction, please add it here.
    auto relatedCards = sourceCard->getCardInfo().getAllRelatedCards();

    CardRelation *cardRelation = relatedCards.at(action->data().toInt());
    actRequestCreateRelatedFromRelationDialog(sourceCard, cardRelation);
}

void PlayerActions::actCreateAllRelatedCards()
{
    const CardItem *sourceCard = player->getGame()->getActiveCard();
    if (!sourceCard) {
        return;
    }

    auto relatedCards = sourceCard->getCardInfo().getAllRelatedCards();
    if (relatedCards.isEmpty()) {
        return;
    }

    CardRelation *cardRelation = nullptr;
    int tokensTypesCreated = 0;

    if (relatedCards.length() == 1) {
        cardRelation = relatedCards.at(0);
        lastRelatedCreationSucceeded = false; // reset before emit
        actRequestCreateRelatedFromRelationDialog(sourceCard, cardRelation);
        if (lastRelatedCreationSucceeded) {
            ++tokensTypesCreated;
        }
    } else {
        QList<CardRelation *> nonExcludedRelatedCards;
        QString dbName;
        for (CardRelation *cardRelationTemp : relatedCards) {
            if (!cardRelationTemp->getIsCreateAllExclusion() && !cardRelationTemp->getDoesAttach()) {
                nonExcludedRelatedCards.append(cardRelationTemp);
            }
        }
        switch (nonExcludedRelatedCards.length()) {
            case 1:
                cardRelation = nonExcludedRelatedCards.at(0);
                lastRelatedCreationSucceeded = false; // reset before emit
                actRequestCreateRelatedFromRelationDialog(sourceCard, cardRelation);
                if (lastRelatedCreationSucceeded) {
                    ++tokensTypesCreated;
                }
                break;

            // If all are marked "Exclude", then treat the situation as if none of them are.
            // We won't accept "garbage in, garbage out", here.
            case 0:
                for (CardRelation *cardRelationAll : relatedCards) {
                    if (!cardRelationAll->getDoesAttach() && !cardRelationAll->getIsVariable()) {
                        dbName = cardRelationAll->getName();
                        bool persistent = cardRelationAll->getIsPersistent();
                        bool faceDown = cardRelationAll->getIsFaceDown();
                        for (int i = 0; i < cardRelationAll->getDefaultCount(); ++i) {
                            createCard(sourceCard, dbName, CardRelationType::DoesNotAttach, persistent, faceDown);
                        }
                        ++tokensTypesCreated;
                        if (tokensTypesCreated == 1) {
                            cardRelation = cardRelationAll;
                        }
                    }
                }
                break;

            default:
                for (CardRelation *cardRelationNotExcluded : nonExcludedRelatedCards) {
                    if (!cardRelationNotExcluded->getDoesAttach() && !cardRelationNotExcluded->getIsVariable()) {
                        dbName = cardRelationNotExcluded->getName();
                        bool persistent = cardRelationNotExcluded->getIsPersistent();
                        bool faceDown = cardRelationNotExcluded->getIsFaceDown();
                        for (int i = 0; i < cardRelationNotExcluded->getDefaultCount(); ++i) {
                            createCard(sourceCard, dbName, CardRelationType::DoesNotAttach, persistent, faceDown);
                        }
                        ++tokensTypesCreated;
                        if (tokensTypesCreated == 1) {
                            cardRelation = cardRelationNotExcluded;
                        }
                    }
                }
                break;
        }
    }

    /*
     * If we made at least one token via "Create All Tokens"
     * then assign the first to the "Create another" shortcut.
     */
    if (cardRelation != nullptr && cardRelation->getCanCreateAnother()) {
        CardInfoPtr cardInfo = CardDatabaseManager::query()->getCardInfo(cardRelation->getName());
        setLastToken(cardInfo);
    }
}

void PlayerActions::actRequestCreateRelatedFromRelationDialog(const CardItem *sourceCard,
                                                              const CardRelation *cardRelation)
{
    emit requestCreateRelatedFromRelationDialog(sourceCard, cardRelation);
}

bool PlayerActions::createRelatedFromRelation(const CardItem *sourceCard,
                                              const CardRelation *cardRelation,
                                              int variableCount)
{
    if (sourceCard == nullptr || cardRelation == nullptr) {
        return false;
    }

    const QString dbName = cardRelation->getName();
    const bool persistent = cardRelation->getIsPersistent();
    const bool faceDown = cardRelation->getIsFaceDown();

    // Variable relations always use DoesNotAttach, regardless of the count the user
    // entered.
    if (cardRelation->getIsVariable()) {
        if (variableCount <= 0) {
            return false;
        }
        for (int i = 0; i < variableCount; ++i) {
            createCard(sourceCard, dbName, CardRelationType::DoesNotAttach, persistent, faceDown);
        }
        return true;
    }

    const int count = cardRelation->getDefaultCount();

    if (count > 1) {
        for (int i = 0; i < count; ++i) {
            createCard(sourceCard, dbName, CardRelationType::DoesNotAttach, persistent, faceDown);
        }
        return true;
    }

    CardRelationType attachType;
    // do not attempt to attach to another player's cards, this causes the card to attempt to attach to the same
    // cardid on the local player's field instead, which is an entirely different card!
    if (player->getPlayerInfo()->getLocalOrJudge()) {
        attachType = cardRelation->getAttachType();
    } else {
        attachType = CardRelationType::DoesNotAttach;
    }

    // move card onto table first if attaching from some other zone
    // we only do this for AttachTo because cross-zone TransformInto is already handled server-side
    if (attachType == CardRelationType::AttachTo && sourceCard->getZone()->getName() != ZoneNames::TABLE) {
        playCardToTable(sourceCard, false);
    }

    createCard(sourceCard, dbName, attachType, persistent, faceDown);
    return true;
}

void PlayerActions::onRelatedCardCreated(const CardItem *sourceCard, const CardRelation *cardRelation)
{
    if (sourceCard == nullptr || cardRelation == nullptr) {
        return;
    }

    /*
     * If we make a token via "Token: TokenName"
     * then let's allow it to be created via "create another token"
     */
    if (!cardRelation->getCanCreateAnother()) {
        return;
    }

    ExactCard relatedCard =
        CardDatabaseManager::query()->getCardFromSameSet(cardRelation->getName(), sourceCard->getCard().getPrinting());

    setLastToken(relatedCard.getCardPtr());
}

void PlayerActions::createCard(const CardItem *sourceCard,
                               const QString &dbCardName,
                               CardRelationType attachType,
                               bool persistent,
                               bool faceDown)
{
    CardInfoPtr cardInfo = CardDatabaseManager::query()->getCardInfo(dbCardName);

    if (cardInfo == nullptr || sourceCard == nullptr) {
        return;
    }

    QPoint gridPoint = QPoint(-1, TableZone::tableRowToGridY(cardInfo->getUiAttributes().tableRow));

    // create the token for the related card
    Command_CreateToken cmd;
    cmd.set_zone(ZoneNames::TABLE);
    cmd.set_card_name(cardInfo->getName().toStdString());
    switch (cardInfo->getColors().size()) {
        case 0:
            cmd.set_color("");
            break;
        case 1:
            cmd.set_color("m");
            break;
        default:
            cmd.set_color(cardInfo->getColors().left(1).toLower().toStdString());
            break;
    }

    cmd.set_pt(cardInfo->getPowTough().toStdString());
    cmd.set_keywords(QStringList(CardKeywords::parse(*cardInfo).values()).join(QStringLiteral(",")).toStdString());
    cmd.set_static_abilities(StaticAbilities::serialize(StaticAbilities::parse(*cardInfo)).toStdString());
    if (SettingsCache::instance().getAnnotateTokens()) {
        cmd.set_annotation(cardInfo->getText().toStdString());
    } else {
        cmd.set_annotation("");
    }
    cmd.set_destroy_on_zone_change(!persistent);
    cmd.set_x(gridPoint.x());
    cmd.set_y(gridPoint.y());
    cmd.set_face_down(faceDown);

    ExactCard relatedCard =
        CardDatabaseManager::query()->getCardFromSameSet(cardInfo->getName(), sourceCard->getCard().getPrinting());

    switch (attachType) {
        case CardRelationType::DoesNotAttach:
            cmd.set_target_zone(ZoneNames::TABLE);
            cmd.set_card_provider_id(relatedCard.getPrinting().getUuid().toStdString());
            break;

        case CardRelationType::AttachTo:
            cmd.set_target_zone(ZoneNames::TABLE); // We currently only support creating tokens on the table
            cmd.set_card_provider_id(relatedCard.getPrinting().getUuid().toStdString());
            cmd.set_target_card_id(sourceCard->getId());
            cmd.set_target_mode(Command_CreateToken::ATTACH_TO);
            break;

        case CardRelationType::TransformInto:
            // allow cards to directly transform on stack
            cmd.set_zone(sourceCard->getZone()->getName() == ZoneNames::STACK ? ZoneNames::STACK : ZoneNames::TABLE);
            // Transform card zone changes are handled server-side
            cmd.set_target_zone(sourceCard->getZone()->getName().toStdString());
            cmd.set_target_card_id(sourceCard->getId());
            cmd.set_target_mode(Command_CreateToken::TRANSFORM_INTO);
            cmd.set_card_provider_id(sourceCard->getProviderId().toStdString());
            break;
    }

    sendGameCommand(cmd);
}

void PlayerActions::actSayMessage()
{
    auto *a = qobject_cast<QAction *>(sender());
    Command_GameSay cmd;
    cmd.set_message(a->text().toStdString());
    sendGameCommand(cmd);
}

void PlayerActions::actRequestMoveCardXCardsFromTopDialog()
{
    int deckSize = player->getDeckZone()->getCards().size() + 1; // add the card to move to the deck

    emit requestMoveCardXCardsFromTopDialog(defaultNumberTopCardsToPlaceBelow, deckSize);
}

void PlayerActions::actMoveCardXCardsFromTop(QList<CardItem *> selectedCards, int number)
{
    defaultNumberTopCardsToPlaceBelow = number;

    if (selectedCards.isEmpty()) {
        return;
    }

    QList<const ::google::protobuf::Message *> commandList;
    ListOfCardsToMove idList;
    for (const auto &i : selectedCards) {
        idList.add_card()->set_card_id(i->getId());
    }

    int startPlayerId = selectedCards[0]->getZone()->getPlayer()->getPlayerInfo()->getId();
    QString startZone = selectedCards[0]->getZone()->getName();

    auto *cmd = new Command_MoveCard;
    cmd->set_start_player_id(startPlayerId);
    cmd->set_start_zone(startZone.toStdString());
    cmd->mutable_cards_to_move()->CopyFrom(idList);
    cmd->set_target_player_id(player->getPlayerInfo()->getId());
    cmd->set_target_zone(ZoneNames::DECK);
    cmd->set_x(number);
    cmd->set_y(0);
    commandList.append(cmd);

    if (player->getPlayerInfo()->local) {
        sendGameCommand(prepareGameCommand(commandList));
    } else {
        player->getGame()->getGameEventHandler()->sendGameCommand(prepareGameCommand(commandList));
    }
}

void PlayerActions::actIncPT(QList<CardItem *> selectedCards, int deltaP, int deltaT)
{
    int playerid = player->getPlayerInfo()->getId();

    QList<const ::google::protobuf::Message *> commandList;
    for (auto card : selectedCards) {
        QString pt = card->getPT();
        const auto ptList = CardItem::parsePT(pt);
        QString newpt;
        if (ptList.isEmpty()) {
            newpt = QString::number(deltaP) + (deltaT ? "/" + QString::number(deltaT) : "");
        } else if (ptList.size() == 1) {
            newpt = QString::number(ptList.at(0).toInt() + deltaP) + (deltaT ? "/" + QString::number(deltaT) : "");
        } else {
            newpt =
                QString::number(ptList.at(0).toInt() + deltaP) + "/" + QString::number(ptList.at(1).toInt() + deltaT);
        }

        auto *cmd = new Command_SetCardAttr;
        cmd->set_zone(card->getZone()->getName().toStdString());
        cmd->set_card_id(card->getId());
        cmd->set_attribute(AttrPT);
        cmd->set_attr_value(newpt.toStdString());
        commandList.append(cmd);

        if (player->getPlayerInfo()->getLocal()) {
            playerid = card->getZone()->getPlayer()->getPlayerInfo()->getId();
        }
    }

    player->getGame()->getGameEventHandler()->sendGameCommand(prepareGameCommand(commandList), playerid);
}

void PlayerActions::actResetPT(QList<CardItem *> selectedCards)
{
    int playerid = player->getPlayerInfo()->getId();
    QList<const ::google::protobuf::Message *> commandList;
    for (auto card : selectedCards) {
        QString ptString;
        if (!card->getFaceDown()) { // leave the pt empty if the card is face down
            ExactCard ec = card->getCard();
            if (ec) {
                ptString = ec.getInfo().getPowTough();
            }
        }
        if (ptString == card->getPT()) {
            continue;
        }
        QString zoneName = card->getZone()->getName();
        auto *cmd = new Command_SetCardAttr;
        cmd->set_zone(zoneName.toStdString());
        cmd->set_card_id(card->getId());
        cmd->set_attribute(AttrPT);
        cmd->set_attr_value(ptString.toStdString());
        commandList.append(cmd);

        if (player->getPlayerInfo()->getLocal()) {
            playerid = card->getZone()->getPlayer()->getPlayerInfo()->getId();
        }
    }

    if (!commandList.empty()) {
        player->getGame()->getGameEventHandler()->sendGameCommand(prepareGameCommand(commandList), playerid);
    }
}

void PlayerActions::actRequestSetPTDialog(QList<CardItem *> selectedCards)
{
    QString oldPT;

    for (auto card : selectedCards) {
        if (!card->getPT().isEmpty()) {
            oldPT = card->getPT();
        }
    }

    emit requestSetPTDialog(oldPT);
}

void PlayerActions::actSetPT(QList<CardItem *> selectedCards, const QString &pt)
{
    int playerid = player->getPlayerInfo()->getId();

    const auto ptList = CardItem::parsePT(pt);
    bool empty = ptList.isEmpty();

    QList<const ::google::protobuf::Message *> commandList;
    for (auto card : selectedCards) {
        auto *cmd = new Command_SetCardAttr;
        QString newpt = QString();
        if (!empty) {
            const auto oldpt = CardItem::parsePT(card->getPT());
            int ptIter = 0;
            for (const auto &_item : ptList) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
                if (_item.typeId() == QMetaType::Type::Int) {
#else
                if (_item.type() == QVariant::Int) {
#endif
                    int oldItem = ptIter < oldpt.size() ? oldpt.at(ptIter).toInt() : 0;
                    newpt += '/' + QString::number(oldItem + _item.toInt());
                } else {
                    newpt += '/' + _item.toString();
                }
                ++ptIter;
            }
            newpt = newpt.mid(1);
        }

        cmd->set_zone(card->getZone()->getName().toStdString());
        cmd->set_card_id(card->getId());
        cmd->set_attribute(AttrPT);
        cmd->set_attr_value(newpt.toStdString());
        commandList.append(cmd);

        if (player->getPlayerInfo()->local) {
            playerid = card->getZone()->getPlayer()->getPlayerInfo()->getId();
        }
    }

    player->getGame()->getGameEventHandler()->sendGameCommand(prepareGameCommand(commandList), playerid);
}

void PlayerActions::actDrawArrow()
{
    auto *card = player->getGame()->getActiveCard();
    if (card) {
        card->drawArrow(Qt::red);
    }
}

void PlayerActions::actIncP(QList<CardItem *> selectedCards)
{
    actIncPT(selectedCards, 1, 0);
}

void PlayerActions::actDecP(QList<CardItem *> selectedCards)
{
    actIncPT(selectedCards, -1, 0);
}

void PlayerActions::actIncT(QList<CardItem *> selectedCards)
{
    actIncPT(selectedCards, 0, 1);
}

void PlayerActions::actDecT(QList<CardItem *> selectedCards)
{
    actIncPT(selectedCards, 0, -1);
}

void PlayerActions::actIncPT(QList<CardItem *> selectedCards)
{
    actIncPT(selectedCards, 1, 1);
}

void PlayerActions::actDecPT(QList<CardItem *> selectedCards)
{
    actIncPT(selectedCards, -1, -1);
}

void PlayerActions::actFlowP(QList<CardItem *> selectedCards)
{
    actIncPT(selectedCards, 1, -1);
}

void PlayerActions::actFlowT(QList<CardItem *> selectedCards)
{
    actIncPT(selectedCards, -1, 1);
}

void PlayerActions::actReduceLifeByPower(QList<CardItem *> selectedCards)
{
    // find life counter
    auto lifeCounter = player->getLifeCounter();
    if (!lifeCounter) {
        return;
    }

    // calculate total power;
    int total = 0;
    for (auto card : selectedCards) {
        QVariantList parsed = CardItem::parsePT(card->getPT());
        if (!parsed.isEmpty()) {
            int power = parsed.first().toInt(); // toInt will default to 0 if it's not an int
            total += qMax(power, 0);
        }
    }

    // send cmd
    Command_IncCounter cmd;
    cmd.set_counter_id(lifeCounter->getId());
    cmd.set_delta(-total);
    sendGameCommand(prepareGameCommand(cmd));
}

void AnnotationDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return && event->modifiers() & Qt::ControlModifier) {
        event->accept();
        accept();
        return;
    }
    QInputDialog::keyPressEvent(event);
}

void PlayerActions::actRequestSetAnnotationDialog(QList<CardItem *> selectedCards)
{
    QString oldAnnotation;
    for (auto card : selectedCards) {
        if (!card->getAnnotation().isEmpty()) {
            oldAnnotation = card->getAnnotation();
        }
    }

    emit requestSetAnnotationDialog(oldAnnotation);
}

void PlayerActions::actSetAnnotation(QList<CardItem *> selectedCards, const QString &annotation)
{
    QList<const ::google::protobuf::Message *> commandList;
    for (auto card : selectedCards) {
        auto *cmd = new Command_SetCardAttr;
        cmd->set_zone(card->getZone()->getName().toStdString());
        cmd->set_card_id(card->getId());
        cmd->set_attribute(AttrAnnotation);
        cmd->set_attr_value(annotation.toStdString());
        commandList.append(cmd);
    }
    sendGameCommand(prepareGameCommand(commandList));
}

void PlayerActions::actAttach()
{
    auto *card = player->getGame()->getActiveCard();
    if (!card) {
        return;
    }

    card->drawAttachArrow();
}

void PlayerActions::actUnattach(QList<CardItem *> selectedCards)
{
    QList<const ::google::protobuf::Message *> commandList;
    for (auto card : selectedCards) {
        if (!card->getAttachedTo()) {
            continue;
        }

        auto *cmd = new Command_AttachCard;
        cmd->set_start_zone(card->getZone()->getName().toStdString());
        cmd->set_card_id(card->getId());
        commandList.append(cmd);
    }
    sendGameCommand(prepareGameCommand(commandList));
}

void PlayerActions::actAddCardCounter(QList<CardItem *> selectedCards, int counterId)
{
    offsetCardCounter(selectedCards, counterId, 1);
}

void PlayerActions::actRemoveCardCounter(QList<CardItem *> selectedCards, int counterId)
{
    offsetCardCounter(selectedCards, counterId, -1);
}

void PlayerActions::offsetCardCounter(QList<CardItem *> selectedCards, int counterId, int offset)
{
    QList<const ::google::protobuf::Message *> commandList;
    for (auto card : selectedCards) {
        int oldValue = card->getCounters().value(counterId, 0);

        // Overflow-safe clamp to the server-enforced range [0, MAX_COUNTER_VALUE];
        // a result differing from oldValue also corrects an out-of-range cached value.
        // Callers only ever pass offset == ±1 (actAddCardCounter / actRemoveCardCounter).
        // This client-side clamp is a defense-in-depth UX check, consistent with
        // actSetCardCounter and actIncrementAllCardCounters; the server remains the
        // authoritative enforcer of the bounds.
        int newValue = addClamped(oldValue, offset, 0, MAX_COUNTER_VALUE);
        if (newValue != oldValue) {
            auto *cmd = new Command_SetCardCounter;
            cmd->set_zone(card->getZone()->getName().toStdString());
            cmd->set_card_id(card->getId());
            cmd->set_counter_id(counterId);
            cmd->set_counter_value(newValue);
            commandList.append(cmd);
        }
    }

    sendGameCommand(prepareGameCommand(commandList));
}

void PlayerActions::actRequestSetCardCounterDialog(QList<CardItem *> selectedCards, int counterId)
{
    // If a single card is selected, we show the old value in the dialog. Otherwise, we show "x"
    QString oldValueForDlg = "x";
    if (selectedCards.size() == 1) {
        auto *card = selectedCards.first();
        oldValueForDlg = QString::number(card->getCounters().value(counterId, 0));
    }

    emit requestSetCardCounterDialog(counterId, oldValueForDlg);
}

void PlayerActions::actSetCardCounter(QList<CardItem *> selectedCards, int counterId, const QString &counterValue)
{
    QList<const ::google::protobuf::Message *> commandList;
    for (auto card : selectedCards) {
        int oldValue = card->getCounters().value(counterId, 0);
        Expression exp(oldValue);
        double parsed = exp.parse(counterValue);
        // Clamp in double precision first to avoid UB, then cast
        int number = static_cast<int>(qBound(0.0, parsed, static_cast<double>(MAX_COUNTER_VALUE)));

        auto *cmd = new Command_SetCardCounter;
        cmd->set_zone(card->getZone()->getName().toStdString());
        cmd->set_card_id(card->getId());
        cmd->set_counter_id(counterId);
        cmd->set_counter_value(number);
        commandList.append(cmd);
    }

    sendGameCommand(prepareGameCommand(commandList));
}

void PlayerActions::actIncrementAllCardCounters(QList<CardItem *> cardsToUpdate)
{
    if (cardsToUpdate.isEmpty()) {
        // If no cards selected, update all cards on table
        cardsToUpdate = static_cast<QList<CardItem *>>(player->getTableZone()->getCards());
    }

    QList<const ::google::protobuf::Message *> commandList;

    for (const auto *card : cardsToUpdate) {
        const auto &cardCounters = card->getCounters();

        QMapIterator<int, int> counterIterator(cardCounters);
        while (counterIterator.hasNext()) {
            counterIterator.next();
            int counterId = counterIterator.key();
            int currentValue = counterIterator.value();
            if (currentValue >= MAX_COUNTER_VALUE) {
                continue;
            }

            auto cmd = std::make_unique<Command_SetCardCounter>();
            cmd->set_zone(card->getZone()->getName().toStdString());
            cmd->set_card_id(card->getId());
            cmd->set_counter_id(counterId);
            cmd->set_counter_value(currentValue + 1);
            commandList.append(cmd.release());
        }
    }

    if (!commandList.isEmpty()) {
        sendGameCommand(prepareGameCommand(commandList));
    }
}

// Maps a mana symbol letter ("W"/"U"/"B"/"R"/"G"/"C") to the matching mana-pool counter's name, per
// Server_Player::setupZones()'s naming (colorless mana is named "x" there, not "c" -- everything
// else is just the lowercased symbol letter).
static QString manaCounterNameForSymbol(const QString &manaSymbol)
{
    if (manaSymbol == QLatin1String("C")) {
        return QStringLiteral("x");
    }
    return manaSymbol.toLower();
}

// Flattens a card's recognized mana abilities (design doc §3 Phase 7 "Increment 2") into the set
// of individually selectable (color, amount) options a player could pick when tapping it: a
// single fixed-color ability contributes one option; a choice ability (or a card with more than
// one qualifying ability line) contributes one option per color. Only meaningful for an untapped
// table-zone card -- anything else (wrong zone, already tapped, no card data) returns empty.
static QList<ManaTapOption> manaTapOptionsForCard(const CardItem *card)
{
    if (!card || card->getTapped() || !card->getZone() || card->getZone()->getName() != ZoneNames::TABLE) {
        return {};
    }

    ExactCard exactCard = card->getCard();
    if (!exactCard) {
        return {};
    }

    QList<ManaTapOption> options;
    for (const auto &ability : ManaAbilities::parse(exactCard.getInfo())) {
        for (const QString &symbol : ability.symbolOptions) {
            QString symbols;
            for (int i = 0; i < ability.amount; ++i) {
                symbols += QStringLiteral("{%1}").arg(symbol);
            }
            options.append(ManaTapOption{QObject::tr("Add %1").arg(symbols), symbol, ability.amount});
        }
    }
    return options;
}

// Stage 1 of a real card-ability execution engine (see COMMANDER_IMPLEMENTATION_STATUS.md's Phase
// 7 section for the staged roadmap): a card's simplest self-targeted "{T}: <effect>." activated
// ability, if it has exactly one. Unlike mana abilities, Stage 1 effects are never ambiguous by
// construction (no choice dialog needed) -- a card with more than one qualifying line is left
// unhandled here rather than guessed at, same conservatism as the parser itself. Returns the whole
// ActivatedAbility (not just its effect) since Phase 7 Stage 4 also needs the parsed ManaCost.
static std::optional<ActivatedAbility> nonManaActivatedAbilityForCard(const CardItem *card)
{
    if (!card || card->getTapped() || !card->getZone() || card->getZone()->getName() != ZoneNames::TABLE) {
        return std::nullopt;
    }

    ExactCard exactCard = card->getCard();
    if (!exactCard) {
        return std::nullopt;
    }

    const QList<ActivatedAbility> abilities = ActivatedAbilities::parse(exactCard.getInfo());
    if (abilities.size() != 1) {
        return std::nullopt;
    }
    return abilities.first();
}

// Phase 7 Stage 4: the local player's current mana pool, keyed by the same w/u/b/r/g/x counter
// names as RulesEngine::manaCounterNames() / ManaCost::coloredPips, read from the already-mirrored
// client-side counter state (same source actApplyTap()'s mana-ability handling already reads).
static QMap<QString, int> currentManaPool(PlayerLogic *player)
{
    QMap<QString, int> pool;
    if (!player) {
        return pool;
    }
    const QMap<int, CounterState *> counters = player->getCounters();
    for (auto it = counters.constBegin(); it != counters.constEnd(); ++it) {
        pool.insert(it.value()->getName(), it.value()->getValue());
    }
    return pool;
}

// Phase 7 Stage 4 (extended by phase6-mana.md's hybrid/Phyrexian/X addendum): a "{2}{R}"-style
// display string for a ManaCost, used only in the "can't afford" dialog. {X} symbols first (as
// real cards print them), then generic, then colored pips in RulesEngine::manaCounterNames() order
// (matching the deterministic payment order planManaPayment() itself uses), then hybrid/Phyrexian
// pips in parse order.
static QString counterNameToSymbol(const QString &counterName)
{
    return counterName == QLatin1String("x") ? QStringLiteral("C") : counterName.toUpper();
}

static QString manaCostDescription(const ManaCost &cost)
{
    QString description;
    for (int i = 0; i < cost.xCount; ++i) {
        description += QStringLiteral("{X}");
    }
    if (cost.generic > 0) {
        description += QStringLiteral("{%1}").arg(cost.generic);
    }
    for (const QString &counterName : Rules::RulesEngine::manaCounterNames()) {
        const int count = cost.coloredPips.value(counterName, 0);
        for (int i = 0; i < count; ++i) {
            description += QStringLiteral("{%1}").arg(counterNameToSymbol(counterName));
        }
    }
    for (const auto &hybrid : cost.hybridPips) {
        description +=
            QStringLiteral("{%1/%2}").arg(counterNameToSymbol(hybrid.first), counterNameToSymbol(hybrid.second));
    }
    for (const QString &color : cost.phyrexianPips) {
        description += QStringLiteral("{%1/P}").arg(counterNameToSymbol(color));
    }
    return description;
}

// Phase 7 Stage 4: appends one negative Command_IncCounter per (counterName, amount) entry in
// @p payment -- the exact deduction plan RulesEngine::planManaPayment() already computed -- to
// @p commandList, looking up each counter's id by name the same way actApplyTap()'s own
// appendManaIncrement lambda does for (positive) mana-ability production.
static void appendManaPaymentCommands(QList<const ::google::protobuf::Message *> &commandList,
                                      PlayerLogic *player,
                                      const QMap<QString, int> &payment)
{
    if (!player || payment.isEmpty()) {
        return;
    }
    const QMap<int, CounterState *> counters = player->getCounters();
    for (auto paymentIt = payment.constBegin(); paymentIt != payment.constEnd(); ++paymentIt) {
        if (paymentIt.value() <= 0) {
            continue;
        }
        for (auto counterIt = counters.constBegin(); counterIt != counters.constEnd(); ++counterIt) {
            if (counterIt.value()->getName() == paymentIt.key()) {
                auto *counterCmd = new Command_IncCounter;
                counterCmd->set_counter_id(counterIt.key());
                counterCmd->set_delta(-paymentIt.value());
                commandList.append(counterCmd);
                break;
            }
        }
    }
}

// Real spell casting from hand (doc/commander-status/phase6-mana.md): the shared gate every "a
// card left hand" call site funnels through -- PlayerActions::playCard() (click/double-click-play
// and arrow-drag-from-hand), TableZone::handleDropEventByGrid(), and StackZone::handleDropEvent()
// (both direct drag-and-drop). See gateManaCostForHandPlay()'s own doc comment in player_actions.h
// for the return-value contract.
bool PlayerActions::gateManaCostForHandPlay(const CardItem *card,
                                            bool faceDown,
                                            QList<const ::google::protobuf::Message *> &extraCommands)
{
    if (!card || faceDown || !card->getZone() || card->getZone()->getName() != ZoneNames::HAND) {
        return true; // scoped strictly to casting from hand -- see the header doc for what's excluded
    }

    ExactCard exactCard = card->getCard();
    if (!exactCard) {
        return true;
    }

    const CardInfo &info = exactCard.getInfo();
    if (info.getMainCardType() == QLatin1String("Land")) {
        return true; // land drops are free, rule 305.1
    }

    const std::optional<ManaCost> parsedCost = SpellManaCost::parse(info);
    if (!parsedCost || parsedCost->isFree()) {
        // Unparseable (monocolored hybrid, snow, split cost) or genuinely free -- same
        // conservative "don't guess" fallback as every other unrecognized shape in this fork.
        return true;
    }

    const QMap<QString, int> pool = currentManaPool(player);

    // Phase6-mana.md's hybrid/Phyrexian/X addendum: resolve those components (a real choice, or
    // for {X} an announcement) into a plain payable ManaCost + separate life cost *before* running
    // the pre-existing affordability/generic-choice flow below unchanged against the result.
    ManaCost cost = *parsedCost;
    int lifeCost = 0;
    if (!cost.hybridPips.isEmpty() || !cost.phyrexianPips.isEmpty() || cost.xCount > 0) {
        const Rules::RulesEngine::ManaCostChoices choices = Rules::RulesEngine::planManaCostChoices(cost, pool);

        QList<QString> hybridColorChoices;
        for (const auto &hybrid : choices.hybridChoices) {
            hybridColorChoices.append(hybrid.defaultColor);
        }
        QList<bool> phyrexianPayLifeChoices;
        for (const auto &phyrexian : choices.phyrexianChoices) {
            phyrexianPayLifeChoices.append(phyrexian.defaultPayLife);
        }
        int xValue = 0;

        DlgChooseVariableManaCost variableDialog(nullptr, cost, choices, pool);
        if (variableDialog.hasAnythingToAsk()) {
            if (variableDialog.exec() != QDialog::Accepted) {
                return false; // atomic cancel -- nothing sent
            }
            xValue = variableDialog.chosenXValue();
            hybridColorChoices = variableDialog.chosenHybridColors();
            phyrexianPayLifeChoices = variableDialog.chosenPhyrexianPayLife();
        }

        const Rules::RulesEngine::ResolvedManaCost resolved =
            Rules::RulesEngine::resolveManaCost(cost, xValue, hybridColorChoices, phyrexianPayLifeChoices);
        cost = resolved.cost;
        lifeCost = resolved.lifeCost;
    }

    const std::optional<QMap<QString, int>> plan = Rules::RulesEngine::planManaPayment(cost, pool);
    if (!plan) {
        QMessageBox::information(nullptr, tr("Cannot Cast"),
                                 tr("Not enough mana to cast %1 (%2).").arg(info.getName(), manaCostDescription(cost)));
        return false;
    }

    QMap<QString, int> payment = *plan;
    if (Rules::RulesEngine::isGenericPaymentAmbiguous(cost, pool)) {
        const std::optional<QMap<QString, int>> remaining =
            Rules::RulesEngine::remainingPoolAfterColoredPips(cost, pool);
        QMap<QString, int> prefill;
        for (auto it = payment.constBegin(); it != payment.constEnd(); ++it) {
            const int genericPortion = it.value() - cost.coloredPips.value(it.key(), 0);
            if (genericPortion > 0) {
                prefill[it.key()] = genericPortion;
            }
        }

        DlgChooseGenericManaPayment dialog(nullptr, cost, remaining.value_or(QMap<QString, int>{}), prefill);
        if (dialog.exec() != QDialog::Accepted) {
            return false; // atomic cancel -- nothing sent
        }
        const std::optional<QMap<QString, int>> chosenPlan =
            Rules::RulesEngine::planManaPaymentWithGenericChoice(cost, pool, dialog.chosenGenericSplit());
        if (!chosenPlan) {
            return false; // the dialog only allows valid input -- defensive, shouldn't happen
        }
        payment = *chosenPlan;
    }

    appendManaPaymentCommands(extraCommands, player, payment);
    if (lifeCost > 0) {
        // Life is just another named per-player counter (Server_Player::setupZones()) -- reuse the
        // same counter-lookup-by-name helper rather than a bespoke life-deduction code path.
        appendManaPaymentCommands(extraCommands, player, {{QStringLiteral("life"), lifeCost}});
    }
    return true;
}

QList<ManaTapChoice> PlayerActions::computeManaTapChoices(const QList<CardItem *> &cardList) const
{
    QList<ManaTapChoice> choices;
    for (const auto &card : cardList) {
        const QList<ManaTapOption> options = manaTapOptionsForCard(card);
        if (options.size() > 1) {
            choices.append(ManaTapChoice{card, card->getName(), options});
        }
    }
    return choices;
}

void PlayerActions::actApplyTap(QList<CardItem *> cardList, QMap<const CardItem *, ManaTapOption> chosenManaOptions)
{
    QList<const ::google::protobuf::Message *> commandList;
    const QMap<int, CounterState *> counters = player->getCounters();

    auto appendManaIncrement = [&](const QString &symbol, int amount) {
        const QString counterName = manaCounterNameForSymbol(symbol);
        for (auto it = counters.constBegin(); it != counters.constEnd(); ++it) {
            if (it.value()->getName() == counterName) {
                auto *counterCmd = new Command_IncCounter;
                counterCmd->set_counter_id(it.key());
                counterCmd->set_delta(amount);
                commandList.append(counterCmd);
                break;
            }
        }
    };

    for (const auto &card : cardList) {
        if (!card || !card->getZone()) {
            continue;
        }

        const bool wasTapped = card->getTapped();
        auto *cmd = new Command_SetCardAttr;
        cmd->set_zone(card->getZone()->getName().toStdString());
        cmd->set_card_id(card->getId());
        cmd->set_attribute(AttrTapped);
        cmd->set_attr_value(std::to_string(1 - static_cast<int>(wasTapped)));
        commandList.append(cmd);

        if (wasTapped) {
            continue; // untapping -- never adds mana
        }

        auto chosenIt = chosenManaOptions.constFind(card);
        if (chosenIt != chosenManaOptions.constEnd()) {
            appendManaIncrement(chosenIt.value().symbol, chosenIt.value().amount);
            continue;
        }

        const QList<ManaTapOption> options = manaTapOptionsForCard(card);
        if (options.size() == 1) {
            appendManaIncrement(options.first().symbol, options.first().amount);
        }
        // options.size() > 1 with no entry in chosenManaOptions means the choice dialog was
        // skipped somehow -- don't guess, just tap without adding mana.

        // Phase 7 Stage 3: a recognized non-mana "{T}: <effect>." ability no longer resolves on
        // the spot -- it batches a Command_ActivateAbility alongside the tap toggle above, which
        // pushes it onto the server's pending-ability stack instead (see
        // COMMANDER_IMPLEMENTATION_STATUS.md's Phase 7 Stage 3 section). Independent of the mana
        // handling above (different, mutually-exclusive regex whitelists on the same rules text),
        // so both can fire for a card that happens to have one line of each kind.
        if (const std::optional<ActivatedAbility> ability = nonManaActivatedAbilityForCard(card)) {
            // Phase 7 Stage 4: a costed ability is only ever activated via the single-card path
            // in cardMenuAction() (which checks affordability up front and blocks the whole
            // activation -- including this card's tap above -- if it can't be paid). A
            // multi-select batch simply doesn't attempt a costed ability at all, same "don't
            // guess" precedent as an ambiguous mana choice above; the tap already queued above is
            // unaffected.
            if (!ability->cost.isFree()) {
                continue;
            }
            switch (ability->effect.kind) {
                case EffectKind::DrawCards:
                case EffectKind::GainLife:
                case EffectKind::LoseLife: {
                    auto *abilityCmd = new Command_ActivateAbility;
                    abilityCmd->set_effect_kind(static_cast<int>(ability->effect.kind));
                    abilityCmd->set_amount(ability->effect.amount);
                    commandList.append(abilityCmd);
                    break;
                }
                case EffectKind::DealDamage:
                    // Phase 7 Stage 2: targeted effects never reach this switch in practice --
                    // cardMenuAction() intercepts a lone card with a TargetKind::AnyTarget
                    // ability before actApplyTap() is ever called, routing it through
                    // AbilityTargetPicker + actApplyTapWithTarget() instead (see below). Kept as
                    // an explicit no-op case (not a default:) so this switch stays exhaustive.
                    break;
                case EffectKind::AddCounterToSelf:
                    break; // not wired yet -- no parser produces this kind
            }
        }
    }

    if (!commandList.isEmpty()) {
        sendGameCommand(prepareGameCommand(commandList));
    }
}

// Phase 7 Stage 2 (targeting): the resolved-target sibling of actApplyTap(), called only for the
// single-card path AbilityTargetPicker feeds (see cardMenuAction()'s cmTap interception below --
// targeted-ability activation is single-card-selection only, unlike Stage 1's untargeted effects).
// Deliberately does not also apply an unambiguous mana ability the way actApplyTap() does for
// untargeted taps -- no real printed card combines a mana ability with a targeted damage ability
// on the same tap-cost line, so this is left unhandled rather than guessed at, same conservatism
// as every other narrow boundary in this parser/dispatch pair. Phase 7 Stage 4: manaPayment is the
// already-computed, already-affordability-checked deduction plan from cardMenuAction() (empty for a
// free ability) -- this function only ever sends it, never re-checks or re-plans it.
void PlayerActions::actApplyTapWithTarget(CardItem *card,
                                          CardEffect effect,
                                          AbilityTarget target,
                                          const QMap<QString, int> &manaPayment)
{
    if (!card || !card->getZone()) {
        return;
    }

    QList<const ::google::protobuf::Message *> commandList;

    auto *tapCmd = new Command_SetCardAttr;
    tapCmd->set_zone(card->getZone()->getName().toStdString());
    tapCmd->set_card_id(card->getId());
    tapCmd->set_attribute(AttrTapped);
    tapCmd->set_attr_value("1");
    commandList.append(tapCmd);

    appendManaPaymentCommands(commandList, player, manaPayment);

    auto *effectCmd = new Command_ActivateAbility;
    effectCmd->set_effect_kind(static_cast<int>(effect.kind));
    effectCmd->set_amount(effect.amount);
    effectCmd->set_target_player_id(target.targetPlayerId);
    if (!target.isPlayer) {
        effectCmd->set_target_zone(target.targetZone.toStdString());
        effectCmd->set_target_card_id(target.targetCardId);
    }
    commandList.append(effectCmd);

    sendGameCommand(prepareGameCommand(commandList));
}

// Phase 7 Stage 4: the untargeted sibling of actApplyTapWithTarget() -- a single card's costed
// non-mana activated ability, resolved without needing AbilityTargetPicker at all. Called only from
// cardMenuAction()'s single-card interception once affordability has already been checked and the
// payment plan computed; manaPayment is sent as-is, same "already resolved, just send it" contract
// as actApplyTapWithTarget().
void PlayerActions::actApplyTapWithCost(CardItem *card, CardEffect effect, const QMap<QString, int> &manaPayment)
{
    if (!card || !card->getZone()) {
        return;
    }

    QList<const ::google::protobuf::Message *> commandList;

    auto *tapCmd = new Command_SetCardAttr;
    tapCmd->set_zone(card->getZone()->getName().toStdString());
    tapCmd->set_card_id(card->getId());
    tapCmd->set_attribute(AttrTapped);
    tapCmd->set_attr_value("1");
    commandList.append(tapCmd);

    appendManaPaymentCommands(commandList, player, manaPayment);

    auto *abilityCmd = new Command_ActivateAbility;
    abilityCmd->set_effect_kind(static_cast<int>(effect.kind));
    abilityCmd->set_amount(effect.amount);
    commandList.append(abilityCmd);

    sendGameCommand(prepareGameCommand(commandList));
}

// Phase 7 Stage 5 (triggered abilities): called from PlayerEventHandler::eventMoveCard() -- the
// single client-side chokepoint every card move funnels through -- whenever @p card just landed on
// or left the local player's own battlefield in a way that could match an "enters the battlefield"
// or "dies" trigger (see the call site for the exact zone-transition/ownership gating). Unlike
// activated abilities, nothing here is conditional on a player action: any qualifying line found
// fires unconditionally, no tap, no mana, no target -- so this is the simplest of the ability-check
// helpers, a direct parse-then-send with no intermediate dialog or picker.
void PlayerActions::actCheckTrigger(CardItem *card, TriggerKind trigger)
{
    if (!card) {
        return;
    }

    ExactCard exactCard = card->getCard();
    if (!exactCard) {
        return;
    }

    QList<const ::google::protobuf::Message *> commandList;
    for (const auto &ability : TriggeredAbilities::parse(exactCard.getInfo())) {
        if (ability.trigger != trigger) {
            continue;
        }
        auto *abilityCmd = new Command_ActivateAbility;
        abilityCmd->set_effect_kind(static_cast<int>(ability.effect.kind));
        abilityCmd->set_amount(ability.effect.amount);
        commandList.append(abilityCmd);
    }

    if (!commandList.isEmpty()) {
        sendGameCommand(prepareGameCommand(commandList));
    }
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

void PlayerActions::playSelectedCards(QList<CardItem *> selectedCards, const bool faceDown)
{
    // CardIds will get shuffled downwards when cards leave the deck.
    // We need to iterate through the cards in reverse order so cardIds don't get changed out from under us as we play
    // out the cards one-by-one.
    std::sort(selectedCards.begin(), selectedCards.end(),
              [](const auto &card1, const auto &card2) { return card1->getId() > card2->getId(); });

    for (auto &card : selectedCards) {
        if (card && !isUnwritableRevealZone(card->getZone()) && card->getZone()->getName() != ZoneNames::TABLE) {
            playCard(card, faceDown);
        }
    }
}

void PlayerActions::actPlay(QList<CardItem *> selectedCards)
{
    playSelectedCards(selectedCards, false);
}

void PlayerActions::actPlayFacedown(QList<CardItem *> selectedCards)
{
    playSelectedCards(selectedCards, true);
}

void PlayerActions::actHide(QList<CardItem *> selectedCards)
{
    for (const auto &item : selectedCards) {
        auto *card = static_cast<CardItem *>(item);
        if (card && isUnwritableRevealZone(card->getZone())) {
            card->getZone()->removeCard(card);
        }
    }
}

void PlayerActions::actReveal(QList<CardItem *> selectedCards, QAction *action)
{
    const int otherPlayerId = action->data().toInt();

    Command_RevealCards cmd;
    if (otherPlayerId != -1) {
        cmd.set_player_id(otherPlayerId);
    }

    for (auto card : selectedCards) {
        if (!cmd.has_zone_name()) {
            cmd.set_zone_name(card->getZone()->getName().toStdString());
        }
        cmd.add_card_id(card->getId());
    }

    sendGameCommand(cmd);
}

void PlayerActions::actRevealHand(int revealToPlayerId)
{
    Command_RevealCards cmd;
    if (revealToPlayerId != -1) {
        cmd.set_player_id(revealToPlayerId);
    }
    cmd.set_zone_name(ZoneNames::HAND);

    sendGameCommand(cmd);
}

void PlayerActions::actRevealRandomHandCard(int revealToPlayerId)
{
    Command_RevealCards cmd;
    if (revealToPlayerId != -1) {
        cmd.set_player_id(revealToPlayerId);
    }
    cmd.set_zone_name(ZoneNames::HAND);
    cmd.add_card_id(RANDOM_CARD_FROM_ZONE);

    sendGameCommand(cmd);
}

void PlayerActions::actRevealLibrary(int revealToPlayerId)
{
    Command_RevealCards cmd;
    if (revealToPlayerId != -1) {
        cmd.set_player_id(revealToPlayerId);
    }
    cmd.set_zone_name(ZoneNames::DECK);

    sendGameCommand(cmd);
}

void PlayerActions::actLendLibrary(int lendToPlayerId)
{
    Command_RevealCards cmd;
    if (lendToPlayerId != -1) {
        cmd.set_player_id(lendToPlayerId);
    }
    cmd.set_zone_name(ZoneNames::DECK);
    cmd.set_grant_write_access(true);

    sendGameCommand(cmd);
}

void PlayerActions::actRevealTopCards(int revealToPlayerId, int amount)
{
    Command_RevealCards cmd;
    if (revealToPlayerId != -1) {
        cmd.set_player_id(revealToPlayerId);
    }

    cmd.set_zone_name(ZoneNames::DECK);
    cmd.set_top_cards(amount);
    // backward compatibility: servers before #1051 only permits to reveal the first card
    cmd.add_card_id(0);

    sendGameCommand(cmd);
}

void PlayerActions::actRevealRandomGraveyardCard(int revealToPlayerId)
{
    Command_RevealCards cmd;
    if (revealToPlayerId != -1) {
        cmd.set_player_id(revealToPlayerId);
    }
    cmd.set_zone_name(ZoneNames::GRAVE);
    cmd.add_card_id(RANDOM_CARD_FROM_ZONE);
    sendGameCommand(cmd);
}

void PlayerActions::cardMenuAction(QList<CardItem *> selectedCards, CardMenuActionType type)
{
    QList<CardItem *> cardList = selectedCards;

    // Phase 8 Stage 6 (combat, declare-attacker slice): checked ahead of everything else below,
    // since it needs per-card keyword inspection the generic per-card attribute-toggle loop further
    // down (cmDoesntUntap's shape) doesn't provide. Declaring a card as an attacker taps it unless
    // it has Vigilance (rule 508.1f) -- the "keyword abilities can matter mechanically" payoff this
    // stage was picked for; removing a card from combat never untaps it (matches real rules: ceasing
    // to attack isn't itself an untap effect).
    if (type == cmAttacking) {
        QList<const ::google::protobuf::Message *> commandList;
        for (const auto &card : cardList) {
            if (!card || !card->getZone()) {
                continue;
            }

            const bool wasAttacking = card->getAttacking();
            auto *attackCmd = new Command_SetCardAttr;
            attackCmd->set_zone(card->getZone()->getName().toStdString());
            attackCmd->set_card_id(card->getId());
            attackCmd->set_attribute(AttrAttacking);
            attackCmd->set_attr_value(wasAttacking ? "0" : "1");
            commandList.append(attackCmd);

            if (wasAttacking || card->getTapped()) {
                continue; // removing from combat, or already tapped -- nothing else to send
            }

            ExactCard exactCard = card->getCard();
            const bool hasVigilance =
                exactCard && CardKeywords::parse(exactCard.getInfo()).contains(QStringLiteral("Vigilance"));
            if (!hasVigilance) {
                auto *tapCmd = new Command_SetCardAttr;
                tapCmd->set_zone(card->getZone()->getName().toStdString());
                tapCmd->set_card_id(card->getId());
                tapCmd->set_attribute(AttrTapped);
                tapCmd->set_attr_value("1");
                commandList.append(tapCmd);
            }
        }

        if (!commandList.isEmpty()) {
            sendGameCommand(prepareGameCommand(commandList));
        }
        return;
    }

    // Phase 7 Stages 2/4 (targeting / mana cost): checked ahead of everything else below, and only
    // for a single selected card -- multi-select batch activation (each card needing its own
    // independently resolved target and/or mana payment) is real added complexity explicitly
    // deferred, not attempted here (same "left unhandled rather than guessed at" conservatism as
    // the parser's own boundaries; actApplyTap()'s own per-card loop below only ever activates
    // *free* non-mana abilities for a multi-select batch). nonManaActivatedAbilityForCard() already
    // returns std::nullopt for an already-tapped card, so this naturally never fires on a cmUntap
    // click.
    if (type == cmTap && cardList.size() == 1) {
        CardItem *soleCard = cardList.first();
        if (const std::optional<ActivatedAbility> ability = nonManaActivatedAbilityForCard(soleCard)) {
            // Phase 7 Stage 4: affordability is checked once, up front, before anything (tap
            // included) is sent -- a real activation, per rule 601.2h, either pays its full cost
            // or never starts. planManaPayment() returns the exact deduction plan to send; the
            // dialog below explains the shortfall the same way this fork's other advisory
            // messages do (Phase 9 SBA warnings), even though this is otherwise the one place in
            // this fork that actually blocks an action rather than just warning about it.
            QMap<QString, int> payment;
            if (!ability->cost.isFree()) {
                const auto plan = Rules::RulesEngine::planManaPayment(ability->cost, currentManaPool(player));
                if (!plan) {
                    QMessageBox::information(
                        nullptr, tr("Cannot Activate"),
                        tr("Not enough mana to pay this ability's cost (%1).").arg(manaCostDescription(ability->cost)));
                    return;
                }
                payment = *plan;
            }

            if (ability->effect.target == TargetKind::AnyTarget) {
                auto *picker = new AbilityTargetPicker(player, soleCard);
                soleCard->scene()->addItem(picker);
                connect(picker, &AbilityTargetPicker::targetChosen, this,
                        [this, soleCard, effect = ability->effect, payment](const AbilityTarget &target) {
                            actApplyTapWithTarget(soleCard, effect, target, payment);
                        });
                picker->grabMouse();
                return;
            }

            if (!ability->cost.isFree()) {
                actApplyTapWithCost(soleCard, ability->effect, payment);
                return;
            }
            // Free, untargeted ability -- fall through to the existing generic mana-choice /
            // actApplyTap() path below, unchanged from before Stage 4.
        }
    }

    // Leaving both for compatibility with server. Handled ahead of the generic per-card loop
    // below since it operates on the whole selection at once (multi-select tap-for-mana, design
    // doc §3 Phase 7 "Increment 2" follow-up): every card being tapped (not untapped) with a
    // recognized mana ability auto-adds mana to the pool, unless it has more than one option (a
    // choice ability, or multiple ability lines), in which case a dialog asks which to use before
    // anything is sent.
    if (type == cmUntap || type == cmTap) {
        const QList<ManaTapChoice> choices = computeManaTapChoices(cardList);
        if (choices.isEmpty()) {
            actApplyTap(cardList, {});
        } else {
            emit requestManaAbilityChoiceDialog(cardList, choices);
        }
        return;
    }

    QList<const ::google::protobuf::Message *> commandList;
    if (type <= cmClone) {
        for (const auto &card : cardList) {
            switch (type) {
                case cmDoesntUntap: {
                    auto *cmd = new Command_SetCardAttr;
                    cmd->set_zone(card->getZone()->getName().toStdString());
                    cmd->set_card_id(card->getId());
                    cmd->set_attribute(AttrDoesntUntap);
                    cmd->set_attr_value(card->getDoesntUntap() ? "0" : "1");
                    commandList.append(cmd);
                    break;
                }
                case cmFlip: {
                    auto *cmd = new Command_FlipCard;
                    cmd->set_zone(card->getZone()->getName().toStdString());
                    cmd->set_card_id(card->getId());
                    cmd->set_face_down(!card->getFaceDown());
                    if (card->getFaceDown()) {
                        ExactCard ec = card->getCard();
                        if (ec) {
                            cmd->set_pt(ec.getInfo().getPowTough().toStdString());
                            cmd->set_keywords(QStringList(CardKeywords::parse(ec.getInfo()).values())
                                                  .join(QStringLiteral(","))
                                                  .toStdString());
                            cmd->set_static_abilities(
                                StaticAbilities::serialize(StaticAbilities::parse(ec.getInfo())).toStdString());
                        }
                    }
                    commandList.append(cmd);
                    break;
                }
                case cmPeek: {
                    auto *cmd = new Command_RevealCards;
                    cmd->set_zone_name(card->getZone()->getName().toStdString());
                    cmd->add_card_id(card->getId());
                    cmd->set_player_id(player->getPlayerInfo()->getId());
                    commandList.append(cmd);
                    break;
                }
                case cmClone: {
                    auto *cmd = new Command_CreateToken;
                    cmd->set_zone(ZoneNames::TABLE);
                    cmd->set_card_name(card->getName().toStdString());
                    cmd->set_card_provider_id(card->getProviderId().toStdString());
                    cmd->set_color(card->getColor().toStdString());
                    cmd->set_pt(card->getPT().toStdString());
                    ExactCard cloneSourceCard = card->getCard();
                    if (cloneSourceCard) {
                        cmd->set_keywords(QStringList(CardKeywords::parse(cloneSourceCard.getInfo()).values())
                                              .join(QStringLiteral(","))
                                              .toStdString());
                        cmd->set_static_abilities(
                            StaticAbilities::serialize(StaticAbilities::parse(cloneSourceCard.getInfo()))
                                .toStdString());
                    }
                    cmd->set_annotation(card->getAnnotation().toStdString());
                    cmd->set_destroy_on_zone_change(true);
                    cmd->set_x(-1);
                    cmd->set_y(card->getGridPoint().y());
                    commandList.append(cmd);
                    break;
                }
                default:
                    break;
            }
        }
    } else {
        CardZoneLogic *zone = cardList[0]->getZone();
        if (!zone) {
            return;
        }

        PlayerLogic *startPlayer = zone->getPlayer();
        if (!startPlayer) {
            return;
        }

        int startPlayerId = startPlayer->getPlayerInfo()->getId();
        QString startZone = zone->getName();

        ListOfCardsToMove idList;
        for (const auto &i : cardList) {
            idList.add_card()->set_card_id(i->getId());
        }

        switch (type) {
            case cmMoveToTopLibrary: {
                auto *cmd = new Command_MoveCard;
                cmd->set_start_player_id(startPlayerId);
                cmd->set_start_zone(startZone.toStdString());
                cmd->mutable_cards_to_move()->CopyFrom(idList);
                cmd->set_target_player_id(player->getPlayerInfo()->getId());
                cmd->set_target_zone(ZoneNames::DECK);
                cmd->set_x(0);
                cmd->set_y(0);

                if (idList.card_size() > 1) {
                    auto *scmd = new Command_Shuffle;
                    scmd->set_zone_name(ZoneNames::DECK);
                    scmd->set_start(0);
                    scmd->set_end(idList.card_size() - 1); // inclusive, the indexed card at end will be shuffled
                    // Server process events backwards, so...
                    commandList.append(scmd);
                }

                commandList.append(cmd);
                break;
            }
            case cmMoveToBottomLibrary: {
                auto *cmd = new Command_MoveCard;
                cmd->set_start_player_id(startPlayerId);
                cmd->set_start_zone(startZone.toStdString());
                cmd->mutable_cards_to_move()->CopyFrom(idList);
                cmd->set_target_player_id(player->getPlayerInfo()->getId());
                cmd->set_target_zone(ZoneNames::DECK);
                cmd->set_x(-1);
                cmd->set_y(0);

                if (idList.card_size() > 1) {
                    auto *scmd = new Command_Shuffle;
                    scmd->set_zone_name(ZoneNames::DECK);
                    scmd->set_start(-idList.card_size());
                    scmd->set_end(-1);
                    // Server process events backwards, so...
                    commandList.append(scmd);
                }

                commandList.append(cmd);
                break;
            }
            case cmMoveToHand: {
                auto *cmd = new Command_MoveCard;
                cmd->set_start_player_id(startPlayerId);
                cmd->set_start_zone(startZone.toStdString());
                cmd->mutable_cards_to_move()->CopyFrom(idList);
                cmd->set_target_player_id(player->getPlayerInfo()->getId());
                cmd->set_target_zone(ZoneNames::HAND);
                cmd->set_x(0);
                cmd->set_y(0);
                commandList.append(cmd);
                break;
            }
            case cmMoveToGraveyard: {
                auto *cmd = new Command_MoveCard;
                cmd->set_start_player_id(startPlayerId);
                cmd->set_start_zone(startZone.toStdString());
                cmd->mutable_cards_to_move()->CopyFrom(idList);
                cmd->set_target_player_id(player->getPlayerInfo()->getId());
                cmd->set_target_zone(ZoneNames::GRAVE);
                cmd->set_x(0);
                cmd->set_y(0);
                commandList.append(cmd);
                break;
            }
            case cmMoveToExile: {
                auto *cmd = new Command_MoveCard;
                cmd->set_start_player_id(startPlayerId);
                cmd->set_start_zone(startZone.toStdString());
                cmd->mutable_cards_to_move()->CopyFrom(idList);
                cmd->set_target_player_id(player->getPlayerInfo()->getId());
                cmd->set_target_zone(ZoneNames::EXILE);
                cmd->set_x(0);
                cmd->set_y(0);
                commandList.append(cmd);
                break;
            }
            case cmMoveToTable: {
                // Each card needs its own command because table row, pt, and cipt vary per card
                for (const auto &card : cardList) {
                    auto *cmd = new Command_MoveCard;
                    cmd->set_start_player_id(startPlayerId);
                    cmd->set_start_zone(startZone.toStdString());
                    cmd->set_target_player_id(player->getPlayerInfo()->getId());
                    cmd->set_target_zone(ZoneNames::TABLE);
                    cmd->set_x(-1);

                    CardToMove *ctm = cmd->mutable_cards_to_move()->add_card();
                    ctm->set_card_id(card->getId());
                    ctm->set_face_down(false);

                    int tableRow = 0;
                    ExactCard exactCard = card->getCard();
                    if (exactCard) {
                        const CardInfo &info = exactCard.getInfo();
                        tableRow = info.getUiAttributes().tableRow;
                        ctm->set_pt(info.getPowTough().toStdString());
                        ctm->set_keywords(
                            QStringList(CardKeywords::parse(info).values()).join(QStringLiteral(",")).toStdString());
                        ctm->set_static_abilities(
                            StaticAbilities::serialize(StaticAbilities::parse(info)).toStdString());
                        ctm->set_tapped(info.getUiAttributes().cipt);
                    }

                    cmd->set_y(TableZone::tableRowToGridY(tableRow));
                    commandList.append(cmd);
                }
                break;
            }
            default:
                break;
        }
    }

    if (player->getPlayerInfo()->getLocal()) {
        sendGameCommand(prepareGameCommand(commandList));
    } else {
        player->getGame()->getGameEventHandler()->sendGameCommand(prepareGameCommand(commandList));
    }
}

// Phase 8 combat automation, Stage A: declares each selected card as an attacker targeting
// @p targetPlayerId, mirroring cmAttacking's vigilance-aware tap logic above -- the one addition
// is also sending AttrAttackTarget, which is what makes the attack automatable in Stage B (unlike
// the bare cmAttacking toggle, which never recorded who was being attacked).
void PlayerActions::actDeclareAttacker(QList<CardItem *> selectedCards, int targetPlayerId)
{
    QList<const ::google::protobuf::Message *> commandList;
    for (const auto &card : selectedCards) {
        if (!card || !card->getZone()) {
            continue;
        }

        const bool wasAttacking = card->getAttacking();

        auto *attackCmd = new Command_SetCardAttr;
        attackCmd->set_zone(card->getZone()->getName().toStdString());
        attackCmd->set_card_id(card->getId());
        attackCmd->set_attribute(AttrAttacking);
        attackCmd->set_attr_value("1");
        commandList.append(attackCmd);

        auto *targetCmd = new Command_SetCardAttr;
        targetCmd->set_zone(card->getZone()->getName().toStdString());
        targetCmd->set_card_id(card->getId());
        targetCmd->set_attribute(AttrAttackTarget);
        targetCmd->set_attr_value(QString::number(targetPlayerId).toStdString());
        commandList.append(targetCmd);

        if (wasAttacking || card->getTapped()) {
            continue; // already attacking (just (re)targeting), or already tapped
        }

        ExactCard exactCard = card->getCard();
        const bool hasVigilance =
            exactCard && CardKeywords::parse(exactCard.getInfo()).contains(QStringLiteral("Vigilance"));
        if (!hasVigilance) {
            auto *tapCmd = new Command_SetCardAttr;
            tapCmd->set_zone(card->getZone()->getName().toStdString());
            tapCmd->set_card_id(card->getId());
            tapCmd->set_attribute(AttrTapped);
            tapCmd->set_attr_value("1");
            commandList.append(tapCmd);
        }
    }

    if (!commandList.isEmpty()) {
        sendGameCommand(prepareGameCommand(commandList));
    }
}

// Phase 8 combat automation, Stage A: removes each selected card from combat, clearing both
// AttrAttacking and AttrAttackTarget (cmAttacking's own toggle-off path only clears the former,
// left untouched above for backward compatibility with its existing keyboard shortcut).
void PlayerActions::actRemoveFromCombat(QList<CardItem *> selectedCards)
{
    QList<const ::google::protobuf::Message *> commandList;
    for (const auto &card : selectedCards) {
        if (!card || !card->getZone() || !card->getAttacking()) {
            continue;
        }

        auto *attackCmd = new Command_SetCardAttr;
        attackCmd->set_zone(card->getZone()->getName().toStdString());
        attackCmd->set_card_id(card->getId());
        attackCmd->set_attribute(AttrAttacking);
        attackCmd->set_attr_value("0");
        commandList.append(attackCmd);

        auto *targetCmd = new Command_SetCardAttr;
        targetCmd->set_zone(card->getZone()->getName().toStdString());
        targetCmd->set_card_id(card->getId());
        targetCmd->set_attribute(AttrAttackTarget);
        targetCmd->set_attr_value("-1");
        commandList.append(targetCmd);
    }

    if (!commandList.isEmpty()) {
        sendGameCommand(prepareGameCommand(commandList));
    }
}

// Phase 8 combat automation, Stage A: declares each selected card as blocking the attacker
// identified by (attackerPlayerId, attackerCardId), packed into AttrBlocking's single string
// value the same way AttrPT already packs "power/toughness" (see card_attributes.proto).
void PlayerActions::actDeclareBlocker(QList<CardItem *> selectedCards, int attackerPlayerId, int attackerCardId)
{
    QList<const ::google::protobuf::Message *> commandList;
    for (const auto &card : selectedCards) {
        if (!card || !card->getZone()) {
            continue;
        }

        auto *cmd = new Command_SetCardAttr;
        cmd->set_zone(card->getZone()->getName().toStdString());
        cmd->set_card_id(card->getId());
        cmd->set_attribute(AttrBlocking);
        cmd->set_attr_value(QStringLiteral("%1:%2").arg(attackerPlayerId).arg(attackerCardId).toStdString());
        commandList.append(cmd);
    }

    if (!commandList.isEmpty()) {
        sendGameCommand(prepareGameCommand(commandList));
    }
}

// Phase 8 combat automation, Stage A: clears a previously declared block.
void PlayerActions::actRemoveBlocker(QList<CardItem *> selectedCards)
{
    QList<const ::google::protobuf::Message *> commandList;
    for (const auto &card : selectedCards) {
        if (!card || !card->getZone() || !card->getBlocking()) {
            continue;
        }

        auto *cmd = new Command_SetCardAttr;
        cmd->set_zone(card->getZone()->getName().toStdString());
        cmd->set_card_id(card->getId());
        cmd->set_attribute(AttrBlocking);
        cmd->set_attr_value("-1:-1");
        commandList.append(cmd);
    }

    if (!commandList.isEmpty()) {
        sendGameCommand(prepareGameCommand(commandList));
    }
}

PendingCommand *PlayerActions::prepareGameCommand(const google::protobuf::Message &cmd)
{

    if (player->getPlayerInfo()->getJudge() && !player->getPlayerInfo()->getLocal()) {
        Command_Judge base;
        GameCommand *c = base.add_game_command();
        base.set_target_id(player->getPlayerInfo()->getId());
        c->GetReflection()->MutableMessage(c, cmd.GetDescriptor()->FindExtensionByName("ext"))->CopyFrom(cmd);
        return player->getGame()->getGameEventHandler()->prepareGameCommand(base);
    } else {
        return player->getGame()->getGameEventHandler()->prepareGameCommand(cmd);
    }
}

PendingCommand *PlayerActions::prepareGameCommand(const QList<const ::google::protobuf::Message *> &cmdList)
{
    if (player->getPlayerInfo()->getJudge() && !player->getPlayerInfo()->getLocal()) {
        Command_Judge base;
        base.set_target_id(player->getPlayerInfo()->getId());
        for (int i = 0; i < cmdList.size(); ++i) {
            GameCommand *c = base.add_game_command();
            c->GetReflection()
                ->MutableMessage(c, cmdList[i]->GetDescriptor()->FindExtensionByName("ext"))
                ->CopyFrom(*cmdList[i]);
            delete cmdList[i];
        }
        return player->getGame()->getGameEventHandler()->prepareGameCommand(base);
    } else {
        return player->getGame()->getGameEventHandler()->prepareGameCommand(cmdList);
    }
}

void PlayerActions::sendGameCommand(const google::protobuf::Message &command)
{
    if (player->getPlayerInfo()->getJudge() && !player->getPlayerInfo()->getLocal()) {
        Command_Judge base;
        GameCommand *c = base.add_game_command();
        base.set_target_id(player->getPlayerInfo()->getId());
        c->GetReflection()->MutableMessage(c, command.GetDescriptor()->FindExtensionByName("ext"))->CopyFrom(command);
        player->getGame()->getGameEventHandler()->sendGameCommand(base, player->getPlayerInfo()->getId());
    } else {
        player->getGame()->getGameEventHandler()->sendGameCommand(command, player->getPlayerInfo()->getId());
    }
}

void PlayerActions::sendGameCommand(PendingCommand *pend)
{
    player->getGame()->getGameEventHandler()->sendGameCommand(pend, player->getPlayerInfo()->getId());
}
