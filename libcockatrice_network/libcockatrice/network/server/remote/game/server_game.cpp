/***************************************************************************
 *   Copyright (C) 2008 by Max-Wilhelm Bruker   *
 *   brukie@laptop   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "server_game.h"

#include "../server.h"
#include "../server_database_interface.h"
#include "../server_protocolhandler.h"
#include "../server_room.h"
#include "libcockatrice/protocol/pb/command_move_card.pb.h"
#include "server_abstract_player.h"
#include "server_arrow.h"
#include "server_card.h"
#include "server_cardzone.h"
#include "server_counter.h"
#include "server_player.h"
#include "server_spectator.h"

#include <QDebug>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>
#include <google/protobuf/descriptor.h>
#include <libcockatrice/card/ability/card_effects.h>
#include <libcockatrice/deck_list/deck_list.h>
#include <libcockatrice/protocol/pb/card_attributes.pb.h>
#include <libcockatrice/protocol/pb/context_connection_state_changed.pb.h>
#include <libcockatrice/protocol/pb/context_ping_changed.pb.h>
#include <libcockatrice/protocol/pb/event_ability_resolved.pb.h>
#include <libcockatrice/protocol/pb/event_delete_arrow.pb.h>
#include <libcockatrice/protocol/pb/event_game_closed.pb.h>
#include <libcockatrice/protocol/pb/event_game_host_changed.pb.h>
#include <libcockatrice/protocol/pb/event_game_joined.pb.h>
#include <libcockatrice/protocol/pb/event_game_state_changed.pb.h>
#include <libcockatrice/protocol/pb/event_join.pb.h>
#include <libcockatrice/protocol/pb/event_kicked.pb.h>
#include <libcockatrice/protocol/pb/event_leave.pb.h>
#include <libcockatrice/protocol/pb/event_player_properties_changed.pb.h>
#include <libcockatrice/protocol/pb/event_priority_changed.pb.h>
#include <libcockatrice/protocol/pb/event_replay_added.pb.h>
#include <libcockatrice/protocol/pb/event_set_active_phase.pb.h>
#include <libcockatrice/protocol/pb/event_set_active_player.pb.h>
#include <libcockatrice/protocol/pb/event_set_card_counter.pb.h>
#include <libcockatrice/protocol/pb/event_set_counter.pb.h>
#include <libcockatrice/protocol/pb/game_replay.pb.h>
#include <libcockatrice/rules/commander_counter_names.h>
#include <libcockatrice/utility/color.h>
#include <libcockatrice/utility/zone_names.h>

Server_Game::Server_Game(const ServerInfo_User &_creatorInfo,
                         int _gameId,
                         const QString &_description,
                         const QString &_password,
                         int _maxPlayers,
                         const QList<int> &_gameTypes,
                         bool _onlyBuddies,
                         bool _onlyRegistered,
                         bool _spectatorsAllowed,
                         bool _spectatorsNeedPassword,
                         bool _spectatorsCanTalk,
                         bool _spectatorsSeeEverything,
                         int _startingLifeTotal,
                         bool _shareDecklistsOnLoad,
                         Server_Room *_room)
    : QObject(), room(_room), nextPlayerId(0), hostId(0), creatorInfo(new ServerInfo_User(_creatorInfo)),
      gameStarted(false), gameClosed(false), gameId(_gameId), password(_password), maxPlayers(_maxPlayers),
      gameTypes(_gameTypes), activePlayer(-1), activePhase(-1), onlyBuddies(_onlyBuddies),
      onlyRegistered(_onlyRegistered), spectatorsAllowed(_spectatorsAllowed),
      spectatorsNeedPassword(_spectatorsNeedPassword), spectatorsCanTalk(_spectatorsCanTalk),
      spectatorsSeeEverything(_spectatorsSeeEverything), startingLifeTotal(_startingLifeTotal),
      shareDecklistsOnLoad(_shareDecklistsOnLoad), inactivityCounter(0), startTimeOfThisGame(0), secondsElapsed(0),
      firstGameStarted(false), turnOrderReversed(false), startTime(QDateTime::currentDateTime()), pingClock(nullptr),
      gameMutex()
{
    currentReplay = new GameReplay;
    currentReplay->set_replay_id(room->getServer()->getDatabaseInterface()->getNextReplayId());
    description = _description.simplified();

    connect(this, &Server_Game::sigStartGameIfReady, this, &Server_Game::doStartGameIfReady, Qt::QueuedConnection);

    getInfo(*currentReplay->mutable_game_info());

    if (room->getServer()->getGameShouldPing()) {
        pingClock = new QTimer(this);
        connect(pingClock, &QTimer::timeout, this, &Server_Game::pingClockTimeout);
        pingClock->start(1000);
    }
}

Server_Game::~Server_Game()
{
    room->gamesLock.lockForWrite();
    gameMutex.lock();

    gameClosed = true;
    sendGameEventContainer(prepareGameEvent(Event_GameClosed(), -1));
    for (auto *participant : participants.values()) {
        participant->prepareDestroy();
    }
    participants.clear();

    room->removeGame(this);
    delete creatorInfo;
    creatorInfo = 0;

    gameMutex.unlock();
    room->gamesLock.unlock();
    currentReplay->set_duration_seconds(secondsElapsed - startTimeOfThisGame);
    replayList.append(currentReplay);
    storeGameInformation();

    for (auto *replay : replayList) {
        delete replay;
    }
    replayList.clear();

    room = nullptr;
    currentReplay = nullptr;
    creatorInfo = nullptr;

    if (pingClock) {
        delete pingClock;
        pingClock = nullptr;
    }

    qDebug() << "Server_Game destructor: gameId=" << gameId;
    deleteLater();
}

void Server_Game::storeGameInformation()
{
    const ServerInfo_Game &gameInfo = replayList.first()->game_info();

    Event_ReplayAdded replayEvent;
    ServerInfo_ReplayMatch *replayMatchInfo = replayEvent.mutable_match_info();
    replayMatchInfo->set_game_id(gameInfo.game_id());
    replayMatchInfo->set_room_name(room->getName().toStdString());
    replayMatchInfo->set_time_started(QDateTime::currentDateTime().addSecs(-secondsElapsed).toSecsSinceEpoch());
    replayMatchInfo->set_length(secondsElapsed);
    replayMatchInfo->set_game_name(gameInfo.description());

    const QStringList &allGameTypes = room->getGameTypes();
    QStringList _gameTypes;
    for (int i = gameInfo.game_types_size() - 1; i >= 0; --i) {
        _gameTypes.append(allGameTypes[gameInfo.game_types(i)]);
    }

    for (const auto &playerName : allPlayersEver) {
        replayMatchInfo->add_player_names(playerName.toStdString());
    }

    for (int i = 0; i < replayList.size(); ++i) {
        ServerInfo_Replay *replayInfo = replayMatchInfo->add_replay_list();
        replayInfo->set_replay_id(replayList[i]->replay_id());
        replayInfo->set_replay_name(gameInfo.description());
        replayInfo->set_duration(replayList[i]->duration_seconds());
    }

    SessionEvent *sessionEvent = Server_ProtocolHandler::prepareSessionEvent(replayEvent);
    Server *server = room->getServer();
    server->clientsLock.lockForRead();
    for (auto userName : allPlayersEver + allSpectatorsEver) {
        Server_AbstractUserInterface *userHandler = server->findUser(userName);
        if (userHandler && server->getStoreReplaysEnabled()) {
            userHandler->sendProtocolItem(*sessionEvent);
        }
    }
    server->clientsLock.unlock();
    delete sessionEvent;

    if (server->getStoreReplaysEnabled()) {
        server->getDatabaseInterface()->storeGameInformation(room->getName(), _gameTypes, gameInfo, allPlayersEver,
                                                             allSpectatorsEver, replayList);
    }
}

void Server_Game::pingClockTimeout()
{
    QMutexLocker locker(&gameMutex);
    ++secondsElapsed;

    GameEventStorage ges;
    ges.setGameEventContext(Context_PingChanged());

    bool allPlayersInactive = true;
    int playerCount = 0;
    for (auto *participant : participants) {
        if (participant == nullptr) {
            continue;
        }

        if (!participant->isSpectator()) {
            ++playerCount;
        }

        if (participant->updatePingTime()) {
            Event_PlayerPropertiesChanged event;
            event.mutable_player_properties()->set_ping_seconds(participant->getPingTime());
            ges.enqueueGameEvent(event, participant->getPlayerId());
        }

        if ((participant->getPingTime() != -1) &&
            (!participant->isSpectator() || participant->getPlayerId() == hostId)) {
            allPlayersInactive = false;
        }
    }
    ges.sendToGame(this);

    const int maxTime = room->getServer()->getMaxGameInactivityTime();
    if (allPlayersInactive) {
        if (((maxTime > 0) && (++inactivityCounter >= maxTime)) || (playerCount < maxPlayers)) {
            deleteLater();
        }
    } else {
        inactivityCounter = 0;
    }
}

QMap<int, Server_AbstractPlayer *> Server_Game::getPlayers() const // copies pointers to new map
{
    QMap<int, Server_AbstractPlayer *> players;
    QMutexLocker locker(&gameMutex);
    for (int id : participants.keys()) {
        auto *participant = participants[id];
        if (!participant->isSpectator()) {
            players[id] = static_cast<Server_AbstractPlayer *>(participant);
        }
    }
    return players;
}

Server_AbstractPlayer *Server_Game::getPlayer(int id) const
{
    auto *participant = participants.value(id);
    if (participant && !participant->isSpectator()) {
        return static_cast<Server_AbstractPlayer *>(participant);
    } else {
        return nullptr;
    }
}

int Server_Game::getPlayerCount() const
{
    return participants.size() - getSpectatorCount();
}

int Server_Game::getSpectatorCount() const
{
    QMutexLocker locker(&gameMutex);

    int result = 0;
    for (Server_AbstractParticipant *participant : participants.values()) {
        if (participant->isSpectator()) {
            ++result;
        }
    }
    return result;
}

void Server_Game::createGameStateChangedEvent(Event_GameStateChanged *event,
                                              Server_AbstractParticipant *recipient,
                                              bool omniscient,
                                              bool withUserInfo)
{
    event->set_seconds_elapsed(secondsElapsed);
    if (gameStarted) {
        event->set_game_started(true);
        event->set_active_player_id(0);
        event->set_active_phase(0);
    } else {
        event->set_game_started(false);
    }

    for (Server_AbstractParticipant *participant : participants.values()) {
        participant->getInfo(event->add_player_list(), recipient, omniscient, withUserInfo);
    }
}

void Server_Game::sendGameStateToPlayers()
{
    // game state information for replay and omniscient spectators
    Event_GameStateChanged omniscientEvent;
    createGameStateChangedEvent(&omniscientEvent, nullptr, true, false);

    GameEventContainer *replayCont = prepareGameEvent(omniscientEvent, -1);
    replayCont->set_seconds_elapsed(secondsElapsed - startTimeOfThisGame);
    replayCont->clear_game_id();
    currentReplay->add_event_list()->CopyFrom(*replayCont);
    delete replayCont;

    // If spectators are not omniscient, we need an additional createGameStateChangedEvent call, otherwise we can use
    // the data we used for the replay. All spectators are equal, so we don't need to make a createGameStateChangedEvent
    // call for each one.
    Event_GameStateChanged spectatorNormalEvent;
    createGameStateChangedEvent(&spectatorNormalEvent, nullptr, false, false);

    // send game state info to clients according to their role in the game
    for (auto *participant : participants.values()) {
        GameEventContainer *gec;
        if (participant->isSpectator()) {
            if (spectatorsSeeEverything || participant->isJudge()) {
                gec = prepareGameEvent(omniscientEvent, -1);
            } else {
                gec = prepareGameEvent(spectatorNormalEvent, -1);
            }
        } else {
            Event_GameStateChanged event;
            createGameStateChangedEvent(&event, participant, participant->isJudge(), false);

            gec = prepareGameEvent(event, -1);
        }
        participant->sendGameEvent(*gec);
        delete gec;
    }
}

void Server_Game::doStartGameIfReady(bool forceStartGame)
{
    Server_DatabaseInterface *databaseInterface = room->getServer()->getDatabaseInterface();
    QMutexLocker locker(&gameMutex);

    if (getPlayerCount() < maxPlayers && !forceStartGame) {
        return;
    }

    auto players = getPlayers();
    for (auto *player : players.values()) {
        if (!player->getReadyStart()) {
            if (forceStartGame) {
                // Player is not ready to start, so kick them
                //! \todo Move them to Spectators instead.
                kickParticipant(player->getPlayerId());
            } else {
                return;
            }
        }
    }

    players = getPlayers(); // players could have been kicked, get new list of players
    for (Server_AbstractPlayer *player : players.values()) {
        player->setupZones();
    }

    // Commander damage: every player gets a counter tracking damage received from each other
    // player's commander(s) (rule 704.5g - 21 damage from a single commander is a loss).
    // Zones are set up above, so every player's command zone is now populated.
    for (Server_AbstractPlayer *defender : players.values()) {
        auto *defenderPlayer = dynamic_cast<Server_Player *>(defender);
        if (!defenderPlayer) {
            continue;
        }
        for (Server_AbstractPlayer *attacker : players.values()) {
            if (attacker == defender) {
                continue;
            }
            Server_CardZone *attackerCommandZone = attacker->getZones().value(ZoneNames::COMMAND);
            if (!attackerCommandZone) {
                continue;
            }
            for (Server_Card *commander : attackerCommandZone->getCards()) {
                defenderPlayer->addCounter(new Server_Counter(defenderPlayer->newCounterId(),
                                                              CommanderCounterNames::damage(commander->getName()),
                                                              makeColor(200, 40, 40), 15, 0));
            }
        }
    }

    gameStarted = true;
    for (auto *player : players.values()) {
        player->setConceded(false);
        player->setReadyStart(false);
    }

    if (firstGameStarted) {
        currentReplay->set_duration_seconds(secondsElapsed - startTimeOfThisGame);
        replayList.append(currentReplay);
        currentReplay = new GameReplay;
        currentReplay->set_replay_id(databaseInterface->getNextReplayId());
        ServerInfo_Game *gameInfo = currentReplay->mutable_game_info();
        getInfo(*gameInfo);
        gameInfo->set_started(false);

        Event_GameStateChanged omniscientEvent;
        createGameStateChangedEvent(&omniscientEvent, nullptr, true, true);

        GameEventContainer *replayCont = prepareGameEvent(omniscientEvent, -1);
        replayCont->set_seconds_elapsed(0);
        replayCont->clear_game_id();
        currentReplay->add_event_list()->CopyFrom(*replayCont);
        delete replayCont;

        startTimeOfThisGame = secondsElapsed;
    } else {
        firstGameStarted = true;
    }

    sendGameStateToPlayers();

    activePlayer = -1;
    nextTurn();

    locker.unlock();

    ServerInfo_Game gameInfo;
    gameInfo.set_room_id(room->getId());
    gameInfo.set_game_id(gameId);
    gameInfo.set_started(true);
    emit gameInfoChanged(gameInfo);
}

void Server_Game::startGameIfReady(bool forceStartGame)
{
    emit sigStartGameIfReady(forceStartGame);
}

void Server_Game::stopGameIfFinished()
{
    QMutexLocker locker(&gameMutex);

    int playing = 0;
    auto players = getPlayers();
    for (auto *player : players.values()) {
        if (!player->getConceded()) {
            ++playing;
        }
    }
    if (playing > 1) {
        return;
    }

    gameStarted = false;

    for (auto *player : players.values()) {
        player->clearZones();
        player->setConceded(false);
    }

    sendGameStateToPlayers();

    locker.unlock();

    ServerInfo_Game gameInfo;
    gameInfo.set_room_id(room->getId());
    gameInfo.set_game_id(gameId);
    gameInfo.set_started(false);
    emit gameInfoChanged(gameInfo);
}

Response::ResponseCode Server_Game::checkJoin(ServerInfo_User *user,
                                              const QString &_password,
                                              bool spectator,
                                              bool overrideRestrictions,
                                              bool asJudge)
{
    Server_DatabaseInterface *databaseInterface = room->getServer()->getDatabaseInterface();
    for (auto *participant : participants.values()) {
        if (participant->getUserInfo()->name() == user->name()) {
            return Response::RespContextError;
        }
    }

    if (asJudge && !(user->user_level() & ServerInfo_User::IsJudge)) {
        return Response::RespUserLevelTooLow;
    }
    if (!(overrideRestrictions && (user->user_level() & ServerInfo_User::IsModerator))) {
        if ((_password != password) && !(spectator && !spectatorsNeedPassword)) {
            return Response::RespWrongPassword;
        }
        if (!(user->user_level() & ServerInfo_User::IsRegistered) && onlyRegistered) {
            return Response::RespUserLevelTooLow;
        }
        if (onlyBuddies && (user->name() != creatorInfo->name())) {
            if (!databaseInterface->isInBuddyList(QString::fromStdString(creatorInfo->name()),
                                                  QString::fromStdString(user->name()))) {
                return Response::RespOnlyBuddies;
            }
        }
        if (databaseInterface->isInIgnoreList(QString::fromStdString(creatorInfo->name()),
                                              QString::fromStdString(user->name()))) {
            return Response::RespInIgnoreList;
        }
        if (spectator) {
            if (!spectatorsAllowed) {
                return Response::RespSpectatorsNotAllowed;
            }
        }
    }
    if (!spectator && (gameStarted || (getPlayerCount() >= getMaxPlayers()))) {
        return Response::RespGameFull;
    }

    return Response::RespOk;
}

bool Server_Game::containsUser(const QString &userName) const
{
    QMutexLocker locker(&gameMutex);

    for (auto *participant : participants.values()) {
        if (participant->getUserInfo()->name() == userName.toStdString()) {
            return true;
        }
    }
    return false;
}

void Server_Game::addPlayer(Server_AbstractUserInterface *userInterface,
                            ResponseContainer &rc,
                            bool spectator,
                            bool judge,
                            bool broadcastUpdate)
{
    QMutexLocker locker(&gameMutex);

    Server_AbstractParticipant *newParticipant;
    if (spectator) {
        newParticipant = new Server_Spectator(this, nextPlayerId++, userInterface->copyUserInfo(true, true, true),
                                              judge, userInterface);
    } else {
        newParticipant = new Server_Player(this, nextPlayerId++, userInterface->copyUserInfo(true, true, true), judge,
                                           userInterface);
    }

    newParticipant->moveToThread(thread());

    Event_Join joinEvent;
    newParticipant->getProperties(*joinEvent.mutable_player_properties(), true);
    sendGameEventContainer(prepareGameEvent(joinEvent, -1));

    const QString playerName = QString::fromStdString(newParticipant->getUserInfo()->name());
    participants.insert(newParticipant->getPlayerId(), newParticipant);
    if (spectator) {
        allSpectatorsEver.insert(playerName);
    } else {
        allPlayersEver.insert(playerName);

        // if the original creator of the game joins, give them host status back
        //! \todo Transferring host to spectators has side effects.
        if (newParticipant->getUserInfo()->name() == creatorInfo->name()) {
            hostId = newParticipant->getPlayerId();
            sendGameEventContainer(prepareGameEvent(Event_GameHostChanged(), hostId));
        }
    }

    if (broadcastUpdate) {
        ServerInfo_Game gameInfo;
        gameInfo.set_room_id(room->getId());
        gameInfo.set_game_id(gameId);
        gameInfo.set_player_count(getPlayerCount());
        gameInfo.set_spectators_count(getSpectatorCount());
        emit gameInfoChanged(gameInfo);
    }

    if ((newParticipant->getUserInfo()->user_level() & ServerInfo_User::IsRegistered) && !spectator) {
        room->getServer()->addPersistentPlayer(playerName, room->getId(), gameId, newParticipant->getPlayerId());
    }

    userInterface->playerAddedToGame(gameId, room->getId(), newParticipant->getPlayerId());

    createGameJoinedEvent(newParticipant, rc, false);
}

void Server_Game::removeParticipant(Server_AbstractParticipant *participant, Event_Leave::LeaveReason reason)
{
    room->getServer()->removePersistentPlayer(QString::fromStdString(participant->getUserInfo()->name()), room->getId(),
                                              gameId, participant->getPlayerId());
    participants.remove(participant->getPlayerId());

    bool spectator = participant->isSpectator();
    GameEventStorage ges;
    if (!spectator) {
        auto *player = static_cast<Server_AbstractPlayer *>(participant);
        removeArrowsRelatedToPlayer(ges, player);
        unattachCards(ges, player);
    }

    Event_Leave event;
    event.set_reason(reason);
    ges.enqueueGameEvent(event, participant->getPlayerId());
    ges.sendToGame(this);

    bool playerActive = activePlayer == participant->getPlayerId();
    bool playerHost = hostId == participant->getPlayerId();
    participant->prepareDestroy();

    if (playerHost) {
        int newHostId = -1;
        for (auto *otherPlayer : getPlayers().values()) {
            newHostId = otherPlayer->getPlayerId();
            break;
        }
        if (newHostId != -1) {
            hostId = newHostId;
            sendGameEventContainer(prepareGameEvent(Event_GameHostChanged(), hostId));
        } else {
            gameClosed = true;
            deleteLater();
            return;
        }
    }
    if (!spectator) {
        stopGameIfFinished();
        if (gameStarted && playerActive) {
            nextTurn();
        }
    }

    ServerInfo_Game gameInfo;
    gameInfo.set_room_id(room->getId());
    gameInfo.set_game_id(gameId);
    gameInfo.set_player_count(getPlayerCount());
    gameInfo.set_spectators_count(getSpectatorCount());
    emit gameInfoChanged(gameInfo);
}

void Server_Game::removeArrowsRelatedToPlayer(GameEventStorage &ges, Server_AbstractPlayer *player)
{
    QMutexLocker locker(&gameMutex);

    // Remove all arrows of other players pointing to the player being removed or to one of his cards.
    // Also remove all arrows starting at one of his cards. This is necessary since players can create
    // arrows that start at another person's cards.
    for (Server_AbstractPlayer *anyPlayer : getPlayers().values()) {
        QList<Server_Arrow *> toDelete;
        for (auto *arrow : anyPlayer->getArrows().values()) {
            auto *targetCard = qobject_cast<Server_Card *>(arrow->getTargetItem());
            if (targetCard) {
                if (targetCard->getZone() != nullptr && targetCard->getZone()->getPlayer() == player) {
                    toDelete.append(arrow);
                }
            } else if (arrow->getTargetItem() == player) {
                toDelete.append(arrow);
            }

            // Don't use else here! It has to happen regardless of whether targetCard == 0.
            if (arrow->getStartCard()->getZone() != nullptr &&
                arrow->getStartCard()->getZone()->getPlayer() == player) {
                toDelete.append(arrow);
            }
        }
        for (auto *arrow : toDelete) {
            Event_DeleteArrow event;
            event.set_arrow_id(arrow->getId());
            ges.enqueueGameEvent(event, anyPlayer->getPlayerId());

            anyPlayer->deleteArrow(arrow->getId());
        }
    }
}

void Server_Game::unattachCards(GameEventStorage &ges, Server_AbstractPlayer *player)
{
    QMutexLocker locker(&gameMutex);

    for (auto zone : player->getZones()) {
        for (auto card : zone->getCards()) {
            // Make a copy of the list because the original one gets modified during the loop
            QList<Server_Card *> attachedCards = card->getAttachedCards();
            for (Server_Card *attachedCard : attachedCards) {
                auto otherPlayer = attachedCard->getZone()->getPlayer();
                // do not modify the current player's zone!
                // this would cause the current card iterator to be invalidated!
                // we only have to return cards owned by other players
                // because the current player is leaving the game anyway
                if (otherPlayer != player) {
                    otherPlayer->unattachCard(ges, attachedCard);
                }
            }
        }
    }
}

bool Server_Game::kickParticipant(int playerId)
{
    QMutexLocker locker(&gameMutex);

    auto *participant = participants.value(playerId);
    if (!participant) {
        return false;
    }

    GameEventContainer *gec = prepareGameEvent(Event_Kicked(), -1);
    participant->sendGameEvent(*gec);
    delete gec;

    removeParticipant(participant, Event_Leave::USER_KICKED);

    return true;
}

void Server_Game::setActivePlayer(int _activePlayer)
{
    QMutexLocker locker(&gameMutex);

    removeArrows(0, true);

    activePlayer = _activePlayer;

    Event_SetActivePlayer event;
    event.set_active_player_id(activePlayer);
    sendGameEventContainer(prepareGameEvent(event, -1));

    setActivePhase(0);
}

void Server_Game::setActivePhase(int newPhase)
{
    QMutexLocker locker(&gameMutex);

    removeArrows(newPhase);
    activePhase = newPhase;

    Event_SetActivePhase event;
    event.set_phase(activePhase);
    sendGameEventContainer(prepareGameEvent(event, -1));

    // Turn-structure automation. This fork is wholly Commander-dedicated, so it always runs.
    Rules::PhaseAutomation automation = Rules::RulesEngine::phaseAutomationFor(newPhase, turnNumber, getPlayerCount());
    if (automation != Rules::PhaseAutomation::None) {
        auto *activePlayerObj = dynamic_cast<Server_Player *>(getPlayers().value(activePlayer));
        if (activePlayerObj) {
            GameEventStorage ges;
            if (automation == Rules::PhaseAutomation::UntapActivePlayer) {
                activePlayerObj->setCardAttrHelper(ges, activePlayer, ZoneNames::TABLE, -1, AttrTapped,
                                                   QStringLiteral("0"));
            } else if (automation == Rules::PhaseAutomation::DrawForActivePlayer) {
                activePlayerObj->drawCards(ges, 1);
            }
            ges.sendToGame(this);
        }
    }

    // Phase 8 Stage 6: attacking status ends once combat ends (rule 506.4 simplified as "left the
    // combat-phase range" rather than one specific transition, since this fork's phases can be
    // freely jumped in any order — see Phase 4's standing no-enforced-phase-order design). Active-
    // player-only, like untap/draw above, since only the active player's creatures can be attacking.
    if (!Rules::RulesEngine::isCombatPhase(newPhase)) {
        auto *activePlayerObj = dynamic_cast<Server_Player *>(getPlayers().value(activePlayer));
        if (activePlayerObj) {
            GameEventStorage combatGes;
            activePlayerObj->setCardAttrHelper(combatGes, activePlayer, ZoneNames::TABLE, -1, AttrAttacking,
                                               QStringLiteral("0"));
            combatGes.sendToGame(this);
        }
    }

    // Mana pools empty at the end of every step and phase (rule 500.4) — unlike untap/draw
    // above, this applies to every player, not just the active one.
    GameEventStorage manaGes;
    for (auto *anyPlayer : getPlayers().values()) {
        if (auto *player = dynamic_cast<Server_Player *>(anyPlayer)) {
            player->emptyManaPool(manaGes);
        }
    }
    manaGes.sendToGame(this);

    // Phase 8 combat automation, Stage B: rule 510, resolved automatically on entering the
    // Combat Damage step -- see resolveCombatDamage()'s own doc comment for exactly what this
    // does and doesn't cover.
    if (newPhase == Rules::RulesEngine::COMBAT_DAMAGE_PHASE) {
        GameEventStorage damageGes;
        resolveCombatDamage(damageGes);
        damageGes.sendToGame(this);
    }

    // Rule 514.2: damage marked on permanents is removed at the End/Cleanup step, not when
    // combat itself ends (see RulesEngine::CLEANUP_PHASE's doc comment for why this is separate
    // from the attacking-clear block above). Every player's creatures, not just the active
    // player's, same as the mana-pool sweep above.
    if (newPhase == Rules::RulesEngine::CLEANUP_PHASE) {
        GameEventStorage cleanupGes;
        for (auto *anyPlayer : getPlayers().values()) {
            Server_CardZone *table = anyPlayer->getZones().value(ZoneNames::TABLE);
            if (!table) {
                continue;
            }
            for (Server_Card *card : table->getCards()) {
                if (card->getCounter(DAMAGE_CARD_COUNTER_ID) == 0) {
                    continue;
                }
                Event_SetCardCounter event;
                event.set_zone_name(table->getName().toStdString());
                event.set_card_id(card->getId());
                if (card->setCounter(DAMAGE_CARD_COUNTER_ID, 0, &event)) {
                    cleanupGes.enqueueGameEvent(event, anyPlayer->getPlayerId());
                }
            }
        }
        cleanupGes.sendToGame(this);
    }

    // Priority resets to the active player at the start of every phase (rule 117.3b/117.3c
    // simplified — see advancePriority() docs for what this doesn't model).
    broadcastPriorityChange(activePlayer);
}

QList<QPair<int, QString>> Server_Game::staticAbilitySourcesFor(Server_CardZone *table)
{
    QList<QPair<int, QString>> sources;
    if (!table) {
        return sources;
    }
    for (Server_Card *card : table->getCards()) {
        sources.append({card->getId(), card->getStaticAbilities()});
    }
    return sources;
}

QList<Rules::RulesEngine::CombatAttack>
Server_Game::gatherCombatAttacks(const QSet<QPair<int, int>> &declaredBlockedAttackers)
{
    QMap<int, Server_AbstractPlayer *> allPlayers = getPlayers();

    // Gather every attacking creature with a numerically parseable P/T (see parseNumericPT()'s
    // doc comment -- a non-numeric P/T like "*/1+*" is left out entirely, for manual resolution,
    // rather than silently mis-calculated as 0/0) and its currently declared blockers.
    QList<Rules::RulesEngine::CombatAttack> attacks;
    for (auto it = allPlayers.constBegin(); it != allPlayers.constEnd(); ++it) {
        Server_CardZone *table = it.value()->getZones().value(ZoneNames::TABLE);
        if (!table) {
            continue;
        }
        const QList<QPair<int, QString>> attackerStaticSources = staticAbilitySourcesFor(table);
        for (Server_Card *card : table->getCards()) {
            if (!card->getAttacking()) {
                continue;
            }
            auto attackerPT = Rules::RulesEngine::parseNumericPT(card->getPT());
            if (!attackerPT) {
                continue;
            }
            const auto attackerEffective = Rules::RulesEngine::applyStaticEffects(
                card->getId(), attackerPT->first, attackerPT->second, card->getKeywordSet(), attackerStaticSources);

            Rules::RulesEngine::CombatAttack attack;
            attack.attacker = {it.key(),
                               card->getId(),
                               attackerEffective.power,
                               attackerEffective.toughness,
                               attackerEffective.keywords.contains(QStringLiteral("Deathtouch")),
                               attackerEffective.keywords.contains(QStringLiteral("Trample")),
                               attackerEffective.keywords.contains(QStringLiteral("First strike")),
                               attackerEffective.keywords.contains(QStringLiteral("Double strike"))};
            attack.targetPlayerId = card->getAttackTargetPlayerId();
            attack.blocked = declaredBlockedAttackers.contains({it.key(), card->getId()});

            // This attacker's currently living declared blockers, across every player's table
            // zone. Order is an arbitrary but deterministic stand-in (ascending card id) for the
            // real player-chosen damage-assignment order -- see CombatAttack's doc comment.
            QList<Rules::RulesEngine::CombatCreature> blockers;
            for (auto blockerIt = allPlayers.constBegin(); blockerIt != allPlayers.constEnd(); ++blockerIt) {
                Server_CardZone *blockerTable = blockerIt.value()->getZones().value(ZoneNames::TABLE);
                if (!blockerTable) {
                    continue;
                }
                const QList<QPair<int, QString>> blockerStaticSources = staticAbilitySourcesFor(blockerTable);
                for (Server_Card *blockerCard : blockerTable->getCards()) {
                    if (blockerCard->getBlockedPlayerId() != it.key() ||
                        blockerCard->getBlockedCardId() != card->getId()) {
                        continue;
                    }
                    auto blockerPT = Rules::RulesEngine::parseNumericPT(blockerCard->getPT());
                    if (!blockerPT) {
                        continue;
                    }
                    const auto blockerEffective = Rules::RulesEngine::applyStaticEffects(
                        blockerCard->getId(), blockerPT->first, blockerPT->second, blockerCard->getKeywordSet(),
                        blockerStaticSources);
                    blockers.append({blockerIt.key(), blockerCard->getId(), blockerEffective.power,
                                     blockerEffective.toughness,
                                     blockerEffective.keywords.contains(QStringLiteral("Deathtouch")),
                                     blockerEffective.keywords.contains(QStringLiteral("Trample")),
                                     blockerEffective.keywords.contains(QStringLiteral("First strike")),
                                     blockerEffective.keywords.contains(QStringLiteral("Double strike"))});
                }
            }
            std::sort(blockers.begin(), blockers.end(),
                      [](const auto &a, const auto &b) { return a.cardId < b.cardId; });
            attack.blockers = blockers;

            attacks.append(attack);
        }
    }

    return attacks;
}

