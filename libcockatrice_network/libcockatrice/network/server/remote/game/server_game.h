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
#ifndef SERVERGAME_H
#define SERVERGAME_H

#include "../server_response_containers.h"

#include <QDateTime>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <libcockatrice/protocol/pb/event_leave.pb.h>
#include <libcockatrice/protocol/pb/response.pb.h>
#include <libcockatrice/protocol/pb/serverinfo_game.pb.h>

class QTimer;
class GameEventContainer;
class GameReplay;
class Server_Room;
class Server_AbstractPlayer;
class Server_AbstractParticipant;
class ServerInfo_User;
class ServerInfo_Game;
class Server_AbstractUserInterface;
class Event_GameStateChanged;

/**
 * @brief What (if anything) Commander turn-structure automation should do when entering a
 * phase, given the phase index (see cockatrice/src/game/phase.cpp) and turn context. Kept as
 * a pure decision separate from Server_Game::setActivePhase() so the logic (including the
 * rule 103.8a/103.8c first-draw-skip arithmetic) is unit-testable without needing a fully
 * constructed, participant-registered game.
 */
enum class CommanderPhaseAutomation
{
    None,
    UntapActivePlayer,
    DrawForActivePlayer
};

/**
 * @brief Number of phases in a turn, for wrapping phase 10 (End/Cleanup) back to phase 0
 * (Untap) of the next turn. Coupled to cockatrice/src/game/phase.cpp: Phases::phaseTypesCount
 * — see the caveat on CommanderPhaseAutomation above; same architectural gap (no shared
 * server/client phase enum), same Commander-games-only gating to contain the assumption.
 */
constexpr int COMMANDER_PHASE_COUNT = 11;

class Server_Game : public QObject
{
    Q_OBJECT
private:
    Server_Room *room;
    int nextPlayerId;
    std::atomic<qint64> nextArrowId = 1;
    int hostId;
    ServerInfo_User *creatorInfo;
    QMap<int, Server_AbstractParticipant *> participants;
    QSet<QString> allPlayersEver, allSpectatorsEver;
    bool gameStarted;
    bool gameClosed;
    int gameId;
    QString description;
    QString password;
    int maxPlayers;
    QList<int> gameTypes;
    int activePlayer, activePhase;
    int turnNumber = 0; // incremented once per nextTurn() call; turn 1 is the first turn of the game.
    // Commander-only priority tracking (see isCommanderGame()); unused/meaningless for other
    // game types. priorityPlayerId is -1 when not applicable (e.g. game not started).
    int priorityPlayerId = -1;
    QSet<int> priorityPassedBy;
    bool onlyBuddies, onlyRegistered;
    bool spectatorsAllowed;
    bool spectatorsNeedPassword;
    bool spectatorsCanTalk;
    bool spectatorsSeeEverything;
    int startingLifeTotal;
    bool shareDecklistsOnLoad;
    int inactivityCounter;
    int startTimeOfThisGame, secondsElapsed;
    bool firstGameStarted;
    bool turnOrderReversed;
    QDateTime startTime;
    QTimer *pingClock;
    QList<GameReplay *> replayList;
    GameReplay *currentReplay;

