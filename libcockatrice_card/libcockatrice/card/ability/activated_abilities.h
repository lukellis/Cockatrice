#ifndef COCKATRICE_ACTIVATED_ABILITIES_H
#define COCKATRICE_ACTIVATED_ABILITIES_H

#include "card_effects.h"

#include <QList>

class CardInfo;

/**
 * @brief Best-effort recognition of a card's simplest printed "{T}: <effect>." activated
 * abilities (e.g. "{T}: Draw a card.", "{T}: You gain 1 life.", "{T}: Deal 1 damage to any
 * target.") from its rules text.
 *
 * This is Stages 1-2 of a real card-ability execution engine (see
 * COMMANDER_IMPLEMENTATION_STATUS.md's Phase 7 section for the full staged roadmap) -- structurally
 * a near-exact sibling of ManaAbilities::parse() (same line-by-line, same reminder-text handling,
 * same "exact shape or skip" conservatism), generalized from mana-producing effects to a small
 * whitelist of other tap-only effects. Anything needing a non-tap cost, a target shape outside the
 * whitelist below, or more than one effect per line is intentionally left unrecognized -- expanding
 * the whitelist is cheap, low-risk follow-up work once this shape is proven, not attempted here.
 */
namespace ActivatedAbilities
{

/**
 * @brief Parses @p card's rules text for standalone "{T}: <effect>." (optionally
 * "<mana cost>, {T}: <effect>.", Phase 7 Stage 4) lines matching one of a small whitelist of exact
 * effect shapes, and returns one ActivatedAbility per qualifying line found. Currently recognized
 * shapes: "{T}: Draw a card.", "{T}: You gain N life.", "{T}: You lose N life.", "{T}: Deal N
 * damage to any target." (the last sets CardEffect::target to TargetKind::AnyTarget; older
 * non-"any target" templating like "target creature"/"target player" is deliberately not
 * matched), each optionally prefixed with a directly-concatenated mana-cost symbol string (plain
 * digits and/or W/U/B/R/G/C only -- no {X}, hybrid, or Phyrexian symbols) followed by a comma,
 * populating ActivatedAbility::cost (default free when no prefix is present). Anything else on a
 * "{T}: ..." line (a non-mana additional/alternative cost, a trailing condition, or an effect
 * shape not in the whitelist above) causes that line to be skipped entirely, never guessed at --
 * same philosophy as ManaAbilities::parse().
 */
QList<ActivatedAbility> parse(const CardInfo &card);

} // namespace ActivatedAbilities

#endif // COCKATRICE_ACTIVATED_ABILITIES_H