void Server_Game::resolveCombatDamage(GameEventStorage &ges)
{
    // Rule 509.1h: an attacker remains "blocked" even after every creature blocking it is removed
    // from combat (e.g. killed in the first-strike sub-pass below) -- so which attackers are
    // blocked must be captured once here, before either sub-pass runs, rather than re-derived from
    // each sub-pass's (possibly-thinned-by-deaths) blocker scan.
    QSet<QPair<int, int>> declaredBlockedAttackers;
    QMap<int, Server_AbstractPlayer *> allPlayers = getPlayers();
    for (auto it = allPlayers.constBegin(); it != allPlayers.constEnd(); ++it) {
        Server_CardZone *table = it.value()->getZones().value(ZoneNames::TABLE);
        if (!table) {
            continue;
        }
        for (Server_Card *card : table->getCards()) {
            if (card->getBlockedCardId() != -1) {
                declaredBlockedAttackers.insert({card->getBlockedPlayerId(), card->getBlockedCardId()});
            }
        }
    }

    // Rule 510.4: first-strike/double-strike creatures deal (and take) combat damage in an earlier
    // sub-pass; anything it kills is moved to its owner's graveyard (inside
    // applyCombatDamageResult()) before the regular sub-pass re-gathers combatants fresh from the
    // board -- see gatherCombatAttacks()'s own doc comment for why re-gathering, rather than
    // reusing the first sub-pass's list, is what makes a first-strike death "count" before the
    // second sub-pass. Most combats have no first/double strike creature at all, in which case the
    // first sub-pass is a no-op (calculateCombatDamage() finds nothing that acts in it) and the
    // second sub-pass alone reproduces this fork's pre-Stage-D single-pass behavior exactly.
    QList<Rules::RulesEngine::CombatAttack> firstStrikeAttacks = gatherCombatAttacks(declaredBlockedAttackers);
    if (!firstStrikeAttacks.isEmpty()) {
        applyCombatDamageResult(Rules::RulesEngine::calculateCombatDamage(
                                    firstStrikeAttacks, Rules::RulesEngine::CombatDamageStep::FirstStrike),
                                ges);
    }

    QList<Rules::RulesEngine::CombatAttack> regularAttacks = gatherCombatAttacks(declaredBlockedAttackers);
    if (!regularAttacks.isEmpty()) {
        applyCombatDamageResult(
            Rules::RulesEngine::calculateCombatDamage(regularAttacks, Rules::RulesEngine::CombatDamageStep::Regular),
            ges);
    }
}

