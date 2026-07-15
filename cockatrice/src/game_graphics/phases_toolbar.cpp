#include "phases_toolbar.h"

#include "../interface/pixel_map_generator.h"

#include <QAction>
#include <QDebug>
#include <QPainter>
#include <QPen>
#include <QTimer>
#include <libcockatrice/protocol/pb/command_draw_cards.pb.h>
#include <libcockatrice/protocol/pb/command_next_turn.pb.h>
#include <libcockatrice/protocol/pb/command_pass_priority.pb.h>
#include <libcockatrice/protocol/pb/command_set_active_phase.pb.h>
#include <libcockatrice/protocol/pb/command_set_card_attr.pb.h>
#include <libcockatrice/utility/zone_names.h>

PhaseButton::PhaseButton(const QString &_name, QGraphicsItem *parent, QAction *_doubleClickAction, bool _highlightable)
    : QObject(), QGraphicsItem(parent), name(_name), active(false), highlightable(_highlightable),
      activeAnimationCounter(0), doubleClickAction(_doubleClickAction), width(50)
{
    if (highlightable) {
        activeAnimationTimer = new QTimer(this);
        connect(activeAnimationTimer, &QTimer::timeout, this, &PhaseButton::updateAnimation);
        activeAnimationTimer->setSingleShot(false);
    } else {
        activeAnimationCounter = 9;
    }

    setCacheMode(DeviceCoordinateCache);
}

QRectF PhaseButton::boundingRect() const
{
    return {0, 0, width, width};
}

void PhaseButton::paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/)
{
    QRectF iconRect = boundingRect().adjusted(3, 3, -3, -3);
    QRectF translatedIconRect = painter->combinedTransform().mapRect(iconRect);
    qreal scaleFactor = translatedIconRect.width() / iconRect.width();
    QPixmap iconPixmap = PhasePixmapGenerator::generatePixmap(qRound(translatedIconRect.height()), name);

    painter->setBrush(QColor(static_cast<int>(220 * (activeAnimationCounter / 10.0)),
                             static_cast<int>(220 * (activeAnimationCounter / 10.0)),
                             static_cast<int>(220 * (activeAnimationCounter / 10.0))));
    painter->setPen(Qt::gray);
    painter->drawRect(0, 0, static_cast<int>(width - 1), static_cast<int>(width - 1));
    painter->save();
    resetPainterTransform(painter);
    painter->drawPixmap(iconPixmap.rect().translated(qRound(3 * scaleFactor), qRound(3 * scaleFactor)), iconPixmap,
                        iconPixmap.rect());
    painter->restore();

    painter->setBrush(QColor(0, 0, 0, static_cast<int>(255 * ((10 - activeAnimationCounter) / 15.0))));
    painter->setPen(Qt::gray);
    painter->drawRect(0, 0, static_cast<int>(width - 1), static_cast<int>(width - 1));
}

void PhaseButton::setWidth(double _width)
{
    prepareGeometryChange();
    width = _width;
}

void PhaseButton::setActive(bool _active)
{
    if ((active == _active) || !highlightable) {
        return;
    }

    active = _active;
    activeAnimationTimer->start(25);
}

void PhaseButton::updateAnimation()
{
    if (!highlightable) {
        return;
    }

    // the counter ticks up to 10 when active and down to 0 when inactive
    if (active && activeAnimationCounter < 10) {
        ++activeAnimationCounter;
    } else if (!active && activeAnimationCounter > 0) {
        --activeAnimationCounter;
    } else {
        activeAnimationTimer->stop();
    }

    update();
}

void PhaseButton::mousePressEvent(QGraphicsSceneMouseEvent * /*event*/)
{
    emit clicked();
}

void PhaseButton::mouseDoubleClickEvent(QGraphicsSceneMouseEvent * /*event*/)
{
    triggerDoubleClickAction();
}

void PhaseButton::triggerDoubleClickAction()
{
    if (doubleClickAction) {
        doubleClickAction->trigger();
    }
}

PriorityButton::PriorityButton(QAction *_toggleAutoPassAction, QGraphicsItem *parent)
    : PhaseButton(QStringLiteral("pass_priority"), parent, _toggleAutoPassAction, true)
{
}

void PriorityButton::setAutoPassEnabled(bool _autoPassEnabled)
{
    if (autoPassEnabled == _autoPassEnabled) {
        return;
    }
    autoPassEnabled = _autoPassEnabled;
    update();
}

