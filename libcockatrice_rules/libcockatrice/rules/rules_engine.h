#ifndef COCKATRICE_RULES_ENGINE_H
#define COCKATRICE_RULES_ENGINE_H

#include <QList>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <libcockatrice/card/ability/card_effects.h>
#include <libcockatrice/card/ability/static_abilities.h>
#include <optional>

namespace Rules
{

/**
 * @brief What (if anything) turn-structure automation should do when entering a phase, given the
 * phase index (see cockatrice/src/game/phase.cpp) and turn context.
 *
 * Kept as a pure decision (see RulesEngine::phaseAutomationFor) separate from the server's
 * side-effecting Server_Game::setActivePhase() so the logic -- including the rule 103.8a/103.8c
 * first-draw-skip arithmetic -- is unit-testable without a fully constructed, participant-registered
 * game. (Formerly Server_Game's CommanderPhaseAutomation enum.)
 */
enum class PhaseAutomation
{
    None,
    UntapActivePlayer,
    DrawForActivePlayer
};

/**
 * @brief A single activated ability (Phase 7 Stages 1/2's ActivatedAbilities IR) that's been
 * activated but not yet resolved -- pushed onto RulesEngine's pending-ability stack when the
 * activating client sends Command_ActivateAbility. Deliberately named distinctly from
 * ZoneNames::STACK's per-player visual pile (see COMMANDER_IMPLEMENTATION_STATUS.md's Phase 7
 * Stage 3 section): this is new, separate, engine-level state, not a card sitting in any zone.
 */
struct PendingAbility
{
    int controllerId = -1;
    CardEffect effect;       // reuses EffectKind + amount from Stages 1/2
    int targetPlayerId = -1; // meaningful only if effect.target == TargetKind::AnyTarget
    QString targetZone;      // empty unless the target is a permanent, not a player
    int targetCardId = -1;
};

/**
 * @brief Result of RulesEngine::passPriority(): whether the pass changed anything, the resulting
 * priority holder (-1 == no one), and -- if a priority round was exhausted with a non-empty
 * pending-ability stack -- the ability that was popped and needs to be resolved (rule 117.4
 * simplified; see passPriority()'s doc comment).
 */
struct PriorityPassResult
{
    bool changed; ///< false if the pass was a no-op (the passing player didn't hold priority)
    int holder;   ///< the resulting priority holder after the pass; -1 means no one holds it
    std::optional<PendingAbility> resolvedAbility; ///< set only when exhaustion popped something
};

/**
 * @brief The Commander rules engine: the single home for this fork's Commander game-rules logic.
 *
 * This fork is wholly Commander-dedicated, so the engine is always active (there is no game-type
 * gate). It is deliberately a *pure decision* layer: it owns the priority-round state and computes
 * what should happen, but performs no I/O. The server (Server_Game / Server_Player) holds a
 * RulesEngine, consults it, and performs the resulting side effects (broadcasting
 * Event_PriorityChanged, untapping, drawing, etc.). This split is what makes the engine
 * unit-testable without a live participant -- see tests/rules/.
 *
 * Real rule *enforcement* (stack resolution, mana payment, combat) will grow inside this class
 * incrementally; today it covers turn-structure automation decisions and priority passing, migrated
 * verbatim from Server_Game. See COMMANDER_IMPLEMENTATION_STATUS.md's Phase 1 / Design & Review
 * sections for the roadmap.
 */
class RulesEngine
{
public:
    // ---- Pure decision helpers (stateless) ----

    /**
     * @brief Pure decision logic for what PhaseAutomation applies when entering @p phase, given the
     * game's current @p turnNumber and @p playerCount. Rule 103.8a/103.8c: only a strict two-player
     * game's starting player skips their first draw step; multiplayer games never skip it.
     */
    static PhaseAutomation phaseAutomationFor(int phase, int turnNumber, int playerCount);

    /**
     * @brief Phase 8 Stage 6: whether @p phase (per cockatrice/src/game/phase.cpp's Phases::phases[]
     * ordering) is one of the five combat phases (Beginning of Combat through End of Combat, indices
     * 4-8 inclusive). Used to decide when to auto-clear the "attacking" attribute -- rule 506.4
     * simplified as "left the combat-phase range" rather than one specific transition, since this
     * fork's phases can be freely jumped in any order (see Server_Game::setActivePhase()'s caller).
     */
    static bool isCombatPhase(int phase);

