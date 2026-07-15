#include "commander_deck_validator.h"

#include "deck_list_model.h"

#include <QCoreApplication>
#include <libcockatrice/card/database/card_database_manager.h>
#include <libcockatrice/card/format/commander_rules.h>
#include <libcockatrice/deck_list/deck_list.h>
#include <libcockatrice/deck_list/tree/deck_list_card_node.h>
#include <libcockatrice/deck_list/tree/inner_deck_list_node.h>

namespace CommanderDeckValidator
{

namespace
{
// A plain free function (not a QObject subclass) has no tr(); this is the direct Qt-idiomatic
// equivalent, giving these user-facing strings (shown in the deck editor) a translation context
// per CONTRIBUTING.md's translation guidelines.
QString tr(const char *sourceText)
{
    return QCoreApplication::translate("CommanderDeckValidator", sourceText);
}
} // namespace

Result validate(const DeckListModel &model)
{
    Result result;

    const QSharedPointer<DeckList> deckList = model.getDeckList();
    if (!deckList) {
        result.isValid = false;
        result.errors << tr("No deck loaded.");
        return result;
    }

    // ---- Commander designation ----
    const CardRef commanderRef = deckList->getBannerCard();
    if (commanderRef.isEmpty()) {
        result.isValid = false;
        result.errors << tr("No commander has been designated for this deck.");
        return result;
    }

    const ExactCard commanderCard = CardDatabaseManager::query()->getCard(commanderRef);
    const CardInfoPtr commanderInfo = commanderCard.getCardPtr();
    if (!commanderInfo) {
        result.isValid = false;
        result.errors << tr("Commander \"%1\" could not be found in the card database.").arg(commanderRef.name);
        return result;
    }

    if (!CommanderRules::canBeCommander(*commanderInfo)) {
        result.isValid = false;
        result.errors << tr("\"%1\" is not a legal commander (must be a legendary creature, or a card whose text "
                            "says it can be your commander).")
                             .arg(commanderInfo->getName());
    }

    const QSet<QChar> commanderIdentity = CommanderRules::colorIdentity(*commanderInfo);

    const QList<const DecklistCardNode *> mainDeckCards = model.getCardNodesForZone(DECK_ZONE_MAIN);

    // ---- Deck size: 100 cards total, including the commander ----
    // The deck editor's Banner Card picker is populated from cards already in the main zone (a
    // player adds their commander like any other card, then designates it), so the commander is
    // normally already counted by the main-zone sum below. Only add it separately when it isn't
    // present there — e.g. a banner card set via an API/import path that doesn't also add a main
    // deck entry for it.
    bool commanderIsInMainDeck = false;
    int totalCards = 0;
    for (const DecklistCardNode *card : mainDeckCards) {
        totalCards += card->getNumber();
        if (card->getName() == commanderRef.name) {
            commanderIsInMainDeck = true;
        }
    }
    if (!commanderIsInMainDeck) {
        totalCards += 1; // the commander itself, tracked outside the main zone
    }
    if (totalCards != 100) {
        result.isValid = false;
        result.errors << QStringLiteral("Commander decks must contain exactly 100 cards including the commander "
                                        "(found %1).")
                             .arg(totalCards);
    }

    // ---- Per-card: singleton (already computed by the model's format-legality pass) + color identity ----
    for (const DecklistCardNode *card : mainDeckCards) {
        const ExactCard exactCard = CardDatabaseManager::query()->getCard(card->toCardRef());
        const CardInfoPtr cardInfo = exactCard.getCardPtr();
        if (!cardInfo) {
            result.isValid = false;
            result.errors << QStringLiteral("\"%1\" could not be found in the card database.").arg(card->getName());
            continue;
        }

        if (!card->getFormatLegality()) {
            result.isValid = false;
            result.errors << QStringLiteral("\"%1\" is not legal in this Commander deck (banned, or more than one "
                                            "copy of a non-basic-land card).")
                                 .arg(cardInfo->getName());
        }

        const QSet<QChar> cardIdentity = CommanderRules::colorIdentity(*cardInfo);
        if (!CommanderRules::isWithinColorIdentity(cardIdentity, commanderIdentity)) {
            result.isValid = false;
            result.errors
                << QStringLiteral("\"%1\" is outside the commander's color identity.").arg(cardInfo->getName());
        }
    }

    return result;
}

} // namespace CommanderDeckValidator