void PriorityButton::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    PhaseButton::paint(painter, option, widget);
    if (!autoPassEnabled) {
        return;
    }

    const qreal dotDiameter = boundingRect().width() * 0.28;
    QRectF dotRect(boundingRect().width() - dotDiameter - 3, 3, dotDiameter, dotDiameter);
    painter->save();
    painter->setBrush(QColor(80, 200, 255));
    painter->setPen(Qt::white);
    painter->drawEllipse(dotRect);
    painter->restore();
}

PhasesToolbar::PhasesToolbar(QGraphicsItem *parent)
    : QGraphicsItem(parent), commanderGame(false), width(100), height(100), ySpacing(1), symbolSize(8)
{
    auto *aUntapAll = new QAction(this);
    connect(aUntapAll, &QAction::triggered, this, &PhasesToolbar::actUntapAll);
    auto *aDrawCard = new QAction(this);
    connect(aDrawCard, &QAction::triggered, this, &PhasesToolbar::actDrawCard);

    PhaseButton *untapButton = new PhaseButton("untap", this, aUntapAll);
    PhaseButton *upkeepButton = new PhaseButton("upkeep", this);
    PhaseButton *drawButton = new PhaseButton("draw", this, aDrawCard);
    PhaseButton *main1Button = new PhaseButton("main1", this);
    PhaseButton *combatStartButton = new PhaseButton("combat_start", this);
    PhaseButton *combatAttackersButton = new PhaseButton("combat_attackers", this);
    PhaseButton *combatBlockersButton = new PhaseButton("combat_blockers", this);
    PhaseButton *combatDamageButton = new PhaseButton("combat_damage", this);
    PhaseButton *combatEndButton = new PhaseButton("combat_end", this);
    PhaseButton *main2Button = new PhaseButton("main2", this);
    PhaseButton *cleanupButton = new PhaseButton("cleanup", this);

    buttonList << untapButton << upkeepButton << drawButton << main1Button << combatStartButton << combatAttackersButton
               << combatBlockersButton << combatDamageButton << combatEndButton << main2Button << cleanupButton;

    for (auto &i : buttonList) {
        connect(i, &PhaseButton::clicked, this, &PhasesToolbar::phaseButtonClicked);
    }

    nextTurnButton = new PhaseButton("nextturn", this, nullptr, false);
    connect(nextTurnButton, &PhaseButton::clicked, this, &PhasesToolbar::actNextTurn);

    auto *aToggleAutoPassPriority = new QAction(this);
    connect(aToggleAutoPassPriority, &QAction::triggered, this, &PhasesToolbar::actToggleAutoPassPriority);

    passPriorityButton = new PriorityButton(aToggleAutoPassPriority, this);
    passPriorityButton->setVisible(false);
    connect(passPriorityButton, &PhaseButton::clicked, this, &PhasesToolbar::actPassPriority);

    rearrangeButtons();

    retranslateUi();
}

QRectF PhasesToolbar::boundingRect() const
{
    return {0, 0, width, height};
}

void PhasesToolbar::retranslateUi()
{
    for (int i = 0; i < buttonList.size(); ++i) {
        buttonList[i]->setToolTip(getLongPhaseName(i));
    }
    updatePassPriorityTooltip();
}

void PhasesToolbar::updatePassPriorityTooltip()
{
    passPriorityButton->setToolTip(autoPassPriority
                                       ? tr("Pass priority (auto-pass is ON — double-click to turn off)")
                                       : tr("Pass priority (double-click to auto-pass while you hold it)"));
}

QString PhasesToolbar::getLongPhaseName(int phase) const
{
    switch (phase) {
        case 0:
            return tr("Untap step");
        case 1:
            return tr("Upkeep step");
        case 2:
            return tr("Draw step");
        case 3:
            return tr("First main phase");
        case 4:
            return tr("Beginning of combat step");
        case 5:
            return tr("Declare attackers step");
        case 6:
            return tr("Declare blockers step");
        case 7:
            return tr("Combat damage step");
        case 8:
            return tr("End of combat step");
        case 9:
            return tr("Second main phase");
        case 10:
            return tr("End of turn step");
        default:
            return QString();
    }
}

void PhasesToolbar::paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/)
{
    painter->fillRect(boundingRect(), QColor(50, 50, 50));
}

