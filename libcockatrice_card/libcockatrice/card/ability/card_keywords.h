#ifndef COCKATRICE_CARD_KEYWORDS_H
#define COCKATRICE_CARD_KEYWORDS_H

#include <QSet>
#include <QString>
#include <QStringList>

class CardInfo;

/**
 * @brief Best-effort recognition of a card's printed evergreen keyword abilities (e.g. Flying,
 * Trample) from its rules text.
 *
 * This is display-only infrastructure (design doc §3 Phase 7 "Increment 1: Keywords") -- it does
 * not execute, enforce, or otherwise act on the abilities it recognizes. It exists so the client
 * can show a player which evergreen keywords a card has without needing a full rules-text parser;
 * building one of those is squarely out of this fork's scope (see
 * COMMANDER_IMPLEMENTATION_STATUS.md's Phase 7 notes).
 */
namespace CardKeywords
{

/**
 * @brief The evergreen keyword abilities recognized by @c parse(), in their canonical printed
 * capitalization. Deliberately limited to keywords with no attached cost or variable text (so a
 * plain string match is meaningful) -- excludes things like "Ward {2}" or "Protection from red".
 */
const QStringList &evergreenKeywords();

/**
 * @brief Parses @p card's rules text for standalone printed keyword-ability lines (e.g. "Flying"
 * or "First strike, trample.") and returns the recognized keywords found, in canonical form.
 *
 * Deliberately conservative: a line only contributes keywords if *every* comma/"and"-separated
 * token on it is a recognized keyword (after stripping reminder text and trailing punctuation).
 * This avoids false positives from abilities that merely *mention* a keyword without granting it
 * to this card as a printed ability (e.g. "Whenever this creature attacks, it gains flying until
 * end of turn" is correctly excluded, since that line has other words on it).
 */
QSet<QString> parse(const CardInfo &card);

} // namespace CardKeywords

#endif // COCKATRICE_CARD_KEYWORDS_H