void Server_Game::applyCombatDamageResult(const Rules::RulesEngine::CombatDamageResult &result, GameEventStorage &ges)
{
    QMap<int, Server_AbstractPlayer *> allPlayers = getPlayers();

    for (auto it = result.playerLifeLoss.constBegin(); it != result.playerLifeLoss.constEnd(); ++it) {
        auto *targetPlayer = dynamic_cast<Server_Player *>(allPlayers.value(it.key()));
        if (!targetPlayer) {
            continue;
        }
        const QMap<int, Server_Counter *> &counters = targetPlayer->getCounters();
        for (auto counterIt = counters.constBegin(); counterIt != counters.constEnd(); ++counterIt) {
            if (counterIt.value()->getName() == QStringLiteral("life")) {
                if (counterIt.value()->incrementCount(-it.value())) {
                    Event_SetCounter event;
                    event.set_counter_id(counterIt.value()->getId());
                    event.set_value(counterIt.value()->getCount());
                    ges.enqueueGameEvent(event, targetPlayer->getPlayerId());
                }
                break;
            }
        }
    }

    // Mark damage via the same DAMAGE_CARD_COUNTER_ID counter applyPendingAbility()'s targeted
    // damage effect already uses, and collect anything now lethal for the state-based death check
    // below (rule 704.5g, simplified to just lethal combat damage -- this stage's explicit scope,
    // see doc/commander-status/phase8-combat.md) via RulesEngine::isLethallyDamaged(), which also
    // accounts for deathtouch (Stage C: any nonzero damage from a deathtouch source is lethal) and
    // indestructible (Stage C: never destroyed by damage).
    QList<QPair<int, int>> lethalCards; // (owner player id, card id)
    for (auto ownerIt = result.cardDamageMarked.constBegin(); ownerIt != result.cardDamageMarked.constEnd();
         ++ownerIt) {
        Server_AbstractPlayer *owner = allPlayers.value(ownerIt.key());
        Server_CardZone *table = owner ? owner->getZones().value(ZoneNames::TABLE) : nullptr;
        if (!table) {
            continue;
        }
        const QList<QPair<int, QString>> staticSources = staticAbilitySourcesFor(table);
        for (auto cardIt = ownerIt.value().constBegin(); cardIt != ownerIt.value().constEnd(); ++cardIt) {
            Server_Card *card = table->getCard(cardIt.key());
            if (!card) {
                continue;
            }
            Event_SetCardCounter event;
            event.set_zone_name(table->getName().toStdString());
            event.set_card_id(card->getId());
            if (card->incrementCounter(DAMAGE_CARD_COUNTER_ID, cardIt.value(), &event)) {
                ges.enqueueGameEvent(event, owner->getPlayerId());
            }

            auto pt = Rules::RulesEngine::parseNumericPT(card->getPT());
            const bool anyDeathtouchDamage = result.deathtouchDamaged.value(ownerIt.key()).contains(card->getId());
            if (pt) {
                const auto effective = Rules::RulesEngine::applyStaticEffects(card->getId(), pt->first, pt->second,
                                                                              card->getKeywordSet(), staticSources);
                if (Rules::RulesEngine::isLethallyDamaged(
                        card->getCounter(DAMAGE_CARD_COUNTER_ID), effective.toughness, anyDeathtouchDamage,
                        effective.keywords.contains(QStringLiteral("Indestructible")))) {
                    lethalCards.append({ownerIt.key(), card->getId()});
                }
            }
        }
    }

    for (const auto &lethal : lethalCards) {
        Server_AbstractPlayer *owner = allPlayers.value(lethal.first);
        if (!owner) {
            continue;
        }
        Server_CardZone *table = owner->getZones().value(ZoneNames::TABLE);
        Server_CardZone *grave = owner->getZones().value(ZoneNames::GRAVE);
        if (!table || !grave) {
            continue;
        }
        CardToMove cardToMove;
        cardToMove.set_card_id(lethal.second);
        cardToMove.set_face_down(false);
        owner->moveCard(ges, table, {&cardToMove}, grave, -1, -1, true, false, false);
    }
}

