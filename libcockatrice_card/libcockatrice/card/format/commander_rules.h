#ifndef COCKATRICE_COMMANDER_RULES_H
#define COCKATRICE_COMMANDER_RULES_H

#include <QSet>
#include <QString>

class CardInfo;

/**
 * @brief Rules helpers for the Commander/EDH format (per the official rules at mtgcommander.net).
 */
namespace CommanderRules
{

/**
 * @brief Checks whether a card is legal to be designated as a Commander.
 *
 * A card can be a commander if it's a legendary creature, or if its rules text explicitly
 * grants the ability (e.g. "... can be your commander.").
 */
bool canBeCommander(const CardInfo &card);

/**
 * @brief Computes a card's color identity per rule 903.4: its own color(s), plus the color
 * of any mana symbols in its mana cost and rules text (reminder text excluded).
 *
 * @return The set of colors ('W', 'U', 'B', 'R', 'G') in the card's color identity.
 */
QSet<QChar> colorIdentity(const CardInfo &card);

/**
 * @brief Checks whether a card's color identity fits within a commander's color identity,
 * i.e. every color in @p cardIdentity is also present in @p commanderIdentity.
 */
bool isWithinColorIdentity(const QSet<QChar> &cardIdentity, const QSet<QChar> &commanderIdentity);

/**
 * @brief Checks whether @p format is a Commander-family format that restricts deck contents to
 * a designated card's color identity (Commander, Duel Commander, Brawl, Standard Brawl,
 * Oathbreaker, Pauper Commander, Predh). Case-insensitive.
 *
 * A deck's banner card (DeckList::getBannerCard()) is sometimes just a cosmetic "cover card" for
 * non-Commander formats, so callers must check this before treating the banner card as a
 * color-identity-restricting commander.
 */
bool formatUsesColorIdentity(const QString &format);

/**
 * @brief Checks whether a server room's game-type label (e.g. "Commander", "Commander (1v1)")
 * denotes a Commander-family game, for gating server-side gameplay behavior (command zone
 * placement, tax/damage tracking, turn-structure automation) to Commander games only.
 *
 * This is deliberately looser than @c formatUsesColorIdentity: room game-type labels are
 * free-text strings configured by server admins (@c Server_Room::getGameTypes()), not the
 * fixed deck-format names checked there. Case-insensitive substring match on "commander".
 */
bool gameTypeLabelIsCommander(const QString &gameTypeLabel);

} // namespace CommanderRules

#endif // COCKATRICE_COMMANDER_RULES_H
