#ifndef COCKATRICE_COMMANDER_DECK_VALIDATOR_H
#define COCKATRICE_COMMANDER_DECK_VALIDATOR_H

#include <QStringList>

class DeckListModel;

/**
 * @brief Validates a deck against the official Commander/EDH deck construction rules:
 * exactly 100 cards including the commander, singleton (except basic lands), a legal
 * commander designation, and every card's color identity fitting within the commander's.
 *
 * @see https://mtgcommander.net/rules.html
 */
namespace CommanderDeckValidator
{

struct Result
{
    bool isValid = true;
    QStringList errors;
};

/**
 * @brief Runs full Commander deck construction validation against the given model.
 *
 * Callers should generally only invoke this when @c model.getDeckList()->getGameFormat()
 * is a Commander-style singleton format; it does not check the format name itself.
 */
Result validate(const DeckListModel &model);

} // namespace CommanderDeckValidator

#endif // COCKATRICE_COMMANDER_DECK_VALIDATOR_H
