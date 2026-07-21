/**
 * @file player_actions.h
 *  @ingroup GameLogicActions
 * @ingroup GameLogicPlayers
 */
//! \todo Document this file.

#ifndef COCKATRICE_PLAYER_ACTIONS_H
#define COCKATRICE_PLAYER_ACTIONS_H

#include "../../game_graphics/board/card_item.h"
#include "../../game_graphics/dialogs/dlg_create_token.h"
#include "../../game_graphics/dialogs/dlg_move_top_cards_until.h"
#include "../../game_graphics/player/card_menu_action_type.h"
#include "event_processing_options.h"
#include "player_logic.h"

#include <QMenu>
#include <QObject>
#include <libcockatrice/card/ability/card_effects.h>
#include <libcockatrice/card/relation/card_relation_type.h>
#include <libcockatrice/filters/filter_string.h>

namespace google
{
namespace protobuf
{
class Message;
}
} // namespace google

class Command_MoveCard;
class GameEventContext;
class PendingCommand;
class PlayerLogic;

// One selectable option for a card whose mana ability isn't a single unambiguous color -- either
// because it has more than one qualifying ManaAbilities::ManaAbility line, or because its one
// line is itself a player choice among colors (see ManaAbilities::ManaAbility::isChoice()).
struct ManaTapOption
{
    QString label;  // e.g. "Add {W}", shown in the choice dialog
    QString symbol; // "W", "U", "B", "R", "G", or "C"
    int amount = 1;
};

// A single table-zone card, about to be tapped as part of a multi-select Tap action, whose mana
// contribution needs the player to pick one of several options before the tap can be batched
// with a counter increment.
struct ManaTapChoice
{
    const CardItem *card = nullptr;
    QString cardName;
    QList<ManaTapOption> options;
};

// Phase 7 Stage 2 (targeting): a resolved target for a CardEffect with TargetKind::AnyTarget,
// produced by AbilityTargetPicker's board-click targeting interaction. card_id is only unique
// within one player's zone, never globally, so a card target is always addressed by the explicit
// (targetPlayerId, targetZone, targetCardId) triple, never targetCardId alone -- same convention
// Command_ActivateAbility carries over the wire.
struct AbilityTarget
{
    bool isPlayer = true;
    int targetPlayerId = -1;
    QString targetZone;    // empty when isPlayer
    int targetCardId = -1; // -1 when isPlayer
};

class PlayerActions : public QObject
{
    Q_OBJECT

public:
    enum CardsToReveal
    {
        RANDOM_CARD_FROM_ZONE = -2
    };

    explicit PlayerActions(PlayerLogic *player);

    void sendGameCommand(PendingCommand *pend);
    void sendGameCommand(const google::protobuf::Message &command);

    PendingCommand *prepareGameCommand(const ::google::protobuf::Message &cmd);
    PendingCommand *prepareGameCommand(const QList<const ::google::protobuf::Message *> &cmdList);

    void moveOneCardUntil(CardItem *card);
    void stopMoveTopCardsUntil();

    [[nodiscard]] bool isMovingCardsUntil() const
    {
        return movingCardsUntil;
    }

    // Real spell casting from hand (doc/commander-status/phase6-mana.md): gates a single card
    // leaving the HAND zone behind an affordability check on its printed mana cost. Returns true
    // if the caller may proceed -- payment Command_IncCounters, if any, are appended to
    // extraCommands for the caller to send batched alongside its own move/attach command. Returns
    // false if the whole action must be aborted (unaffordable -- a message was already shown --
    // or the player cancelled a color-choice dialog); nothing is sent either way on false, so
    // every caller is free to just `return` on false with no cleanup. A no-op (returns true, never
    // touches extraCommands) for anything other than a face-up, non-land, affordable-cost card
    // actually leaving hand -- see the .cpp for the exact scope (lands, face-down plays, monocolored
    // hybrid, snow, and split-cost spells are deliberately never gated; plain hybrid, Phyrexian, and
    // {X} costs are, per phase6-mana.md's addendum).
    bool gateManaCostForHandPlay(const CardItem *card,
                                 bool faceDown,
                                 QList<const ::google::protobuf::Message *> &extraCommands);

signals:
    void requestViewTopCardsDialog(int defaultNumberTopCards, int deckSize);
    void requestViewBottomCardsDialog(int defaultNumberBottomCards, int deckSize);
    void requestShuffleTopDialog(int defaultNumberTopCards, int maxCards);
    void requestShuffleBottomDialog(int defaultNumberBottomCards, int maxCards);
    void requestMulliganDialog(int startSize, int handSize, int deckSize);
    void requestDrawCardsDialog(int defaultNumberTopCards, int deckSize);
    void requestMoveTopCardsToDialog(int defaultNumberTopCards,
                                     int maxCards,
                                     const QString &targetZone,
                                     const QString &zoneDisplayName,
                                     bool faceDown);
    void requestMoveTopCardsUntilDialog(MoveTopCardsUntilOptions options);
    void requestMoveBottomCardsToDialog(int defaultNumberBottomCards,
                                        int maxCards,
                                        const QString &targetZone,
                                        const QString &zoneDisplayName,
                                        bool faceDown);
    void requestDrawBottomCardsDialog(int defaultNumberBottomCards, int maxCards);
    void requestRollDieDialog();
    void requestCreateTokenDialog(const QStringList &predefinedTokens);
    void requestCreateRelatedFromRelationDialog(const CardItem *sourceCard, const CardRelation *cardRelation);
    void requestMoveCardXCardsFromTopDialog(int defaultNumberTopCardsToPlaceBelow, int deckSize);
    void requestManaAbilityChoiceDialog(QList<CardItem *> cardList, QList<ManaTapChoice> choices);
    void requestSetPTDialog(const QString &oldPT);
    void requestSetAnnotationDialog(const QString &oldAnnotation);
    void requestSetCardCounterDialog(int counterId, const QString &oldValueForDlg);
    void requestZoneViewToggle(const QString &zoneName, int numberCards, bool isReversed = false);
    void requestSortHand(const QList<CardList::SortOption> &options);
    void requestEnableAndSetCreateAnotherTokenAction(const QString &lastTokenName);
    void requestSetLastToken(CardInfoPtr lastToken);

public slots:
    void setLastToken(CardInfoPtr cardInfo);
    void setLastTokenInfo(CardInfoPtr cardInfo);
    void playCard(CardItem *c, bool faceDown);
    void playCardToTable(const CardItem *c, bool faceDown);

