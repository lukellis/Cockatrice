#include "pending_ability_widget.h"

#include "../../game/abstract_game.h"
#include "../../game/player/player_info.h"
#include "../../game/player/player_logic.h"
#include "../../game/player/player_manager.h"

#include <QVBoxLayout>
#include <libcockatrice/card/ability/card_effects.h>

PendingAbilityWidget::PendingAbilityWidget(AbstractGame *_game, QWidget *parent) : QWidget(parent), game(_game)
{
    priorityLabel = new QLabel(this);
    stackList = new QListWidget(this);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(priorityLabel);
    layout->addWidget(stackList);
    setLayout(layout);

    setPriorityPlayer(-1);
}

QString PendingAbilityWidget::playerName(int playerId) const
{
    if (!game) {
        return tr("Unknown");
    }
    PlayerLogic *player = game->getPlayerManager()->getPlayers().value(playerId, nullptr);
    return player ? player->getPlayerInfo()->getName() : tr("Unknown");
}

QString
PendingAbilityWidget::describeEffect(int effectKind, int amount, int targetPlayerId, const QString &targetZone) const
{
    switch (static_cast<EffectKind>(effectKind)) {
        case EffectKind::DrawCards:
            return tr("Draw %n card(s)", "", amount);
        case EffectKind::GainLife:
            return tr("Gain %1 life").arg(amount);
        case EffectKind::LoseLife:
            return tr("Lose %1 life").arg(amount);
        case EffectKind::DealDamage:
            if (targetZone.isEmpty()) {
                return tr("Deal %1 damage to %2").arg(amount).arg(playerName(targetPlayerId));
            }
            return tr("Deal %1 damage to a permanent").arg(amount);
        case EffectKind::AddCounterToSelf:
            return tr("Add a counter"); // no parser produces this kind yet -- never reached in practice
    }
    return tr("Unknown ability");
}

void PendingAbilityWidget::pushAbility(
    int controllerId, int effectKind, int amount, int targetPlayerId, QString targetZone, int targetCardId)
{
    Q_UNUSED(targetCardId); // display is deliberately generic for a card-zone target -- see header doc comment
    const QString text =
        tr("%1: %2").arg(playerName(controllerId), describeEffect(effectKind, amount, targetPlayerId, targetZone));
    stackList->insertItem(0, text);
}

void PendingAbilityWidget::popAbility()
{
    if (stackList->count() > 0) {
        delete stackList->takeItem(0);
    }
}

void PendingAbilityWidget::setPriorityPlayer(int playerId)
{
    priorityLabel->setText(playerId == -1 ? tr("Priority: no one") : tr("Priority: %1").arg(playerName(playerId)));
}