    /**
     * @brief Phase index for the Declare Blockers step (see isCombatPhase()'s phase-index doc:
     * cockatrice/src/game/phase.cpp's Phases::phases[] ordering).
     */
    static constexpr int DECLARE_BLOCKERS_PHASE = 6;

    /**
     * @brief Rule 509.1a, simplified: a blocker may only be declared during the Declare Blockers
     * step, and must itself be untapped and not already attacking. Ownership/identity checks (a
     * blocker can't block its own controller's creature, and the target must actually be
     * attacking) are the server's job -- this is deliberately just the pure phase/state part, kept
     * unit-testable the same way isCombatPhase() is.
     */
    static bool canDeclareBlocker(int phase, bool blockerTapped, bool blockerAttacking);

    /**
     * @brief Phase index for the Declare Attackers step (see isCombatPhase()'s phase-index doc).
     */
    static constexpr int DECLARE_ATTACKERS_PHASE = 5;

    /**
     * @brief Rule 508.1a, simplified: a creature may only be declared as an attacker during the
     * Declare Attackers step, and only by its controller if they're the active player (only the
     * active player attacks in a turn). Untapped/vigilance/summoning-sickness are not modeled
     * here -- deliberately just the phase/turn part, kept unit-testable the same way
     * canDeclareBlocker() is.
     */
    static bool canDeclareAttacker(int phase, bool controllerIsActivePlayer);

    /**
     * @brief Total number of turn-structure phases/steps (see cockatrice/src/game/phase.cpp's
     * Phases::phases[] -- same "coupled to that array's ordering" caveat every other phase
     * constant in this class already documents).
     */
    static constexpr int PHASE_COUNT = 11;

    /**
     * @brief Whether a player-requested jump from @p currentPhase to @p requestedPhase is a legal
     * single step forward through turn structure (rule 500.1: phases and steps always happen in
     * order, one at a time). Reaching phase 0 again is a *new turn* (a different active player,
     * rule 500.3-500.4) rather than a phase advance, so it's deliberately never valid via this
     * check -- see canEndTurn() for that transition instead.
     */
    static bool canAdvanceToPhase(int currentPhase, int requestedPhase);

    /**
     * @brief Rule 500.3/514: a turn may only be ended (moving to the next player's untap step)
     * once the table has actually reached the End/Cleanup step -- see CLEANUP_PHASE.
     */
    static bool canEndTurn(int phase);

    /**
     * @brief Phase index for the Untap step, i.e. the first step of a new turn (see
     * canAdvanceToPhase()'s doc comment: reaching this phase again is what marks a new turn).
     */
    static constexpr int UNTAP_PHASE = 0;

    /** @brief Phase index for the First Main phase (see isCombatPhase()'s phase-index doc). */
    static constexpr int FIRST_MAIN_PHASE = 3;

    /** @brief Phase index for the Second Main phase (see isCombatPhase()'s phase-index doc). */
    static constexpr int SECOND_MAIN_PHASE = 9;

    /**
     * @brief Rule 505.5a, simplified: a sorcery-speed card (anything but an instant -- flash
     * isn't modeled) may only be played during one of the controller's own main phases, by the
     * active player, while they hold priority. @p controllerHoldsPriority stands in for "the
     * stack is empty and nothing is pending a response" the same simplified way holding priority
     * already stands in for "no one has responded yet" elsewhere in this fork (Phase 5) -- not a
     * literal stack-empty check, since this fork has no general spell stack to inspect.
     */
    static bool canCastSorcerySpeed(int phase, bool controllerIsActivePlayer, bool controllerHoldsPriority);

    /** @brief Phase index for the Combat Damage step (see isCombatPhase()'s phase-index doc). */
    static constexpr int COMBAT_DAMAGE_PHASE = 7;

    /**
     * @brief Phase index for the (combined, in this fork's simplified turn structure) End Step /
     * Cleanup step. Rule 514.2: damage marked on permanents is removed here, not when combat
     * itself ends -- a creature that survived combat still remembers its damage through Second
     * Main, which is why the DAMAGE_CARD_COUNTER_ID counter (see card_effects.h; already reused
     * from Phase 7 Stage 5's targeted damage effect) is cleared on this specific phase, not (like
     * AttrAttacking) whenever the game leaves isCombatPhase()'s range.
     */
    static constexpr int CLEANUP_PHASE = 10;