    void actUntapAll();
    void actRequestRollDieDialog();
    void actRollDie(int sides, int count);
    void actFlipCoin();
    void actRequestCreateTokenDialog(const QStringList &predefinedTokens);
    void actCreateToken(TokenInfo tokenToCreate);
    void actCreateAnotherToken();
    void actRequestCreateRelatedFromRelationDialog(const CardItem *sourceCard, const CardRelation *cardRelation);
    bool createRelatedFromRelation(const CardItem *sourceCard, const CardRelation *cardRelation, int variableCount);
    void onRelatedCardCreated(const CardItem *sourceCard, const CardRelation *cardRelation);
    void setLastRelatedCreationSucceeded(bool succeeded)
    {
        lastRelatedCreationSucceeded = succeeded;
    }
    void actShuffle();
    void actRequestShuffleTopDialog();
    void actShuffleTop(int number);
    void actRequestShuffleBottomDialog();
    void actShuffleBottom(int number);
    void actDrawCard();
    void actRequestDrawCardsDialog();
    void actDrawCards(int number);
    void actUndoDraw();
    void actRequestMulliganDialog();
    void actMulligan(int number);
    void actMulliganSameSize();
    void actMulliganMinusOne();
    void doMulligan(int number);

    void actPlay(QList<CardItem *> selectedCards);
    void actPlayFacedown(QList<CardItem *> selectedCards);
    void actHide(QList<CardItem *> selectedCards);

    void actMoveTopCardToPlay();
    void actMoveTopCardToPlayFaceDown();
    void actMoveTopCardToGrave();
    void actMoveTopCardToExile();
    void actMoveTopCardsToGrave();
    void actMoveTopCardsToGraveFaceDown();
    void actMoveTopCardsToExile();
    void actMoveTopCardsToExileFaceDown();
    void actRequestMoveTopCardsUntilDialog();
    void moveTopCardsUntil(const QString &expr, MoveTopCardsUntilOptions options);
    void actMoveTopCardToBottom();
    void actRequestMoveTopCardsToDialog(const QString &targetZone, const QString &zoneDisplayName, bool faceDown);
    void moveTopCardsTo(int number, const QString &targetZone, bool faceDown);
    void actDrawBottomCard();
    void actRequestDrawBottomCardsDialog();
    void actDrawBottomCards(int number);
    void actMoveBottomCardToPlay();
    void actMoveBottomCardToPlayFaceDown();
    void actMoveBottomCardToGrave();
    void actMoveBottomCardToExile();
    void actMoveBottomCardsToGrave();
    void actMoveBottomCardsToGraveFaceDown();
    void actMoveBottomCardsToExile();
    void actMoveBottomCardsToExileFaceDown();
    void actMoveBottomCardToTop();
    void actRequestMoveBottomCardsToDialog(const QString &targetZone, const QString &zoneDisplayName, bool faceDown);
    void moveBottomCardsTo(int number, const QString &targetZone, bool faceDown);

    void actSelectAll();
    void actSelectRow();
    void actSelectColumn();

