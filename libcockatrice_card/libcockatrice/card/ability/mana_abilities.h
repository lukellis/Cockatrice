#ifndef COCKATRICE_MANA_ABILITIES_H
#define COCKATRICE_MANA_ABILITIES_H

#include <QList>
#include <QString>

class CardInfo;

/**
 * @brief Best-effort recognition of a card's simplest printed fixed-mana activated abilities
 * (e.g. "{T}: Add {G}." or "{T}: Add {C}{C}.") from its rules text.
 *
 * This is deliberately narrow (design doc §3 Phase 7 "Increment 2: mana abilities") -- unlike
 * CardKeywords, its result is used to offer a context-menu action that mutates real shared game
 * state (tap flag + a mana counter), so the parser only recognizes the unambiguous "always the
 * same fixed amount of one color, tap only, no other cost or condition" shape. Anything requiring
 * a player choice (e.g. "Add one mana of any color"), an additional/alternative cost, or a
 * condition/restriction is intentionally left unrecognized -- see
 * COMMANDER_IMPLEMENTATION_STATUS.md's Phase 7 notes and
 * doc/design-docs/phase7-increment2-mana-abilities-plan.md for the full scope boundary.
 */
namespace ManaAbilities
{

struct ManaAbility
{
    QString producedSymbol; // "W", "U", "B", "R", "G", or "C"
    int amount = 0;         // count of repeated symbols, e.g. 2 for Sol Ring's "{T}: Add {C}{C}."

    bool operator==(const ManaAbility &other) const
    {
        return producedSymbol == other.producedSymbol && amount == other.amount;
    }
};

/**
 * @brief Parses @p card's rules text for standalone "{T}: Add {X}{X}...{X}." lines, where every
 * {X} is the same single mana symbol (one of {W}{U}{B}{R}{G}{C}), and returns one ManaAbility per
 * qualifying line found.
 *
 * A line only contributes a ManaAbility if it matches this exact shape (after stripping reminder
 * text and trailing whitespace) -- any additional cost, player choice between symbols, or trailing
 * condition on the same line causes that line to be skipped entirely, never guessed at.
 */
QList<ManaAbility> parse(const CardInfo &card);

} // namespace ManaAbilities

#endif // COCKATRICE_MANA_ABILITIES_H
