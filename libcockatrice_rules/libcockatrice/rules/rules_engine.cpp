#include "rules_engine.h"

namespace Rules
{

PhaseAutomation RulesEngine::phaseAutomationFor(int phase, int turnNumber, int playerCount)
{
    // Phase indices match the client's phase order (cockatrice/src/game/phase.cpp:
    // Phases::phases[]) -- there's no shared server/client phase enum, so this is inherently
    // coupled to that ordering.
    constexpr int UNTAP_PHASE = 0;
    constexpr int DRAW_PHASE = 2;

    if (phase == UNTAP_PHASE) {
        return PhaseAutomation::UntapActivePlayer;
    }
    if (phase == DRAW_PHASE) {
        // Rule 103.8a/103.8c: only a strict two-player game's starting player skips their
        // first draw step; multiplayer games never skip it.
        bool skipFirstDraw = turnNumber == 1 && playerCount == 2;
        return skipFirstDraw ? PhaseAutomation::None : PhaseAutomation::DrawForActivePlayer;
    }
    return PhaseAutomation::None;
}

int RulesEngine::nextPriorityPlayer(const QList<int> &playerOrder,
                                    int currentPlayerId,
                                    const QSet<int> &passedPlayers,
                                    const QSet<int> &concededPlayers)
{
    const int n = playerOrder.size();
    if (n == 0) {
        return -1;
    }

    int startIndex = playerOrder.indexOf(currentPlayerId);
    for (int step = 1; step <= n; ++step) {
        int idx = (startIndex + step + n) % n;
        int candidate = playerOrder.at(idx);
        if (candidate == currentPlayerId || concededPlayers.contains(candidate) || passedPlayers.contains(candidate)) {
            continue;
        }
        return candidate;
    }
    return -1;
}

const QStringList &RulesEngine::manaCounterNames()
{
    static const QStringList names{"w", "u", "b", "r", "g", "x"};
    return names;
}

int RulesEngine::startPriorityRound(int playerId)
{
    passedBy.clear();
    holder = playerId;
    return holder;
}

PriorityPassResult
RulesEngine::passPriority(int playerId, const QList<int> &playerOrder, const QSet<int> &concededPlayers)
{
    if (playerId != holder) {
        return {false, holder}; // not the current holder -- no-op (server also validates)
    }

    passedBy.insert(playerId);

    int next = nextPriorityPlayer(playerOrder, playerId, passedBy, concededPlayers);
    if (next != -1) {
        holder = next;
        return {true, holder};
    }

    // Everyone eligible has passed in succession -- the round is exhausted. Priority simply stops
    // (rule 117.4 would resolve the top stack object here; this fork's Stack zone has no resolvable
    // objects). No one holds priority again until the next triggering event (a phase change, or a
    // spell/ability going on the stack). Deliberately does NOT auto-advance the phase/turn.
    passedBy.clear();
    holder = -1;
    return {true, holder};
}

void RulesEngine::clearPriority()
{
    passedBy.clear();
    holder = -1;
}

} // namespace Rules
