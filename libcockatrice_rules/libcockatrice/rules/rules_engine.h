#ifndef COCKATRICE_RULES_ENGINE_H
#define COCKATRICE_RULES_ENGINE_H

#include <QList>
#include <QSet>
#include <QStringList>

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
 * @brief Result of RulesEngine::passPriority(): whether the pass changed anything, and the resulting
 * priority holder (-1 == no one).
 */
struct PriorityPassResult
{
    bool changed; ///< false if the pass was a no-op (the passing player didn't hold priority)
    int holder;   ///< the resulting priority holder after the pass; -1 means no one holds it
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
     * turn order. If everyone eligible has now passed in succession, the round is exhausted and
     * priority stops (holder becomes -1) rather than auto-advancing the phase -- this fork is a
     * manual "physical simulator" (see CLAUDE.md), so phase changes are always a deliberate player
     * action. @p playerOrder / @p concededPlayers describe the current table.
     *
     * Returns {changed:false, holder:current} unchanged if @p playerId isn't the current holder
     * (defensive; the server also validates before calling).
     */
    PriorityPassResult passPriority(int playerId, const QList<int> &playerOrder, const QSet<int> &concededPlayers);

    /** @brief Clears priority to "no one" (holder = -1, empty passed-set). */
    void clearPriority();

private:
    int holder = -1;
    QSet<int> passedBy;
};

} // namespace Rules

#endif // COCKATRICE_RULES_ENGINE_H