void Server_Game::resetPriorityTo(int playerId)
{
    QMutexLocker locker(&gameMutex);
    broadcastPriorityChange(playerId);
}

void Server_Game::broadcastPriorityChange(int playerId)
{
    // Caller holds gameMutex.
    int holder = rulesEngine.startPriorityRound(playerId);
    Event_PriorityChanged event;
    event.set_priority_player_id(holder);
    sendGameEventContainer(prepareGameEvent(event, -1));
}

void Server_Game::pushPendingAbility(const Rules::PendingAbility &ability)
{
    QMutexLocker locker(&gameMutex);
    rulesEngine.pushPendingAbility(ability);
    broadcastPriorityChange(ability.controllerId);
}

void Server_Game::applyPendingAbility(const Rules::PendingAbility &ability, GameEventStorage &ges)
{
    // Reuses the exact mechanisms Stages 1/2 already proved -- nothing here is new logic, just
    // moved from what used to run instantly at activation time to running here, at resolution time.
    auto *controller = dynamic_cast<Server_Player *>(getPlayer(ability.controllerId));
    if (!controller) {
        return;
    }

    switch (ability.effect.kind) {
        case EffectKind::DrawCards:
            controller->drawCards(ges, ability.effect.amount);
            break;
        case EffectKind::GainLife:
        case EffectKind::LoseLife: {
            const int delta =
                ability.effect.kind == EffectKind::GainLife ? ability.effect.amount : -ability.effect.amount;
            const QMap<int, Server_Counter *> &counters = controller->getCounters();
            for (auto it = counters.constBegin(); it != counters.constEnd(); ++it) {
                if (it.value()->getName() == QStringLiteral("life")) {
                    if (it.value()->incrementCount(delta)) {
                        Event_SetCounter event;
                        event.set_counter_id(it.value()->getId());
                        event.set_value(it.value()->getCount());
                        ges.enqueueGameEvent(event, controller->getPlayerId());
                    }
                    break;
                }
            }
            break;
        }
        case EffectKind::DealDamage: {
            auto *targetPlayer = dynamic_cast<Server_Player *>(getPlayer(ability.targetPlayerId));
            if (!targetPlayer) {
                break;
            }
            if (ability.targetZone.isEmpty()) {
                // Player-target path: decrement the target's own "life" counter. Reuses the
                // already-wired Phase 9 life <= 0 advisory warning on the target's own client (a
                // cause-agnostic CounterState::valueChanged hook) for free -- no new SBA code
                // needed here, same as Stage 2's original immediate-resolution version.
                const QMap<int, Server_Counter *> &targetCounters = targetPlayer->getCounters();
                for (auto it = targetCounters.constBegin(); it != targetCounters.constEnd(); ++it) {
                    if (it.value()->getName() == QStringLiteral("life")) {
                        if (it.value()->incrementCount(-ability.effect.amount)) {
                            Event_SetCounter event;
                            event.set_counter_id(it.value()->getId());
                            event.set_value(it.value()->getCount());
                            ges.enqueueGameEvent(event, targetPlayer->getPlayerId());
                        }
                        break;
                    }
                }
            } else {
                // Card-target path: mark damage via the fork's conventional DAMAGE_CARD_COUNTER_ID
                // (see card_effects.h). Purely advisory, same as every other SBA in this fork.
                Server_CardZone *zone = targetPlayer->getZones().value(ability.targetZone);
                if (!zone || !zone->hasCoords()) {
                    break;
                }
                Server_Card *card = zone->getCard(ability.targetCardId);
                if (!card) {
                    break;
                }
                Event_SetCardCounter event;
                event.set_zone_name(zone->getName().toStdString());
                event.set_card_id(card->getId());
                if (card->incrementCounter(DAMAGE_CARD_COUNTER_ID, ability.effect.amount, &event)) {
                    ges.enqueueGameEvent(event, targetPlayer->getPlayerId());
                }
            }
            break;
        }
        case EffectKind::AddCounterToSelf:
            break; // not wired -- no parser produces this kind yet (matches Stage 1)
    }
}

