#ifndef COCKATRICE_ACTIVATED_ABILITIES_H
#define COCKATRICE_ACTIVATED_ABILITIES_H

#include "card_effects.h"

#include <QList>

class CardInfo;

/**
 * @brief Best-effort recognition of a card's simplest printed, self-targeted "{T}: <effect>."
 * activated abilities (e.g. "{T}: Draw a card.", "{T}: You gain 1 life.") from its rules text.
 *
 * This is Stage 1 of a real card-ability execution engine (see
 * COMMANDER_IMPLEMENTATION_STATUS.md's Phase 7 section for the full staged roadmap) -- structurally
 * a near-exact sibling of ManaAbilities::parse() (same line-by-line, same reminder-text handling,
 * same "exact shape or skip" conservatism), generalized from mana-producing effects to a small
 * whitelist of other tap-only, no-target effects. Anything needing a target, a non-tap cost, or
 * more than one effect per line is intentionally left unrecognized -- expanding the whitelist is
 * cheap, low-risk follow-up work once this shape is proven, not attempted here.
 */
namespace ActivatedAbilities
{

/**
 * @brief Parses @p card's rules text for standalone "{T}: <effect>." lines matching one of a small
 * whitelist of exact effect shapes, and returns one ActivatedAbility per qualifying line found.
 * Currently recognized shapes: "{T}: Draw a card.", "{T}: You gain N life.", "{T}: You lose N
 * life." Anything else on a "{T}: ..." line (a target, an additional/alternative cost, a trailing
 * condition, or an effect shape not in the whitelist above) causes that line to be skipped
 * entirely, never guessed at -- same philosophy as ManaAbilities::parse().
 */
QList<ActivatedAbility> parse(const CardInfo &card);

} // namespace ActivatedAbilities

#endif // COCKATRICE_ACTIVATED_ABILITIES_H
