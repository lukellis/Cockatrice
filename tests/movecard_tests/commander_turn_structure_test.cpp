#include "game/server_abstract_player.h"
#include "game/server_card.h"
#include "game/server_cardzone.h"
#include "game/server_game.h"
#include "game/server_player.h"
#include "server_response_containers.h"
#include "server_room.h"
#include "server_test_helpers.h"

#include <gtest/gtest.h>
#include <libcockatrice/card/ability/card_effects.h>
#include <libcockatrice/protocol/pb/card_attributes.pb.h>
#include <libcockatrice/protocol/pb/command_activate_ability.pb.h>
#include <libcockatrice/protocol/pb/command_pass_priority.pb.h>
#include <libcockatrice/protocol/pb/command_set_card_attr.pb.h>
#include <libcockatrice/protocol/pb/serverinfo_user.pb.h>
#include <libcockatrice/rng/rng_abstract.h>
#include <libcockatrice/utility/zone_names.h>

RNG_Abstract *rng = nullptr; // this needs to be defined due to other functions in server

// The pure rules logic (phase automation, priority-round state machine, and -- since Phase 7 Stage
// 3 -- the pending-ability stack's push/LIFO-resolve behavior) lives in libcockatrice_rules and is
// tested without any server dependency in tests/rules/rules_engine_test. This file covers only the
// server-side integration: the reused untap/draw mechanisms that Server_Game::setActivePhase()
// drives, cmdPassPriority's gating, and cmdActivateAbility's gating.

