#include "libcockatrice/rules/rules_engine.h"

#include <gtest/gtest.h>

using Rules::PhaseAutomation;
using Rules::PriorityPassResult;
using Rules::RulesEngine;

namespace
{

// ---- RulesEngine::phaseAutomationFor (pure decision logic) ----

TEST(RulesEngineTest, UntapPhaseAlwaysUntaps)
{
    EXPECT_EQ(RulesEngine::phaseAutomationFor(0, 1, 2), PhaseAutomation::UntapActivePlayer);
    EXPECT_EQ(RulesEngine::phaseAutomationFor(0, 5, 4), PhaseAutomation::UntapActivePlayer);
}

TEST(RulesEngineTest, TwoPlayerGameSkipsFirstDrawStep)
{
    EXPECT_EQ(RulesEngine::phaseAutomationFor(2, 1, 2), PhaseAutomation::None);
}

TEST(RulesEngineTest, TwoPlayerGameDrawsNormallyAfterFirstTurn)
{
    EXPECT_EQ(RulesEngine::phaseAutomationFor(2, 2, 2), PhaseAutomation::DrawForActivePlayer);
    EXPECT_EQ(RulesEngine::phaseAutomationFor(2, 3, 2), PhaseAutomation::DrawForActivePlayer);
}

TEST(RulesEngineTest, MultiplayerGameNeverSkipsFirstDrawStep)
{
    // Rule 103.8c: only strict two-player games skip the first draw step. Free-for-all
    // Commander (this fork's default) always draws, even on turn 1.
    EXPECT_EQ(RulesEngine::phaseAutomationFor(2, 1, 3), PhaseAutomation::DrawForActivePlayer);
    EXPECT_EQ(RulesEngine::phaseAutomationFor(2, 1, 4), PhaseAutomation::DrawForActivePlayer);
}

TEST(RulesEngineTest, OtherPhasesHaveNoAutomation)
{
    for (int phase : {1, 3, 4, 5, 6, 7, 8, 9, 10}) {
        EXPECT_EQ(RulesEngine::phaseAutomationFor(phase, 1, 4), PhaseAutomation::None) << phase;
    }
}

TEST(RulesEngineTest, ManaCounterNamesCoversTheFiveColorsPlusColorless)
{
    QStringList names = RulesEngine::manaCounterNames();
    EXPECT_EQ(names.size(), 6);
    for (const QString &color : {"w", "u", "b", "r", "g", "x"}) {
        EXPECT_TRUE(names.contains(color)) << color.toStdString();
    }
}

// ---- RulesEngine::nextPriorityPlayer (pure decision logic) ----

TEST(RulesEngineTest, NextPriorityPlayerAdvancesToNextInOrder)
{
    QList<int> order{1, 2, 3, 4};
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 1, {}, {}), 2);
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 2, {}, {}), 3);
}

TEST(RulesEngineTest, NextPriorityPlayerWrapsAroundTheTable)
{
    QList<int> order{1, 2, 3, 4};
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 4, {}, {}), 1);
}

TEST(RulesEngineTest, NextPriorityPlayerSkipsPassedPlayers)
{
    QList<int> order{1, 2, 3, 4};
    // 2 already passed this round; 1 just passed too, so priority should skip to 3.
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 1, {2}, {}), 3);
}

TEST(RulesEngineTest, NextPriorityPlayerSkipsConcededPlayers)
{
    QList<int> order{1, 2, 3, 4};
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 1, {}, {2}), 3);
}

TEST(RulesEngineTest, NextPriorityPlayerReturnsNegativeOneWhenEveryoneElseIsIneligible)
{
    QList<int> order{1, 2, 3, 4};
    // Everyone but the passing player (1) has either passed or conceded.
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 1, {3}, {2, 4}), -1);
}

TEST(RulesEngineTest, NextPriorityPlayerReturnsNegativeOneForSoloPlayer)
{
    QList<int> order{1};
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 1, {}, {}), -1);
}

TEST(RulesEngineTest, NextPriorityPlayerReturnsNegativeOneForEmptyOrder)
{
    EXPECT_EQ(RulesEngine::nextPriorityPlayer({}, 1, {}, {}), -1);
}

TEST(RulesEngineTest, NextPriorityPlayerNeverReturnsThePassingPlayerItself)
{
    QList<int> order{1, 2, 3};
    // 2 and 3 both conceded; only 1 (the one passing) remains "eligible" by the naive
    // pass/concede check, but must never be returned as its own next priority holder.
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 1, {}, {2, 3}), -1);
}

// ---- RulesEngine priority-round state machine (newly testable end-to-end) ----

TEST(RulesEngineTest, StartsWithNoOneHoldingPriority)
{
    RulesEngine engine;
    EXPECT_EQ(engine.priorityHolder(), -1);
}

TEST(RulesEngineTest, StartPriorityRoundSetsTheHolder)
{
    RulesEngine engine;
    EXPECT_EQ(engine.startPriorityRound(2), 2);
    EXPECT_EQ(engine.priorityHolder(), 2);
}

TEST(RulesEngineTest, PassPriorityAdvancesAroundTheTable)
{
    RulesEngine engine;
    QList<int> order{1, 2, 3, 4};
    engine.startPriorityRound(1);

    PriorityPassResult r = engine.passPriority(1, order, {});
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(r.holder, 2);
    EXPECT_EQ(engine.priorityHolder(), 2);

    r = engine.passPriority(2, order, {});
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(r.holder, 3);
}

TEST(RulesEngineTest, PassPriorityStopsTheRoundWhenEveryoneHasPassed)
{
    RulesEngine engine;
    QList<int> order{1, 2};
    engine.startPriorityRound(1);

    EXPECT_EQ(engine.passPriority(1, order, {}).holder, 2);   // 1 passes -> 2
    PriorityPassResult r = engine.passPriority(2, order, {}); // 2 passes -> round exhausted
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(r.holder, -1);
    EXPECT_EQ(engine.priorityHolder(), -1);
}

TEST(RulesEngineTest, SoloPlayerPassingStopsTheRoundImmediately)
{
    // The solo/last-player-standing case that previously caused an auto-pass infinite loop:
    // passing hands priority to no one, not back to yourself.
    RulesEngine engine;
    QList<int> order{1};
    engine.startPriorityRound(1);

    PriorityPassResult r = engine.passPriority(1, order, {});
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(r.holder, -1);
}

TEST(RulesEngineTest, PassPriorityFromANonHolderIsANoOp)
{
    RulesEngine engine;
    QList<int> order{1, 2, 3};
    engine.startPriorityRound(1);

    PriorityPassResult r = engine.passPriority(2, order, {}); // 2 doesn't hold priority
    EXPECT_FALSE(r.changed);
    EXPECT_EQ(r.holder, 1);
    EXPECT_EQ(engine.priorityHolder(), 1);
}

TEST(RulesEngineTest, StartingANewRoundClearsPreviousPasses)
{
    RulesEngine engine;
    QList<int> order{1, 2, 3};
    engine.startPriorityRound(1);
    engine.passPriority(1, order, {}); // now 2 holds, 1 has passed

    // A new trigger (phase change / spell) restarts at 3; 1's earlier pass must not carry over.
    engine.startPriorityRound(3);
    EXPECT_EQ(engine.passPriority(3, order, {}).holder, 1); // 1 is eligible again
}

TEST(RulesEngineTest, ClearPriorityResetsToNoOne)
{
    RulesEngine engine;
    engine.startPriorityRound(2);
    engine.clearPriority();
    EXPECT_EQ(engine.priorityHolder(), -1);
}

} // namespace

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