const double PhasesToolbar::marginSize = 3;

void PhasesToolbar::rearrangeButtons()
{
    for (auto &i : buttonList) {
        i->setWidth(symbolSize);
    }
    nextTurnButton->setWidth(symbolSize);
    passPriorityButton->setWidth(symbolSize);

    double y = marginSize;
    buttonList[0]->setPos(marginSize, y);
    buttonList[1]->setPos(marginSize, y += symbolSize);
    buttonList[2]->setPos(marginSize, y += symbolSize);
    y += ySpacing;
    buttonList[3]->setPos(marginSize, y += symbolSize);
    y += ySpacing;
    buttonList[4]->setPos(marginSize, y += symbolSize);
    buttonList[5]->setPos(marginSize, y += symbolSize);
    buttonList[6]->setPos(marginSize, y += symbolSize);
    buttonList[7]->setPos(marginSize, y += symbolSize);
    buttonList[8]->setPos(marginSize, y += symbolSize);
    y += ySpacing;
    buttonList[9]->setPos(marginSize, y += symbolSize);
    y += ySpacing;
    buttonList[10]->setPos(marginSize, y += symbolSize);
    y += ySpacing;
    y += ySpacing;
    nextTurnButton->setPos(marginSize, y += symbolSize);

    if (commanderGame) {
        y += ySpacing;
        passPriorityButton->setPos(marginSize, y + symbolSize);
    }
}

void PhasesToolbar::setHeight(double _height)
{
    prepareGeometryChange();

    height = _height;
    ySpacing = (height - 2 * marginSize) / (buttonCount * 5 + spaceCount);
    symbolSize = ySpacing * 5;
    width = symbolSize + 2 * marginSize;

    rearrangeButtons();
}

void PhasesToolbar::setCommanderGame(bool isCommanderGame)
{
    if (commanderGame == isCommanderGame) {
        return;
    }

    commanderGame = isCommanderGame;
    passPriorityButton->setVisible(commanderGame);
    // One more button-height's worth of vertical space to reserve/release, matching how
    // nextTurnButton is already accounted for in buttonCount despite not being in buttonList.
    buttonCount += commanderGame ? 1 : -1;

    setHeight(height);
}

void PhasesToolbar::setPriorityHolder(bool localPlayerHasPriority)
{
    passPriorityButton->setActive(localPlayerHasPriority);
    if (localPlayerHasPriority && autoPassPriority) {
        actPassPriority();
    }
}

void PhasesToolbar::setActivePhase(int phase)
{
    if (phase >= buttonList.size()) {
        return;
    }

    for (int i = 0; i < buttonList.size(); ++i) {
        buttonList[i]->setActive(i == phase);
    }
}

void PhasesToolbar::triggerPhaseAction(int phase)
{
    if (0 <= phase && phase < buttonList.size()) {
        buttonList[phase]->triggerDoubleClickAction();
    }
}

void PhasesToolbar::phaseButtonClicked()
{
    auto *button = qobject_cast<PhaseButton *>(sender());
    if (button->getActive()) {
        button->triggerDoubleClickAction();
    }

    Command_SetActivePhase cmd;
    cmd.set_phase(static_cast<google::protobuf::uint32>(buttonList.indexOf(button)));

    emit sendGameCommand(cmd, -1);
}

void PhasesToolbar::actNextTurn()
{
    emit sendGameCommand(Command_NextTurn(), -1);
}

void PhasesToolbar::actUntapAll()
{
    Command_SetCardAttr cmd;
    cmd.set_zone(ZoneNames::TABLE);
    cmd.set_attribute(AttrTapped);
    cmd.set_attr_value("0");

    emit sendGameCommand(cmd, -1);
}

void PhasesToolbar::actDrawCard()
{
    Command_DrawCards cmd;
    cmd.set_number(1);

    emit sendGameCommand(cmd, -1);
}

void PhasesToolbar::actPassPriority()
{
    emit sendGameCommand(Command_PassPriority(), -1);
}

void PhasesToolbar::actToggleAutoPassPriority()
{
    autoPassPriority = !autoPassPriority;
    passPriorityButton->setAutoPassEnabled(autoPassPriority);
    updatePassPriorityTooltip();

    if (autoPassPriority && passPriorityButton->getActive()) {
        actPassPriority();
    }
}