    /**
     * @brief A creature's controller + card id + numeric power/toughness, the minimal shape
     * calculateCombatDamage() needs -- deliberately decoupled from Server_Card so the damage math
     * stays unit-testable without a live server (see rules_engine_test.cpp). @p hasDeathtouch and
     * @p hasTrample are Stage C additions (rule 702.2b/702.19b); @p hasFirstStrike and
     * @p hasDoubleStrike are Stage D additions (rule 702.7/702.4) -- all four synced from the
     * client's CardKeywords::parse() via the AttrKeywords card attribute -- see phase8-combat.md's
     * Stage C/D sections for why the server has to be told these rather than looking them up
     * itself. Appended after hasTrample (rather than interleaved) so existing positional aggregate
     * initializers in rules_engine_test.cpp keep compiling unchanged.
     */
    struct CombatCreature
    {
        int playerId = -1;
        int cardId = -1;
        int power = 0;
        int toughness = 0;
        bool hasDeathtouch = false;
        bool hasTrample = false;
        bool hasFirstStrike = false;
        bool hasDoubleStrike = false;
    };

    /**
     * @brief Rule 510.4's two combat-damage sub-passes: first-strike/double-strike creatures deal
     * (and take) combat damage in the first-strike sub-pass; everything else deals damage in the
     * regular sub-pass, and double-strike creatures act in both. This fork models the two
     * sub-passes as two calls to calculateCombatDamage() (see its own doc comment and Stage D in
     * phase8-combat.md) rather than a new turn-structure phase, since Cockatrice's phase list is a
     * single hardcoded array shared by client and server with no phase enum -- reindexing it for
     * one keyword pairing was judged not worth the blast radius (see phase8-combat.md's Stage D
     * section for the full design-tradeoff writeup).
     */
    enum class CombatDamageStep
    {
        FirstStrike,
        Regular
    };

    /**
     * @brief Rule 702.7b/510.4: whether a creature with @p hasFirstStrike/@p hasDoubleStrike deals
     * (and takes) combat damage during @p step. First strike and double strike both act in the
     * FirstStrike step; everything else acts in the Regular step, and double strike acts in both
     * (a creature with only first strike does not act again in the Regular step, having already
     * assigned its damage).
     */
    static bool participatesInStrikeStep(bool hasFirstStrike, bool hasDoubleStrike, CombatDamageStep step);

    /**
     * @brief One attacking creature's combat, from RulesEngine's point of view: who it's
     * attacking (targetPlayerId, -1 if no target was ever set -- see Stage A) and, if any, the
     * creatures currently blocking it, in declaration order (rule 509's damage-assignment order is
     * a player choice this fork doesn't model; declaration order is the deterministic stand-in --
     * see COMMANDER_IMPLEMENTATION_STATUS.md's Phase 8 doc). @p blocked (Stage D) is true if this
     * attacker was ever declared blocked, independent of whether @p blockers is currently empty --
     * rule 509.1h: an attacker remains blocked even after every creature blocking it is removed
     * from combat (e.g. killed in the first-strike sub-pass), so a double-strike/trample attacker
     * whose only blocker died between sub-passes must NOT be treated as unblocked in the regular
     * sub-pass. Left false (the pre-Stage-D default) is fine for any caller that doesn't split
     * combat damage into sub-passes -- calculateCombatDamage() falls back to
     * `!blockers.isEmpty()` in that case, matching this field's behavior before it existed.
     */
    struct CombatAttack
    {
        CombatCreature attacker;
        int targetPlayerId = -1;
        bool blocked = false;
        QList<CombatCreature> blockers;
    };