namespace
{

// FakeServer/Server_Room/Server_Game are deliberately heap-allocated and intentionally never
// freed here (rather than stack-allocated locals, as tests elsewhere in this repo use for a
// single instance). Constructing+destructing multiple Server_Game instances in sequence within
// one process was observed to segfault deterministically (reproducible outside a debugger, not
// under one). Root cause found: Server_Game::~Server_Game() calls deleteLater() on itself at
// the very end of its own destructor (server_game.cpp) — undefined behavior (posting a deferred
// self-deletion event for an object that's already being destructed), unrelated to this fork's
// changes and out of scope to fix here. Leaking avoids exercising that destructor path; the
// test process is short-lived, so this is a pragmatic tradeoff, not a fix for the underlying bug.
Server_Game &makeGame(int maxPlayers, int startingLife)
{
    static ServerInfo_User user = [] {
        ServerInfo_User u;
        u.set_name("test-user");
        return u;
    }();
    auto *server = new FakeServer();
    auto *room = new Server_Room(0, 0, "", "", "", "", false, "", {"Commander"}, server);
    auto *game = new Server_Game(user, 1, "", "", maxPlayers, {0}, false, false, false, false, false, false,
                                 startingLife, false, room);
    return *game;
}

// ---- Server_Player::cmdPassPriority ----
// Only the gating checks reachable without a started, participant-registered game are covered
// here (see makeGame()'s comment above for why registering real participants isn't lightweight
// in this test harness). Full pass -> advance flow is covered by the RulesEngine state-machine
// tests in tests/rules/ plus manual review of advancePriority()'s wiring.

TEST(CommanderTurnStructureTest, PassPriorityRejectedBeforeGameStarts)
{
    Server_Game &game = makeGame(4, 40);
    ServerInfo_User user;
    user.set_name("test-user");
    Server_Player player(&game, 1, user, false, nullptr);

    Command_PassPriority cmd;
    ResponseContainer rc(0);
    GameEventStorage ges;
    EXPECT_EQ(player.cmdPassPriority(cmd, rc, ges), Response::RespGameNotStarted);
}

// ---- Server_Player::cmdActivateAbility (Phase 7 Stage 3) ----
// Same limitation as cmdPassPriority above: only the gating check reachable without a started,
// participant-registered game is covered here. A deeper push -> exhaust -> resolve integration
// test (does the "life" counter actually change only after the round exhausts, not at push time)
// was attempted but isn't reachable from this lightweight harness either:
// Server_Game::advancePriority()/pushPendingAbility() both go through getPlayers()/getPlayer(),
// which read the game's `participants` map -- populated only by Server_Game::addPlayer(), which
// needs a live Server_AbstractUserInterface, the exact same pre-existing limitation documented for
// the untap/draw automation tests below. The full push/resolve/LIFO-ordering behavior is instead
// covered end-to-end at the pure-logic level in tests/rules/rules_engine_test.cpp, and exercised
// live per COMMANDER_IMPLEMENTATION_STATUS.md's Phase 7 Stage 3 verification section.

TEST(CommanderTurnStructureTest, ActivateAbilityRejectedBeforeGameStarts)
{
    Server_Game &game = makeGame(4, 40);
    ServerInfo_User user;
    user.set_name("test-user");
    Server_Player player(&game, 1, user, false, nullptr);

    Command_ActivateAbility cmd;
    cmd.set_effect_kind(static_cast<int>(EffectKind::GainLife));
    cmd.set_amount(3);
    ResponseContainer rc(0);
    GameEventStorage ges;
    EXPECT_EQ(player.cmdActivateAbility(cmd, rc, ges), Response::RespGameNotStarted);
}

// ---- Underlying mechanisms reused by the automatic untap/draw (setCardAttrHelper, drawCards) ----
// These exercise the exact calls Server_Game::setActivePhase() makes, against a manually
// constructed player+zones (matching the style of reverse_card_move_test.cpp), since there's
// no lightweight way to register a Server_Player as a real game participant in a unit test
// (Server_Game::addPlayer() requires a live Server_AbstractUserInterface).

TEST(CommanderTurnStructureTest, UntapAllUntapsEverythingExceptDoesntUntapCards)
{
    Server_Game &game = makeGame(1, 40);
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
    Server_Game &game = makeGame(1, 40);
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

// ---- Server_Card::setAttribute / Server_AbstractPlayer::cmdSetCardAttr (Phase 8 combat
// automation, Stage A: AttrAttackTarget / AttrBlocking) ----
// The phase/tapped/attacking legality RulesEngine::canDeclareBlocker() enforces is covered
// without a server dependency in tests/rules/rules_engine_test.cpp; this covers the two new
// attributes' own string (de)serialization on Server_Card, plus the same "before game starts"
// gating precedent as cmdPassPriority/cmdActivateAbility above.

TEST(CommanderTurnStructureTest, AttackTargetAttributeParsesPlayerId)
{
    Server_Game &game = makeGame(1, 40);
    ServerInfo_User user;
    user.set_name("test-user");
    Server_Player player(&game, 1, user, false, nullptr);
    Server_CardZone table(&player, ZoneNames::TABLE, true, ServerInfo_Zone::PublicZone);
    player.addZone(&table);

    auto *attacker = new Server_Card({"Attacker", "attacker"}, player.newCardId(), 0, 0, &table);
    table.insertCard(attacker, 0, 0);

    attacker->setAttribute(AttrAttackTarget, "2");
    EXPECT_EQ(attacker->getAttackTargetPlayerId(), 2);
}

TEST(CommanderTurnStructureTest, BlockingAttributeParsesPlayerAndCardIdAndClears)
{
    Server_Game &game = makeGame(1, 40);
    ServerInfo_User user;
    user.set_name("test-user");
    Server_Player player(&game, 1, user, false, nullptr);
    Server_CardZone table(&player, ZoneNames::TABLE, true, ServerInfo_Zone::PublicZone);
    player.addZone(&table);

    auto *blocker = new Server_Card({"Blocker", "blocker"}, player.newCardId(), 0, 0, &table);
    table.insertCard(blocker, 0, 0);

    blocker->setAttribute(AttrBlocking, "2:7");
    EXPECT_EQ(blocker->getBlockedPlayerId(), 2);
    EXPECT_EQ(blocker->getBlockedCardId(), 7);
    EXPECT_TRUE(blocker->getBlocking());

    blocker->setAttribute(AttrBlocking, "-1:-1");
    EXPECT_FALSE(blocker->getBlocking());
}

TEST(CommanderTurnStructureTest, SetCardAttrForCombatTargetingRejectedBeforeGameStarts)
{
    Server_Game &game = makeGame(4, 40);
    ServerInfo_User user;
    user.set_name("test-user");
    Server_Player player(&game, 1, user, false, nullptr);

    Command_SetCardAttr cmd;
    cmd.set_zone(ZoneNames::TABLE);
    cmd.set_card_id(0);
    cmd.set_attribute(AttrAttackTarget);
    cmd.set_attr_value("2");
    ResponseContainer rc(0);
    GameEventStorage ges;
    EXPECT_EQ(player.cmdSetCardAttr(cmd, rc, ges), Response::RespGameNotStarted);
}

} // namespace

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
