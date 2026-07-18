/**
 * @file pending_ability_widget.h
 * @ingroup GameWidgets
 */

#ifndef COCKATRICE_PENDING_ABILITY_WIDGET_H
#define COCKATRICE_PENDING_ABILITY_WIDGET_H

#include <QLabel>
#include <QListWidget>
#include <QWidget>

class AbstractGame;

/**
 * @brief A small side panel visualizing state Phase 7 Stage 3 (a real resolvable pending-ability
 * stack) and Phase 5 (priority passing) already track server-side but that was previously only
 * ever visible as transient message-log lines. Purely a client-side mirror of already-broadcast
 * wire data (Event_AbilityActivated/Event_AbilityResolved/Event_PriorityChanged) -- no new
 * protocol messages. The list is pushed on every activation and unconditionally pops its own top
 * entry on every resolution; since the server's pending-ability store is a genuine LIFO stack that
 * always resolves exactly the top entry, this stays correctly in sync without needing a unique
 * ability id on the wire.
 */
class PendingAbilityWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PendingAbilityWidget(AbstractGame *_game, QWidget *parent = nullptr);

public slots:
    void pushAbility(int controllerId, int effectKind, int amount, int targetPlayerId, QString targetZone, int targetCardId);
    void popAbility();
    void setPriorityPlayer(int playerId);

private:
    AbstractGame *game;
    QLabel *priorityLabel;
    QListWidget *stackList;

    QString playerName(int playerId) const;
    QString describeEffect(int effectKind, int amount, int targetPlayerId, const QString &targetZone) const;
};

#endif // COCKATRICE_PENDING_ABILITY_WIDGET_H