void Server_Game::advancePriority(int passingPlayerId)
{
    QMutexLocker locker(&gameMutex);

    auto players = getPlayers();
    QSet<int> concededPlayers;
    for (auto it = players.constBegin(); it != players.constEnd(); ++it) {
        if (it.value()->getConceded()) {
            concededPlayers.insert(it.key());
        }
    }

    // The engine advances to the next eligible player, or -- if everyone has passed in
    // succession -- either resolves the top pending ability (rule 117.4 simplified) or stops the
    // round (holder -1) if nothing is pending. It deliberately does NOT auto-advance the
    // phase/turn itself (see the engine's passPriority() docs for the full rationale, incl. the
    // solo auto-pass infinite-loop this design avoids).
    Rules::PriorityPassResult result = rulesEngine.passPriority(passingPlayerId, players.keys(), concededPlayers);
    if (!result.changed) {
        return; // passingPlayerId didn't hold priority; cmdPassPriority already validates this.
    }

    Event_PriorityChanged event;
    event.set_priority_player_id(result.holder);
    sendGameEventContainer(prepareGameEvent(event, -1));

    if (result.resolvedAbility) {
        const Rules::PendingAbility &resolved = *result.resolvedAbility;

        Event_AbilityResolved resolvedEvent;
        resolvedEvent.set_controller_player_id(resolved.controllerId);
        resolvedEvent.set_effect_kind(static_cast<int>(resolved.effect.kind));
        resolvedEvent.set_amount(resolved.effect.amount);
        resolvedEvent.set_target_player_id(resolved.targetPlayerId);
        if (!resolved.targetZone.isEmpty()) {
            resolvedEvent.set_target_zone(resolved.targetZone.toStdString());
            resolvedEvent.set_target_card_id(resolved.targetCardId);
        }
        sendGameEventContainer(prepareGameEvent(resolvedEvent, -1));

        GameEventStorage ges;
        applyPendingAbility(resolved, ges);
        ges.sendToGame(this);

        // Rule 117.3b simplified: after a resolution, the active player gets priority again --
        // reopening a fresh round the exact same way a new phase/step already does.
        broadcastPriorityChange(activePlayer);
    }
}