    /**
     * @brief The result of resolving a set of CombatAttack%s: life lost per defending player,
     * damage marked per card (both attackers hit by blockers and blockers hit by their attacker),
     * keyed by the card's controller id then its card id (card ids are only unique within one
     * player's zones, not game-wide -- see Stage A's AttrBlocking doc comment), and (Stage C)
     * which of those damaged cards had at least one point of that damage come from a deathtouch
     * source -- see isLethallyDamaged()'s doc comment for why marked-damage amount alone isn't
     * enough to answer "is this creature dead" once deathtouch is in play.
     */
    struct CombatDamageResult
    {
        QMap<int, int> playerLifeLoss;
        QMap<int, QMap<int, int>> cardDamageMarked;
        QMap<int, QSet<int>> deathtouchDamaged;
    };

    /**
     * @brief Rule 510, simplified: an unblocked attacker with a target deals its power to that
     * player; a blocked attacker splits its power across its (possibly zero, per rule 509.1h --
     * see CombatAttack::blocked) blockers in declaration order, assigning each blocker the amount
     * needed to be considered lethal -- its full toughness, or just 1 if the attacker has
     * deathtouch (rule 702.2b) -- before moving to the next; any power left over after all
     * blockers have been assigned lethal damage tramples through to the defending player if the
     * attacker has trample (rule 702.19b), otherwise it's wasted. Blockers simultaneously deal
     * their combined power back to the attacker (rule 510.1a).
     *
     * @p step (Stage D, rule 510.4) restricts both sides of this to only the creatures that act
     * during that sub-pass -- see participatesInStrikeStep(): an attacker/blocker that doesn't act
     * this sub-pass neither deals nor is assigned damage by the *other* side's own action this
     * call (a non-acting attacker still gets damage marked on it by a blocker that *does* act, and
     * vice versa -- only the acting side's own damage-dealing is gated). Defaults to Regular so
     * every pre-Stage-D caller (nobody in combat has first/double strike) is unaffected -- see
     * phase8-combat.md's Stage D section.
     *
     * Explicitly out of scope, same as the rest of this fork's combat depth: protection, damage
     * prevention/replacement effects, planeswalker/battle damage, and player-chosen
     * damage-assignment order (declaration order stands in, same as before). An attacker with no
     * target (targetPlayerId == -1) deals no damage at all if unblocked, trample included.
     */
    static CombatDamageResult calculateCombatDamage(const QList<CombatAttack> &attacks,
                                                    CombatDamageStep step = CombatDamageStep::Regular);

    /**
     * @brief Rule 704.5g simplified: whether a creature with @p toughness and @p markedDamage
     * marked on it should be destroyed as a state-based action. Normally that's just
     * @p markedDamage >= @p toughness, but rule 702.2b makes any nonzero damage from a deathtouch
     * source lethal regardless of toughness -- @p anyDamageFromDeathtouch (see
     * CombatDamageResult::deathtouchDamaged) covers that case. Rule 702.12b (indestructible)
     * overrides both: @p indestructible short-circuits straight to false, since indestructible
     * creatures can't be destroyed by damage at all, however much (or however lethal a source) is
     * marked on them.
     */
    static bool isLethallyDamaged(int markedDamage, int toughness, bool anyDamageFromDeathtouch, bool indestructible);

    /**
     * @brief Parses a P/T string (Server_Card::getPT(), already "effective" -- counters are baked
     * into it by the client's existing +1/+1-counter menu, see CardItem::parsePT()) into
     * (power, toughness), or std::nullopt if either half isn't a plain integer. Real Magic P/T is
     * sometimes non-numeric (characteristic-defining "*", "X/X", etc.); this deliberately returns
     * nullopt rather than defaulting to 0 like the delta-parser's own, different, purpose does, so
     * callers can leave such a creature out of automatic combat-damage calculation entirely
     * instead of silently mis-calculating it as a 0-power/0-toughness creature.
     */
    static std::optional<std::pair<int, int>> parseNumericPT(const QString &pt);

    /**
     * @brief The outcome of folding every applicable static/continuous ability into one
     * permanent's base characteristics -- see applyStaticEffects().
     */
    struct StaticEffectResult
    {
        int power = 0;
        int toughness = 0;
        QSet<QString> keywords;
    };