    void createGameStateChangedEvent(Event_GameStateChanged *event,
                                     Server_AbstractParticipant *recipient,
                                     bool omniscient,
                                     bool withUserInfo);
    void storeGameInformation();
signals:
    void sigStartGameIfReady(bool override);
    void gameInfoChanged(ServerInfo_Game gameInfo);
private slots:
    void pingClockTimeout();
    void doStartGameIfReady(bool forceStartGame = false);

public:
    mutable QRecursiveMutex gameMutex;
    Server_Game(const ServerInfo_User &_creatorInfo,
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
                Server_Room *parent);
    ~Server_Game() override;
    Server_Room *getRoom() const
    {
        return room;
    }
    void getInfo(ServerInfo_Game &result) const;
    int getHostId() const
    {
        return hostId;
    }
    ServerInfo_User *getCreatorInfo() const
    {
        return creatorInfo;
    }
    bool getGameStarted() const
    {
        return gameStarted;
    }
    int getPlayerCount() const;
    int getSpectatorCount() const;
    QMap<int, Server_AbstractPlayer *> getPlayers() const;
    Server_AbstractPlayer *getPlayer(int id) const;
    const QMap<int, Server_AbstractParticipant *> &getParticipants() const
    {
        return participants;
    }
    int getGameId() const
    {
        return gameId;
    }
    QString getDescription() const
    {
        return description;
    }
    QString getPassword() const
    {
        return password;
    }
    int getMaxPlayers() const
    {
        return maxPlayers;
    }
    bool getSpectatorsAllowed() const
    {
        return spectatorsAllowed;
    }
    bool getSpectatorsNeedPassword() const
    {
        return spectatorsNeedPassword;
    }
    bool getSpectatorsCanTalk() const
    {
        return spectatorsCanTalk;
    }
    bool getSpectatorsSeeEverything() const
    {
        return spectatorsSeeEverything;
    }
    int getStartingLifeTotal() const
    {
        return startingLifeTotal;
    }
    bool getShareDecklistsOnLoad() const
    {
        return shareDecklistsOnLoad;
    }
    Response::ResponseCode
    checkJoin(ServerInfo_User *user, const QString &_password, bool spectator, bool overrideRestrictions, bool asJudge);
    bool containsUser(const QString &userName) const;
    void addPlayer(Server_AbstractUserInterface *userInterface,
                   ResponseContainer &rc,
                   bool spectator,
                   bool judge,
                   bool broadcastUpdate = true);
    void removeParticipant(Server_AbstractParticipant *participant, Event_Leave::LeaveReason reason);
    void removeArrowsRelatedToPlayer(GameEventStorage &ges, Server_AbstractPlayer *player);
    void unattachCards(GameEventStorage &ges, Server_AbstractPlayer *player);
    bool kickParticipant(int playerId);
    void startGameIfReady(bool forceStartGame);
    void stopGameIfFinished();
    int getActivePlayer() const
    {
        return activePlayer;
    }
    int getActivePhase() const
    {
        return activePhase;
    }
    void setActivePlayer(int newPlayer);
    void setActivePhase(int newPhase);
    qint64 generateArrowId();
    void removeArrows(int newPhase, bool force = false);
    void nextTurn();
    int getTurnNumber() const
    {
        return turnNumber;
    }
    /**
     * @brief Whether this game's room game-type selection denotes a Commander-family game
     * (see CommanderRules::gameTypeLabelIsCommander), used to gate Commander-specific
     * server-side behavior (command zone placement, tax/damage tracking, turn-structure
     * automation) so it doesn't affect other game types.
     */
    bool isCommanderGame() const;

    /**
     * @brief Pure decision logic for what CommanderPhaseAutomation applies when entering
     * @p phase, given the game's current @p turnNumber and @p playerCount. Rule 103.8a/103.8c:
     * only a strict two-player game's starting player skips their first draw step; multiplayer
     * Commander games never skip it.
     */
    static CommanderPhaseAutomation phaseAutomationFor(int phase, int turnNumber, int playerCount);

    /**
     * @brief Commander-only (see isCommanderGame()) priority tracking — see
     * COMMANDER_IMPLEMENTATION_STATUS.md "Phase 5" for what this simplifies away from the full
     * CR priority/stack rules. -1 if not applicable (e.g. not a Commander game, or the game
     * hasn't started).
     */
    int getPriorityPlayerId() const
    {
        return priorityPlayerId;
    }

    /**
     * @brief Called when @p passingPlayerId passes priority. Advances priority to the next
     * player in turn order (see nextPriorityPlayer()) who hasn't yet passed since the last
     * phase/turn change; if everyone eligible has now passed, advances to the next phase (or,
     * wrapping past the last phase, the next turn) instead — Commander's simplified stand-in
     * for "the stack is empty and everyone passes in succession" (rule 117.4), since this fork
     * doesn't model the stack as resolvable objects (see Phase 5 notes).
     */
    void advancePriority(int passingPlayerId);

    /**
     * @brief Pure logic: the next player, in ascending-id turn order starting just after
     * @p currentPlayerId (wrapping around @p playerOrder), who is in neither @p passedPlayers
     * nor @p concededPlayers. Returns -1 if every eligible player has already passed. Kept
     * separate from advancePriority() so it's unit-testable without a fully constructed,
     * participant-registered game (mirrors phaseAutomationFor()'s rationale above).
     */
    static int nextPriorityPlayer(const QList<int> &playerOrder,
                                  int currentPlayerId,
                                  const QSet<int> &passedPlayers,
                                  const QSet<int> &concededPlayers);

    int getSecondsElapsed() const
    {
        return secondsElapsed;
    }
    bool reverseTurnOrder()
    {
        return turnOrderReversed = !turnOrderReversed;
    }

    void createGameJoinedEvent(Server_AbstractParticipant *participant, ResponseContainer &rc, bool resuming);

    GameEventContainer *
    prepareGameEvent(const ::google::protobuf::Message &gameEvent, int playerId, GameEventContext *context = 0);
    GameEventContext prepareGameEventContext(const ::google::protobuf::Message &gameEventContext);

    void sendGameStateToPlayers();
    void sendGameEventContainer(GameEventContainer *cont,
                                GameEventStorageItem::EventRecipients recipients = GameEventStorageItem::SendToPrivate |
                                                                                   GameEventStorageItem::SendToOthers,
                                int privatePlayerId = -1);
    void returnCardsFromPlayer(GameEventStorage &ges, Server_AbstractPlayer *player);
};

#endif