qint64 Server_Game::generateArrowId()
{
    return nextArrowId++;
}

void Server_Game::removeArrows(int newPhase, bool force)
{
    QMutexLocker locker(&gameMutex);

    for (auto *anyPlayer : getPlayers().values()) {
        for (auto *arrowToDelete : anyPlayer->getArrows().values()) { // values creates a copy
            if (force || arrowToDelete->checkPhaseDeletion(newPhase)) {
                Event_DeleteArrow event;
                event.set_arrow_id(arrowToDelete->getId());
                sendGameEventContainer(prepareGameEvent(event, anyPlayer->getPlayerId()));

                anyPlayer->deleteArrow(arrowToDelete->getId());
            }
        }
    }
}

void Server_Game::nextTurn()
{
    QMutexLocker locker(&gameMutex);

    if (participants.isEmpty()) {
        qWarning() << "Server_Game::nextTurn was called while players is empty; gameId = " << gameId;
        return;
    }

    ++turnNumber;

    auto players = getPlayers();
    const QList<int> keys = players.keys();
    int listPos = -1;
    if (activePlayer != -1) {
        listPos = keys.indexOf(activePlayer);
    }
    do {
        if (turnOrderReversed) {
            --listPos;
            if (listPos < 0) {
                listPos = keys.size() - 1;
            }
        } else {
            ++listPos;
            if (listPos == keys.size()) {
                listPos = 0;
            }
        }
    } while (players.value(keys[listPos])->getConceded());

    setActivePlayer(keys[listPos]);
}

