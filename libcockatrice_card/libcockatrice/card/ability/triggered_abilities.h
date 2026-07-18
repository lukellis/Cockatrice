#ifndef COCKATRICE_TRIGGERED_ABILITIES_H
#define COCKATRICE_TRIGGERED_ABILITIES_H

#include "card_effects.h"

#include <QList>

class CardInfo;

/**
 * @brief Best-effort recognition of a card's simplest printed triggered abilities -- "When/Whenever
 * <CardName> enters the battlefield, <effect>." and "When <CardName> dies, <effect>." -- from its
 * rules text.
 *
 * This is Phase 7 Stage 5 of the card-ability execution engine (see
 * COMMANDER_IMPLEMENTATION_STATUS.md's Phase 7 section for the full staged roadmap) -- structurally
 * a sibling of ActivatedAbilities::parse() (same line-by-line approach, same reminder-text handling,
 * same "exact shape or skip" conservatism), but the trigger condition is a zone change confirmed by
 * PlayerEventHandler::eventMoveCard() rather than a player right-clicking Tap, so there is no cost
 * concept here at all (no {T}, no mana) -- a recognized TriggeredAbility fires unconditionally the
 * instant its zone-change condition is met.
 *
 * Unlike every other parser in this family, the match pattern is per-card (it embeds the card's own
 * literal name, since real Oracle text is self-referential by printed name, not a placeholder like
 * "~" or "this creature") -- see parse()'s doc comment for the exact shapes recognized.
 */
namespace TriggeredAbilities
{

/**
 * @brief Parses @p card's rules text for standalone triggered-ability lines matching one of a small
 * whitelist of exact shapes, and returns one TriggeredAbility per qualifying line found:
 *  - "When <CardName> enters the battlefield, <effect>." / "Whenever <CardName> enters the
 *    battlefield, <effect>." -> TriggerKind::EntersBattlefield.
 *  - "When <CardName> dies, <effect>." -> TriggerKind::Dies.
 * where <CardName> must be @p card's own literal printed name (case-insensitively) and <effect> must
 * be one of the same effect phrasings ActivatedAbilities::parse() recognizes: "Draw a card.", "You
 * gain N life.", "You lose N life." -- deliberately excluding "deal N damage to any target." this
 * stage, since a targeted effect needs AbilityTargetPicker, which today is only ever invoked from a
 * deliberate context-menu click (Stage 2's whole design); popping a targeting picker automatically,
 * mid drag-and-drop, the instant a trigger fires is a real interaction-design problem left as a
 * documented follow-up, not attempted as a side effect of this stage.
 *
 * A line only contributes a TriggeredAbility if it matches one of these exact shapes (after
 * stripping reminder text) -- any other card-name reference, a different trigger condition, or an
 * effect shape not in the whitelist above causes that line to be skipped entirely, never guessed
 * at -- same philosophy as every other parser in this family.
 */
QList<TriggeredAbility> parse(const CardInfo &card);

} // namespace TriggeredAbilities

#endif // COCKATRICE_TRIGGERED_ABILITIES_H
