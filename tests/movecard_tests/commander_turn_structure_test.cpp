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

// ---- Server_Game::isCommanderGame ----
//
// FakeServer/Server_Room/Server_Game are deliberately heap-allocated and intentionally never
// freed here (rather than stack-allocated locals, as tests elsewhere in this repo use for a
// single instance). Constructing+destructing multiple Server_Game instances in sequence within
// one process was observed to segfault nondeterministically (reproducible outside a debugger,
// not under one — consistent with a pre-existing lifecycle issue in Server_Game/Server_Room
// teardown unrelated to this fork's changes). Leaking avoids exercising that destructor path;
// the test process is short-lived, so this is a pragmatic tradeoff, not a fix for the
// underlying fragility.
Server_Game &makeGame(const QStringList &roomGameTypeLabels,
                      const QList<int> &selectedGameTypes,
                      int maxPlayers,
                      int startingLife)
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