void Server_Game::createGameJoinedEvent(Server_AbstractParticipant *joiningParticipant,
                                        ResponseContainer &rc,
                                        bool resuming)
{
    Event_GameJoined event1;
    getInfo(*event1.mutable_game_info());
    event1.set_host_id(hostId);
    event1.set_player_id(joiningParticipant->getPlayerId());
    event1.set_spectator(joiningParticipant->isSpectator());
    event1.set_judge(joiningParticipant->isJudge());
    event1.set_resuming(resuming);
    if (resuming) {
        const QStringList &allGameTypes = room->getGameTypes();
        for (int i = 0; i < allGameTypes.size(); ++i) {
            ServerInfo_GameType *newGameType = event1.add_game_types();
            newGameType->set_game_type_id(i);
            newGameType->set_description(allGameTypes[i].toStdString());
        }
    }
    rc.enqueuePostResponseItem(ServerMessage::SESSION_EVENT, Server_AbstractUserInterface::prepareSessionEvent(event1));

    Event_GameStateChanged event2;
    event2.set_seconds_elapsed(secondsElapsed);
    event2.set_game_started(gameStarted);
    event2.set_active_player_id(activePlayer);
    event2.set_active_phase(activePhase);

    bool omniscient = (joiningParticipant->isSpectator() && spectatorsSeeEverything) || joiningParticipant->isJudge();
    for (auto *participant : participants.values()) {
        participant->getInfo(event2.add_player_list(), joiningParticipant, omniscient, true);
    }

    rc.enqueuePostResponseItem(ServerMessage::GAME_EVENT_CONTAINER, prepareGameEvent(event2, -1));
}

