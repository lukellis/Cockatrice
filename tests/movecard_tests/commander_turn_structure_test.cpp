#include "game/server_abstract_player.h"
#include "game/server_card.h"
#include "game/server_cardzone.h"
#include "game/server_game.h"
#include "game/server_player.h"
#include "server_response_containers.h"
#include "server_room.h"
#include "server_test_helpers.h"

#include <gtest/gtest.h>
#include <libcockatrice/protocol/pb/card_attributes.pb.h>
#include <libcockatrice/protocol/pb/command_pass_priority.pb.h>
#include <libcockatrice/protocol/pb/serverinfo_user.pb.h>
#include <libcockatrice/rng/rng_abstract.h>
#include <libcockatrice/utility/zone_names.h>

RNG_Abstract *rng = nullptr; // this needs to be defined due to other functions in server

namespace
{

// ---- Server_Game::phaseAutomationFor (pure decision logic) ----

TEST(CommanderTurnStructureTest, UntapPhaseAlwaysUntaps)
{
    EXPECT_EQ(Server_Game::phaseAutomationFor(0, 1, 2), CommanderPhaseAutomation::UntapActivePlayer);
    EXPECT_EQ(Server_Game::phaseAutomationFor(0, 5, 4), CommanderPhaseAutomation::UntapActivePlayer);
}

TEST(CommanderTurnStructureTest, TwoPlayerGameSkipsFirstDrawStep)
{
    EXPECT_EQ(Server_Game::phaseAutomationFor(2, 1, 2), CommanderPhaseAutomation::None);
}

TEST(CommanderTurnStructureTest, TwoPlayerGameDrawsNormallyAfterFirstTurn)
{
    EXPECT_EQ(Server_Game::phaseAutomationFor(2, 2, 2), CommanderPhaseAutomation::DrawForActivePlayer);
    EXPECT_EQ(Server_Game::phaseAutomationFor(2, 3, 2), CommanderPhaseAutomation::DrawForActivePlayer);
}

TEST(CommanderTurnStructureTest, MultiplayerGameNeverSkipsFirstDrawStep)
{
    // Rule 103.8c: only strict two-player games skip the first draw step. Free-for-all
    // Commander (this fork's default) always draws, even on turn 1.
    EXPECT_EQ(Server_Game::phaseAutomationFor(2, 1, 3), CommanderPhaseAutomation::DrawForActivePlayer);
    EXPECT_EQ(Server_Game::phaseAutomationFor(2, 1, 4), CommanderPhaseAutomation::DrawForActivePlayer);
}

TEST(CommanderTurnStructureTest, OtherPhasesHaveNoAutomation)
{
    for (int phase : {1, 3, 4, 5, 6, 7, 8, 9, 10}) {
        EXPECT_EQ(Server_Game::phaseAutomationFor(phase, 1, 4), CommanderPhaseAutomation::None) << phase;
    }
}

// ---- Server_Game::nextPriorityPlayer (pure decision logic) ----

TEST(CommanderTurnStructureTest, NextPriorityPlayerAdvancesToNextInOrder)
{
    QList<int> order{1, 2, 3, 4};
    EXPECT_EQ(Server_Game::nextPriorityPlayer(order, 1, {}, {}), 2);
    EXPECT_EQ(Server_Game::nextPriorityPlayer(order, 2, {}, {}), 3);
}

TEST(CommanderTurnStructureTest, NextPriorityPlayerWrapsAroundTheTable)
{
    QList<int> order{1, 2, 3, 4};
    EXPECT_EQ(Server_Game::nextPriorityPlayer(order, 4, {}, {}), 1);
}

TEST(CommanderTurnStructureTest, NextPriorityPlayerSkipsPassedPlayers)
{
    QList<int> order{1, 2, 3, 4};
    // 2 already passed this round; 1 just passed too, so priority should skip to 3.
    EXPECT_EQ(Server_Game::nextPriorityPlayer(order, 1, {2}, {}), 3);
}

TEST(CommanderTurnStructureTest, NextPriorityPlayerSkipsConcededPlayers)
{
    QList<int> order{1, 2, 3, 4};
    EXPECT_EQ(Server_Game::nextPriorityPlayer(order, 1, {}, {2}), 3);
}

TEST(CommanderTurnStructureTest, NextPriorityPlayerReturnsNegativeOneWhenEveryoneElseIsIneligible)
{
    QList<int> order{1, 2, 3, 4};
    // Everyone but the passing player (1) has either passed or conceded.
    EXPECT_EQ(Server_Game::nextPriorityPlayer(order, 1, {3}, {2, 4}), -1);
}

TEST(CommanderTurnStructureTest, NextPriorityPlayerReturnsNegativeOneForSoloPlayer)
{
    QList<int> order{1};
    EXPECT_EQ(Server_Game::nextPriorityPlayer(order, 1, {}, {}), -1);
}

TEST(CommanderTurnStructureTest, NextPriorityPlayerReturnsNegativeOneForEmptyOrder)
{
    EXPECT_EQ(Server_Game::nextPriorityPlayer({}, 1, {}, {}), -1);
}

TEST(CommanderTurnStructureTest, NextPriorityPlayerNeverReturnsThePassingPlayerItself)
{
    QList<int> order{1, 2, 3};
    // 2 and 3 both conceded; only 1 (the one passing) remains "eligible" by the naive
    // pass/concede check, but must never be returned as its own next priority holder.
    EXPECT_EQ(Server_Game::nextPriorityPlayer(order, 1, {}, {2, 3}), -1);
}

// ---- Server_Game::isCommanderGame ----
//
// FakeServer/Server_Room/Server_Game are deliberately heap-allocated and intentionally never
// freed here (rather than stack-allocated locals, as tests elsewhere in this repo use for a
// single instance). Constructing+destructing multiple Server_Game instances in sequence within
// one process was observed to segfault deterministically (reproducible outside a debugger, not
// under one). Root cause found: Server_Game::~Server_Game() calls deleteLater() on itself at
// the very end of its own destructor (server_game.cpp) — undefined behavior (posting a deferred
// self-deletion event for an object that's already being destructed), unrelated to this fork's
// changes and out of scope to fix here. Leaking avoids exercising that destructor path; the
// test process is short-lived, so this is a pragmatic tradeoff, not a fix for the underlying bug.
Server_Game &
makeGame(const QStringList &roomGameTypeLabels, const QList<int> &selectedGameTypes, int maxPlayers, int startingLife)
{
    static ServerInfo_User user = [] {
        ServerInfo_User u;
        u.set_name("test-user");
        return u;
    }();
    auto *server = new FakeServer();
    auto *room = new Server_Room(0, 0, "", "", "", "", false, "", roomGameTypeLabels, server);
    auto *game = new Server_Game(user, 1, "", "", maxPlayers, selectedGameTypes, false, false, false, false, false,
                                 false, startingLife, false, room);
    return *game;
}

// ---- Server_Player::cmdPassPriority ----
// Only the gating checks reachable without a started, participant-registered game are covered
// here (see makeGame()'s comment above for why registering real participants isn't lightweight
// in this test harness). Full pass -> advance flow is covered indirectly via nextPriorityPlayer()
// above plus manual code review of advancePriority()'s wiring.

TEST(CommanderTurnStructureTest, PassPriorityRejectedBeforeGameStarts)
{
    Server_Game &game = makeGame({"Commander"}, {0}, 4, 40);
    ServerInfo_User user;
    user.set_name("test-user");
    Server_Player player(&game, 1, user, false, nullptr);

    Command_PassPriority cmd;
    ResponseContainer rc(0);
    GameEventStorage ges;
    EXPECT_EQ(player.cmdPassPriority(cmd, rc, ges), Response::RespGameNotStarted);
}

TEST(CommanderTurnStructureTest, IsCommanderGameTrueWhenSelectedGameTypeIsCommander)
{
    Server_Game &game = makeGame({"Standard", "Commander"}, {1}, 4, 40);
    EXPECT_TRUE(game.isCommanderGame());
}

TEST(CommanderTurnStructureTest, IsCommanderGameFalseWhenSelectedGameTypeIsNotCommander)
{
    Server_Game &game = makeGame({"Standard", "Commander"}, {0}, 2, 20);
    EXPECT_FALSE(game.isCommanderGame());
}

TEST(CommanderTurnStructureTest, IsCommanderGameFalseWhenNoGameTypesSelected)
{
    Server_Game &game = makeGame({"Standard", "Commander"}, {}, 2, 20);
    EXPECT_FALSE(game.isCommanderGame());
}

TEST(CommanderTurnStructureTest, IsCommanderGameIgnoresOutOfRangeGameTypeIndexWithoutCrashing)
{
    // A stale/out-of-range game type index (e.g. room config changed after the game was
    // created) must not crash isCommanderGame() or be misread as a match.
    Server_Game &game = makeGame({"Standard"}, {5}, 2, 20);
    EXPECT_FALSE(game.isCommanderGame());
}

TEST(CommanderTurnStructureTest, IsCommanderGameMatchesSubstringLabel)
{
    Server_Game &game = makeGame({"Commander (1v1)"}, {0}, 2, 20);
    EXPECT_TRUE(game.isCommanderGame());
}

// ---- Underlying mechanisms reused by the automatic untap/draw (setCardAttrHelper, drawCards) ----
// These exercise the exact calls Server_Game::setActivePhase() makes, against a manually
// constructed player+zones (matching the style of reverse_card_move_test.cpp), since there's
// no lightweight way to register a Server_Player as a real game participant in a unit test
// (Server_Game::addPlayer() requires a live Server_AbstractUserInterface).

TEST(CommanderTurnStructureTest, UntapAllUntapsEverythingExceptDoesntUntapCards)
{
    Server_Game &game = makeGame({"Commander"}, {0}, 1, 40);
    ServerInfo_User user;
    user.set_name("test-user");
    Server_AbstractPlayer player(&game, 1, user, false, nullptr);
    Server_CardZone table(&player, ZoneNames::TABLE, true, ServerInfo_Zone::PublicZone);
    player.addZone(&table);

    auto *normalCard = new Server_Card({"Normal Permanent", "normal"}, player.newCardId(), 0, 0, &table);
    normalCard->setAttribute(AttrTapped, "1");
    table.insertCard(normalCard, 0, 0);

    auto *sticky = new Server_Card({"Stays Tapped Permanent", "sticky"}, player.newCardId(), 1, 0, &table);
    sticky->setAttribute(AttrDoesntUntap, "1");
    sticky->setAttribute(AttrTapped, "1");
    table.insertCard(sticky, 1, 0);

    GameEventStorage ges;
    auto response = player.setCardAttrHelper(ges, player.getPlayerId(), ZoneNames::TABLE, -1, AttrTapped, "0");

    EXPECT_EQ(response, Response::RespOk);
    EXPECT_FALSE(normalCard->getTapped());
    EXPECT_TRUE(sticky->getTapped());
}

TEST(CommanderTurnStructureTest, DrawCardsMovesTopOfDeckToHand)
{
    Server_Game &game = makeGame({"Commander"}, {0}, 1, 40);
    ServerInfo_User user;
    user.set_name("test-user");
    Server_Player player(&game, 1, user, false, nullptr);
    Server_CardZone deckZone(&player, ZoneNames::DECK, true, ServerInfo_Zone::HiddenZone);
    Server_CardZone handZone(&player, ZoneNames::HAND, false, ServerInfo_Zone::PrivateZone);
    player.addZone(&deckZone);
    player.addZone(&handZone);

    deckZone.insertCard(new Server_Card({"Top Card", "top"}, player.newCardId(), 0, 0, &deckZone), 0, 0);
    deckZone.insertCard(new Server_Card({"Bottom Card", "bottom"}, player.newCardId(), 1, 0, &deckZone), 1, 0);

    GameEventStorage ges;
    auto response = player.drawCards(ges, 1);

    EXPECT_EQ(response, Response::RespOk);
    EXPECT_EQ(handZone.getCards().size(), 1);
    EXPECT_EQ(deckZone.getCards().size(), 1);
}

} // namespace

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