    /**
     * @brief Folds every static ability sourced from @p controllerBattlefield into @p targetCardId's
     * base power/toughness/keywords, and returns the result. @p controllerBattlefield is
     * `(cardId, staticAbilitiesString)` for every card the *same controller* as @p targetCardId
     * controls (see StaticAbilities::serialize()/AttrStaticAbilities in card_attributes.proto) --
     * scoping the list to one controller's battlefield is the caller's job, keeping this function
     * pure and callable without a live Server_Card, the same way calculateCombatDamage() takes
     * pre-scoped CombatAttack%s rather than zone objects.
     *
     * Each source's abilities are deserialized and applied in turn: a StaticAbilities::StaticScope::
     * OthersYours ability is skipped for its own source card (an anthem never buffs itself), while
     * an ::AllYours ability applies even to its own source (a creature that says "creatures you
     * control get +1/+1" buffs itself too). Every other source in @p controllerBattlefield always
     * applies to @p targetCardId regardless of scope, since "you control" is evaluated relative to
     * the source's controller, which the caller has already scoped this list to.
     */
    static StaticEffectResult applyStaticEffects(int targetCardId,
                                                 int basePower,
                                                 int baseToughness,
                                                 const QSet<QString> &baseKeywords,
                                                 const QList<QPair<int, QString>> &controllerBattlefield);

    /**
     * @brief Pure logic: the next player, in ascending-id turn order starting just after
     * @p currentPlayerId (wrapping around @p playerOrder), who is in neither @p passedPlayers nor
     * @p concededPlayers. Returns -1 if every eligible player has already passed.
     */
    static int nextPriorityPlayer(const QList<int> &playerOrder,
                                  int currentPlayerId,
                                  const QSet<int> &passedPlayers,
                                  const QSet<int> &concededPlayers);

    /**
     * @brief The names of the per-player counters that make up a mana pool (Server_Player::setupZones()'s
     * w/u/b/r/g/x counters). A mana pool empties at the end of every step and phase (rule 500.4) --
     * this list is what the server zeroes on every phase/step transition, for every player, not just
     * the active one.
     */
    static const QStringList &manaCounterNames();

    /**
     * @brief Phase 7 Stage 4: pure mana-payment planning. Given @p cost and a player's current
     * @p pool (keyed by the same w/u/b/r/g/x counter names as manaCounterNames(), values are the
     * current count of each), returns the exact per-counter-name deduction plan if @p pool can
     * afford @p cost, or std::nullopt if it can't. Colored pips are paid first, one-for-one, from
     * the matching color only (a shortfall in any single color fails the whole plan); the
     * remaining generic amount is then paid by draining whatever colors are left over, in
     * manaCounterNames()'s fixed order (w, u, b, r, g, x) -- a deterministic simplification of
     * real Magic's player-choice-driven generic payment (same "let the player pick, we don't
     * model the restriction" spirit as the Command Tower-style mana-production simplification,
     * just applied to which already-produced mana pays a generic cost -- bookkeeping only, not a
     * meaningful in-game decision under this fork's flat counter-based mana pool).
     */
    static std::optional<QMap<QString, int>> planManaPayment(const ManaCost &cost, const QMap<QString, int> &pool);

    /**
     * @brief Sub-step of planManaPayment(): pays only @p cost's colored pips (exact-color match,
     * one-for-one) and returns what's left of @p pool afterward -- std::nullopt if the colored
     * pips alone aren't affordable. Exposed separately so a caller can reason about what's left to
     * cover the generic remainder (see isGenericPaymentAmbiguous()/planManaPaymentWithGenericChoice()
     * below) without duplicating planManaPayment()'s own colored-pip loop.
     */
    static std::optional<QMap<QString, int>> remainingPoolAfterColoredPips(const ManaCost &cost,
                                                                           const QMap<QString, int> &pool);

    /**
     * @brief True iff, after paying @p cost's colored pips, more than one distinct valid way exists
     * to cover @p cost's generic remainder from what's left of @p pool -- i.e. a real color choice
     * exists for the player to make, as opposed to planManaPayment()'s own fixed w/u/b/r/g/x order
     * silently picking for them. Concretely: at least two colors have a nonzero remaining balance,
     * and their combined total strictly exceeds the generic amount needed (so some freedom exists
     * in which to use) -- if exactly one color remains, or every remaining color's mana is fully
     * required to exactly meet the generic amount, there's no real choice even though multiple
     * colors are nonzero, so this is false. Also false (not an error) when @p cost can't be paid at
     * all -- affordability is planManaPayment()'s job, not this function's.
     */
    static bool isGenericPaymentAmbiguous(const ManaCost &cost, const QMap<QString, int> &pool);