void Server_Game::sendGameEventContainer(GameEventContainer *cont,
                                         GameEventStorageItem::EventRecipients recipients,
                                         int privatePlayerId)
{
    QMutexLocker locker(&gameMutex);

    cont->set_game_id(gameId);
    for (auto *participant : participants.values()) {
        const bool playerPrivate = (participant->getPlayerId() == privatePlayerId) || participant->isJudge() ||
                                   (participant->isSpectator() && spectatorsSeeEverything);
        if ((recipients.testFlag(GameEventStorageItem::SendToPrivate) && playerPrivate) ||
            (recipients.testFlag(GameEventStorageItem::SendToOthers) && !playerPrivate)) {
            participant->sendGameEvent(*cont);
        }
    }
    if (recipients.testFlag(GameEventStorageItem::SendToPrivate)) {
        cont->set_seconds_elapsed(secondsElapsed - startTimeOfThisGame);
        cont->clear_game_id();
        currentReplay->add_event_list()->CopyFrom(*cont);
    }

    delete cont;
}

GameEventContainer *
Server_Game::prepareGameEvent(const ::google::protobuf::Message &gameEvent, int playerId, GameEventContext *context)
{
    auto *cont = new GameEventContainer;
    cont->set_game_id(gameId);
    if (context) {
        cont->mutable_context()->CopyFrom(*context);
    }
    GameEvent *event = cont->add_event_list();
    if (playerId != -1) {
        event->set_player_id(playerId);
    }
    event->GetReflection()
        ->MutableMessage(event, gameEvent.GetDescriptor()->FindExtensionByName("ext"))
        ->CopyFrom(gameEvent);
    return cont;
}

void Server_Game::getInfo(ServerInfo_Game &result) const
{
    QMutexLocker locker(&gameMutex);

    result.set_room_id(room->getId());
    result.set_game_id(gameId);
    if (gameClosed) {
        result.set_closed(true);
    } else {
        for (auto type : gameTypes) {
            result.add_game_types(type);
        }

        result.set_max_players(getMaxPlayers());
        result.set_description(getDescription().toStdString());
        result.set_with_password(!getPassword().isEmpty());
        result.set_player_count(getPlayerCount());
        result.set_started(gameStarted);
        result.mutable_creator_info()->CopyFrom(*getCreatorInfo());
        result.set_only_buddies(onlyBuddies);
        result.set_only_registered(onlyRegistered);
        result.set_spectators_allowed(getSpectatorsAllowed());
        result.set_spectators_need_password(getSpectatorsNeedPassword());
        result.set_spectators_can_chat(spectatorsCanTalk);
        result.set_spectators_omniscient(spectatorsSeeEverything);
        result.set_share_decklists_on_load(shareDecklistsOnLoad);
        result.set_spectators_count(getSpectatorCount());
        result.set_start_time(startTime.toSecsSinceEpoch());
    }
}

void Server_Game::returnCardsFromPlayer(GameEventStorage &ges, Server_AbstractPlayer *player)
{
    QMutexLocker locker(&gameMutex);
    // Return cards to their rightful owners before conceding the game
    static const QRegularExpression ownerRegex{"Owner: ?([^\n]+)"};
    const auto &playerTable = player->getZones().value(ZoneNames::TABLE);
    for (const auto &card : playerTable->getCards()) {
        if (card == nullptr) {
            continue;
        }

        const auto &regexResult = ownerRegex.match(card->getAnnotation());
        if (!regexResult.hasMatch()) {
            continue;
        }

        CardToMove cardToMove;
        cardToMove.set_card_id(card->getId());

        for (const auto *otherPlayer : getPlayers()) {
            if (otherPlayer == nullptr || otherPlayer->getUserInfo() == nullptr) {
                continue;
            }

            const auto &ownerToReturnTo = regexResult.captured(1);
            const auto &correctOwner = QString::compare(QString::fromStdString(otherPlayer->getUserInfo()->name()),
                                                        ownerToReturnTo, Qt::CaseInsensitive) == 0;
            if (!correctOwner) {
                continue;
            }

            const auto &targetZone = otherPlayer->getZones().value(ZoneNames::TABLE);

            if (playerTable == nullptr || targetZone == nullptr) {
                continue;
            }

            player->moveCard(ges, playerTable, QList<const CardToMove *>() << &cardToMove, targetZone, 0, 0, false);
            break;
        }
    }
}
