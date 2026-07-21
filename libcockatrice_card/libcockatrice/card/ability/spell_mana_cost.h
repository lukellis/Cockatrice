#ifndef COCKATRICE_SPELL_MANA_COST_H
#define COCKATRICE_SPELL_MANA_COST_H

#include "card_effects.h"

#include <QString>
#include <optional>

class CardInfo;

/**
 * @brief Parses a card's own printed mana cost (CardInfo::getManaCost(), e.g. "{2}{R}{R}") into a
 * ManaCost, for gating real spell casting from hand -- see COMMANDER_IMPLEMENTATION_STATUS.md and
 * doc/commander-status/phase6-mana.md for the feature this is part of.
 *
 * Deliberately independent of ActivatedAbilities' own private cost-prefix tokenizer (same "small
 * per-parser copy rather than shared extraction" precedent as this fork's other tiny duplicated
 * helpers, e.g. withoutReminderText()) -- that one only ever sees a substring already guaranteed by
 * its containing line regex to be a valid cost, whereas this parses a whole, independently-sourced
 * string that real cards can print with symbol shapes (hybrid, Phyrexian) the activated-ability
 * parser was never asked to handle.
 */
namespace SpellManaCost
{

/**
 * @brief Parses @p card's printed mana cost. The entire string must be covered by concatenated
 * "{digit}", "{W|U|B|R|G|C}", "{X}", two-color hybrid "{W/U}"-style, or Phyrexian "{W/P}"-style
 * tokens (case-insensitive) with nothing left over -- a monocolored hybrid ("{2/W}"), snow ("{S}"),
 * or split-cost ("3U // 4UU") symbol anywhere in the string still means the whole cost is not
 * confidently parseable, so this returns std::nullopt rather than guess at a partial cost (same
 * "exact shape or skip" conservatism as every other parser in this fork). An empty string (no
 * printed mana cost -- true of every basic land) parses to a free ManaCost{}: zero tokens trivially
 * covers the whole (empty) string. Hybrid/Phyrexian/X symbols populate ManaCost's
 * hybridPips/phyrexianPips/xCount fields rather than coloredPips/generic directly -- see
 * RulesEngine::resolveManaCost() for how a caster's choices fold those into a plain payable cost.
 */
std::optional<ManaCost> parse(const CardInfo &card);

} // namespace SpellManaCost

#endif // COCKATRICE_SPELL_MANA_COST_H
