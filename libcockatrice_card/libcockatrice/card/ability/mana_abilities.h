#ifndef COCKATRICE_MANA_ABILITIES_H
#define COCKATRICE_MANA_ABILITIES_H

#include <QList>
#include <QString>
#include <QStringList>

class CardInfo;

/**
 * @brief Best-effort recognition of a card's simplest printed mana-producing activated abilities
 * (e.g. "{T}: Add {G}.", "{T}: Add {W} or {U}.", or "{T}: Add one mana of any color.") from its
 * rules text.
 *
 * This is deliberately narrow (design doc §3 Phase 7 "Increment 2: mana abilities") -- its result
 * is used to drive real shared game state (tap flag + a mana counter), so the parser only
 * recognizes unambiguous "always {T}, no other cost or condition" shapes. Anything requiring an
 * additional/alternative cost, or a condition/restriction the parser can't express as a plain
 * color option list, is intentionally left unrecognized -- see
 * COMMANDER_IMPLEMENTATION_STATUS.md's Phase 7 notes and
 * doc/design-docs/phase7-increment2-mana-abilities-plan.md for the full scope boundary.
 */
namespace ManaAbilities
{

struct ManaAbility
{
    // One entry = a fixed, unambiguous color (e.g. Sol Ring's {"C"}). Two or more entries mean
    // the player must choose one of these colors when the ability is activated (e.g. a dual
    // land's {"W", "U"}, or "any color"'s five basics) -- amount always applies per activation,
    // not per option.
    QStringList symbolOptions;
    int amount = 0; // mana produced per activation, e.g. 2 for Sol Ring's "{T}: Add {C}{C}."

    bool operator==(const ManaAbility &other) const
    {
        return symbolOptions == other.symbolOptions && amount == other.amount;
    }

    [[nodiscard]] bool isChoice() const
    {
        return symbolOptions.size() > 1;
    }
};

/**
 * @brief Parses @p card's rules text for standalone mana-ability lines and returns one
 * ManaAbility per qualifying line found:
 *  - "{T}: Add {X}{X}...{X}." where every {X} is the same symbol -> a fixed ManaAbility.
 *  - "{T}: Add {X} or {Y}." / "{T}: Add {X}, {Y}, or {Z}." -> a choice ManaAbility (amount 1).
 *  - A small set of known real "any color" phrasings (e.g. "Add one mana of any color.",
 *    Command Tower's commander-color-identity-qualified phrasing, Reflecting Pool's
 *    already-produced-by-a-land phrasing) -> a choice ManaAbility over all five colors. These
 *    phrasings' real restrictions depend on game state (commander color identity, other
 *    permanents on the battlefield) this parser can't inspect, so -- consistent with this fork's
 *    advisory, not-fully-enforced philosophy -- they're simplified to "any of the five colors,
 *    let the player pick the legal one themselves."
 *
 * A line only contributes a ManaAbility if it matches one of these exact shapes (after stripping
 * reminder text and trailing whitespace, except for a basic-land-style whole-line parenthetical,
 * which is unwrapped and matched directly since that reminder text IS the entire printed
 * ability) -- any additional cost or trailing condition on the same line causes that line to be
 * skipped entirely, never guessed at.
 */
QList<ManaAbility> parse(const CardInfo &card);

} // namespace ManaAbilities

#endif // COCKATRICE_MANA_ABILITIES_H