    /**
     * @brief Composes a full deduction plan from @p cost's colored pips (always paid in full, never
     * player-chosen) plus a caller-supplied @p genericChoice -- e.g. a split chosen via a picker
     * dialog once isGenericPaymentAmbiguous() is true. Validates @p genericChoice against
     * remainingPoolAfterColoredPips(): every entry must stay within that color's availability, and
     * the choice's total must exactly equal cost.generic. std::nullopt if invalid, matching
     * planManaPayment()'s own "whole plan fails, nothing partial" contract.
     */
    static std::optional<QMap<QString, int>> planManaPaymentWithGenericChoice(const ManaCost &cost,
                                                                              const QMap<QString, int> &pool,
                                                                              const QMap<QString, int> &genericChoice);

    /**
     * @brief One hybrid symbol's (phase6-mana.md addendum) two acceptable colors, plus whether a
     * real choice exists between them and which one a caller should default to if it doesn't ask.
     * @p ambiguous is true iff, given the pool and every pip resolved so far (see
     * planManaCostChoices()), both @p colorA and @p colorB currently have at least 1 mana
     * remaining -- same "only ask when a genuine choice exists" spirit as
     * isGenericPaymentAmbiguous(). @p defaultColor is always set (even when not ambiguous, where
     * it's the forced/only-affordable side) to whichever of the two comes first in
     * manaCounterNames() order when both are viable, matching planManaPayment()'s own deterministic
     * w/u/b/r/g/x tie-breaking.
     */
    struct HybridPipChoice
    {
        QString colorA;
        QString colorB;
        bool ambiguous = false;
        QString defaultColor;
    };

    /**
     * @brief One Phyrexian symbol's (phase6-mana.md addendum) color, plus whether paying it with
     * mana vs. 2 life is a real choice. @p ambiguous is true iff @p color still has at least 1 mana
     * remaining at this point in planManaCostChoices()'s pass -- if none is left, life is the only
     * option (@p defaultPayLife forced true), so there's nothing to ask. When ambiguous,
     * @p defaultPayLife is false: paying mana is the suggested default, same "don't touch life
     * unless needed" spirit as leaving it up to the caster to opt in.
     */
    struct PhyrexianPipChoice
    {
        QString color;
        bool ambiguous = false;
        bool defaultPayLife = false;
    };

    /** @brief planManaCostChoices()'s result: one entry per @p cost hybrid/Phyrexian pip, same order. */
    struct ManaCostChoices
    {
        QList<HybridPipChoice> hybridChoices;
        QList<PhyrexianPipChoice> phyrexianChoices;
    };

    /**
     * @brief Phase6-mana.md addendum: a single deterministic pass (not a constraint solver, same
     * simplification spirit as planManaPayment()'s own fixed-order generic draining) over
     * @p cost.hybridPips then @p cost.phyrexianPips, starting from
     * remainingPoolAfterColoredPips(cost, pool) and tentatively reserving each pip's default choice
     * before moving to the next, so a later pip sharing a color with an earlier one sees accurate
     * leftover availability. If @p cost is unaffordable regardless of choice, this still returns a
     * result (with arbitrary defaults) rather than nullopt -- affordability is planManaPayment()'s
     * job once resolveManaCost() folds the caller's actual choices in, not this function's.
     */
    static ManaCostChoices planManaCostChoices(const ManaCost &cost, const QMap<QString, int> &pool);

    /**
     * @brief Phase6-mana.md addendum: folds a caster's resolved choices for @p cost's hybrid/
     * Phyrexian/X components into a plain ManaCost (hybridPips/phyrexianPips empty, xCount 0) plus
     * a separate life cost that doesn't come from the mana pool at all -- purely mechanical, no
     * availability checking (that's planManaPayment()'s job on the returned cost). @p xValue is the
     * caster-announced X (folded into generic as `xCount * xValue`); @p hybridColorChoices and
     * @p phyrexianPayLifeChoices must have exactly as many entries, in the same order, as
     * @p cost.hybridPips / @p cost.phyrexianPips respectively -- each hybrid entry names the chosen
     * color (added to coloredPips), each Phyrexian entry is true to pay 2 life (added to the
     * returned lifeCost) or false to pay 1 of that pip's color (added to coloredPips).
     */
    struct ResolvedManaCost
    {
        ManaCost cost;
        int lifeCost = 0;
    };
    static ResolvedManaCost resolveManaCost(const ManaCost &cost,
                                            int xValue,
                                            const QList<QString> &hybridColorChoices,
                                            const QList<bool> &phyrexianPayLifeChoices);

