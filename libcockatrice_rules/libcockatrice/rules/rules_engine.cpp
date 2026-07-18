#include "rules_engine.h"

#include <algorithm>

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

std::optional<QMap<QString, int>> RulesEngine::planManaPayment(const ManaCost &cost, const QMap<QString, int> &pool)
{
    QMap<QString, int> remaining = pool;
    QMap<QString, int> plan;

    for (auto it = cost.coloredPips.constBegin(); it != cost.coloredPips.constEnd(); ++it) {
        const int available = remaining.value(it.key(), 0);
        if (available < it.value()) {
            return std::nullopt;
        }
        remaining[it.key()] = available - it.value();
        plan[it.key()] = plan.value(it.key(), 0) + it.value();
    }

    int genericRemaining = cost.generic;
    for (const QString &name : manaCounterNames()) {
        if (genericRemaining <= 0) {
            break;
        }
        const int available = remaining.value(name, 0);
        if (available <= 0) {
            continue;
        }
        const int used = std::min(available, genericRemaining);
        remaining[name] = available - used;
        plan[name] = plan.value(name, 0) + used;
        genericRemaining -= used;
    }

    if (genericRemaining > 0) {
        return std::nullopt;
    }
    return plan;
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
        return {true, holder, std::nullopt};
    }

    // Everyone eligible has passed in succession -- the round is exhausted. If something is
    // pending, rule 117.4's stack resolution: pop the top (most recently activated) ability and
    // hand it back to the caller to apply and re-open a fresh round. Otherwise priority simply
    // stops (holder becomes -1) until the next triggering event (a phase change, or a
    // spell/ability going on the stack). Deliberately does NOT auto-advance the phase/turn itself
    // either way.
    passedBy.clear();
    holder = -1;
    if (!pendingAbilities.isEmpty()) {
        PendingAbility resolved = pendingAbilities.takeLast();
        return {true, holder, resolved};
    }
    return {true, holder, std::nullopt};
}

void RulesEngine::clearPriority()
{
    passedBy.clear();
    holder = -1;
}

void RulesEngine::pushPendingAbility(const PendingAbility &ability)
{
    pendingAbilities.append(ability);
}

} // namespace Rules