    void actViewLibrary();
    void actViewHand();
    void actRequestViewTopCardsDialog();
    void actViewTopCards(int number);
    void actRequestViewBottomCardsDialog();
    void actViewBottomCards(int number);
    void actAlwaysRevealTopCard(bool alwaysRevealTopCard);
    void actAlwaysLookAtTopCard(bool alwaysRevealTopCard);
    void actViewGraveyard();
    void actLendLibrary(int lendToPlayerId);
    void actRevealTopCards(int revealToPlayerId, int amount);
    void actRevealRandomGraveyardCard(int revealToPlayerId);
    void actViewRfg();
    void actViewSideboard();

    void actSayMessage();

    void actOpenDeckInDeckEditor();
    void actCreatePredefinedToken();
    void actCreateRelatedCard();
    void actCreateAllRelatedCards();

    void actRequestMoveCardXCardsFromTopDialog();
    void actMoveCardXCardsFromTop(QList<CardItem *> selectedCards, int number);
    void actRemoveCardCounter(QList<CardItem *> selectedCards, int counterId);
    void actAddCardCounter(QList<CardItem *> selectedCards, int counterId);
    void actRequestSetCardCounterDialog(QList<CardItem *> selectedCards, int counterId);
    void actSetCardCounter(QList<CardItem *> selectedCards, int counterId, const QString &counterValue);
    void actIncrementAllCardCounters(QList<CardItem *> cardsToUpdate);
    void actApplyTap(QList<CardItem *> cardList, QMap<const CardItem *, ManaTapOption> chosenManaOptions);
    void actApplyTapWithTarget(CardItem *card,
                               CardEffect effect,
                               AbilityTarget target,
                               const QMap<QString, int> &manaPayment = {});
    void actApplyTapWithCost(CardItem *card, CardEffect effect, const QMap<QString, int> &manaPayment);
    void actCheckTrigger(CardItem *card, TriggerKind trigger);
    void actAttach();
    void actUnattach(QList<CardItem *> selectedCards);
    void actDrawArrow();
    void actIncPT(QList<CardItem *> selectedCards, int deltaP, int deltaT);
    void actResetPT(QList<CardItem *> selectedCards);
    void actRequestSetPTDialog(QList<CardItem *> selectedCards);
    void actSetPT(QList<CardItem *> selectedCards, const QString &pt);
    void actIncP(QList<CardItem *> selectedCards);
    void actDecP(QList<CardItem *> selectedCards);
    void actIncT(QList<CardItem *> selectedCards);
    void actDecT(QList<CardItem *> selectedCards);
    void actIncPT(QList<CardItem *> selectedCards);
    void actDecPT(QList<CardItem *> selectedCards);
    void actFlowP(QList<CardItem *> selectedCards);
    void actFlowT(QList<CardItem *> selectedCards);

    void actReduceLifeByPower(QList<CardItem *> selectedCards);

    void actRequestSetAnnotationDialog(QList<CardItem *> selectedCards);
    void actSetAnnotation(QList<CardItem *> selectedCards, const QString &annotation);
    void actReveal(QList<CardItem *> selectedCards, QAction *action);
    void actRevealHand(int revealToPlayerId);
    void actRevealRandomHandCard(int revealToPlayerId);
    void actRevealLibrary(int revealToPlayerId);

    void actSortHand();

    void cardMenuAction(QList<CardItem *> selectedCards, CardMenuActionType type);

    // Phase 8 combat automation, Stage A: attack-target selection + blocker declaration.
    void actDeclareAttacker(QList<CardItem *> selectedCards, int targetPlayerId);
    void actRemoveFromCombat(QList<CardItem *> selectedCards);
    void actDeclareBlocker(QList<CardItem *> selectedCards, int attackerPlayerId, int attackerCardId);
    void actRemoveBlocker(QList<CardItem *> selectedCards);

private:
    PlayerLogic *player;

    int defaultNumberTopCards = 1;
    int defaultNumberTopCardsToPlaceBelow = 1;
    int defaultNumberBottomCards = 1;
    int defaultNumberDieRoll = 20;

    TokenInfo lastTokenInfo;
    int lastTokenTableRow;

    bool movingCardsUntil;
    QTimer *moveTopCardTimer;
    FilterString movingCardsUntilFilter;
    int movingCardsUntilCounter = 0;
    MoveTopCardsUntilOptions movingCardsUntilOptions;

    bool lastRelatedCreationSucceeded = false;

    void createCard(const CardItem *sourceCard,
                    const QString &dbCardName,
                    CardRelationType attach = CardRelationType::DoesNotAttach,
                    bool persistent = false,
                    bool faceDown = false);

    void playSelectedCards(QList<CardItem *> selectedCards, bool faceDown = false);

    void cmdSetTopCard(Command_MoveCard &cmd);
    void cmdSetBottomCard(Command_MoveCard &cmd);

    void offsetCardCounter(QList<CardItem *> selectedCards, int counterId, int offset);

    QList<ManaTapChoice> computeManaTapChoices(const QList<CardItem *> &cardList) const;
};

#endif // COCKATRICE_PLAYER_ACTIONS_H
