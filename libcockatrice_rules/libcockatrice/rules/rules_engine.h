#ifndef COCKATRICE_RULES_ENGINE_H
#define COCKATRICE_RULES_ENGINE_H

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <libcockatrice/card/ability/card_effects.h>
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