    /**
     * @brief Rule 903.3: whether @p cardName is the deck's designated commander and should be
     * routed to the command zone instead of the deck/sideboard at setup. @p commanderName is the
     * deck's banner-card name (DeckList::getBannerCard().name); empty means the deck has no
     * commander, so nothing matches. Migrated verbatim from the inline check in
     * Server_Player::setupZones()'s insertCardsIntoZone lambda.
     */
    static bool isCommanderCard(const QString &cardName, const QString &commanderName);

    /**
     * @brief Rule 903.9: casting a commander from the command zone bumps its tax counter by one
     * (displayed count * 2 == the additional generic mana cost). Returns the counter name to
     * increment (CommanderCounterNames::tax(cardName)) if moving @p cardName from
     * @p startZoneName to @p targetZoneName is such a cast -- i.e. leaving the command zone to
     * somewhere that isn't the command zone again (a move straight back, e.g. an undo, isn't a
     * cast and doesn't count) -- or std::nullopt if this move isn't a commander cast. Migrated
     * verbatim from the inline zone-name check in Server_Player::onCardBeingMoved().
     */
    static std::optional<QString> commanderTaxCounterNameForMove(const QString &startZoneName,
                                                                 const QString &targetZoneName,
                                                                 const QString &cardName);

    // ---- Priority round state ----

    /** @brief The current priority holder, or -1 if no one holds priority. */
    int priorityHolder() const
    {
        return holder;
    }

    /**
     * @brief Starts a fresh priority round at @p playerId (clearing the passed-set). Used when a new
     * phase/step begins (round starts at the active player, rule 117.3b/117.3c simplified) or when a
     * spell/ability is put on the stack (round starts at whoever did it, rule 117.3d simplified).
     * Returns the new holder (== @p playerId).
     */
    int startPriorityRound(int playerId);

    /**
     * @brief Records that @p playerId passes priority and advances to the next eligible player in
     * turn order. If everyone eligible has now passed in succession, the round is exhausted: if the
     * pending-ability stack is non-empty, the top (most recently activated) ability is popped and
     * returned via the result's resolvedAbility for the caller to apply and re-open a fresh round
     * (rule 117.4 simplified -- see pushPendingAbility()); otherwise priority simply stops (holder
     * becomes -1) rather than auto-advancing the phase -- this fork is a manual "physical simulator"
     * (see CLAUDE.md), so phase changes are always a deliberate player action. @p playerOrder /
     * @p concededPlayers describe the current table.
     *
     * Returns {changed:false, holder:current} unchanged if @p playerId isn't the current holder
     * (defensive; the server also validates before calling).
     */
    PriorityPassResult passPriority(int playerId, const QList<int> &playerOrder, const QSet<int> &concededPlayers);

    /** @brief Clears priority to "no one" (holder = -1, empty passed-set). */
    void clearPriority();

    // ---- Pending-ability stack (Phase 7 Stage 3) ----

    /**
     * @brief Pushes @p ability onto the top of the pending-ability stack (LIFO -- the most recently
     * activated ability resolves first, matching rule 608.1/117.4 and naturally handling a chain of
     * responses one at a time). Does not itself start a priority round; the caller (Server_Game) is
     * expected to also call startPriorityRound(ability.controllerId), mirroring how a card moving
     * onto the (unrelated, purely visual) Stack zone already triggers a fresh round today.
     */
    void pushPendingAbility(const PendingAbility &ability);

    /** @brief True if any activated ability is awaiting resolution. */
    bool hasPendingAbilities() const
    {
        return !pendingAbilities.isEmpty();
    }

private:
    int holder = -1;
    QSet<int> passedBy;
    QList<PendingAbility> pendingAbilities;
};

} // namespace Rules

#endif // COCKATRICE_RULES_ENGINE_H
