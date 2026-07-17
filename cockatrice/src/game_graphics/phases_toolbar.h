/**
 * @file phases_toolbar.h
 * @ingroup GameGraphics
 * @ingroup GameWidgets
 */
//! \todo Document this file.

#ifndef PHASESTOOLBAR_H
#define PHASESTOOLBAR_H

#include "board/abstract_graphics_item.h"

#include <QFrame>
#include <QGraphicsObject>
#include <QList>

namespace google
{
namespace protobuf
{
class Message;
}
} // namespace google
class PlayerLogic;
class GameCommand;

class PhaseButton : public QObject, public QGraphicsItem
{
    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)
private:
    QString name;
    bool active, highlightable;
    int activeAnimationCounter;
    QTimer *activeAnimationTimer;
    QAction *doubleClickAction;
    double width;

    // void updatePixmap(QPixmap &pixmap);
private slots:
    void updateAnimation();

public:
    explicit PhaseButton(const QString &_name,
                         QGraphicsItem *parent = nullptr,
                         QAction *_doubleClickAction = nullptr,
                         bool _highlightable = true);
    [[nodiscard]] QRectF boundingRect() const override;
    void setWidth(double _width);
    void setActive(bool _active);
    [[nodiscard]] bool getActive() const
    {
        return active;
    }
    void triggerDoubleClickAction();
signals:
    void clicked();

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
};

/**
 * @brief The Commander-only "Pass Priority" button. A thin PhaseButton subclass, matching
 * CommandZone's precedent (cockatrice/src/game_graphics/zones/command_zone.h) of subclassing a
 * shared widget to add one feature-specific visual: a small badge indicating auto-pass mode is
 * on, distinct from PhaseButton's own active-highlight pulse (which here means "you currently
 * hold priority", a different concept from "auto-pass is enabled").
 */
class PriorityButton : public PhaseButton
{
    Q_OBJECT
public:
    explicit PriorityButton(QAction *_toggleAutoPassAction, QGraphicsItem *parent = nullptr);
    void setAutoPassEnabled(bool _autoPassEnabled);
    [[nodiscard]] bool getAutoPassEnabled() const
    {
        return autoPassEnabled;
    }

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    bool autoPassEnabled = false;
};

class PhasesToolbar : public QObject, public QGraphicsItem
{
    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)
private:
    QList<PhaseButton *> buttonList;
    PhaseButton *nextTurnButton;
    PriorityButton *passPriorityButton;
    bool autoPassPriority = false;
    double width, height, ySpacing, symbolSize;
    int buttonCount = 13;
    static const int spaceCount = 6;
    static const double marginSize;
    void rearrangeButtons();
    void updatePassPriorityTooltip();

public:
    explicit PhasesToolbar(QGraphicsItem *parent = nullptr);
    [[nodiscard]] QRectF boundingRect() const override;
    void retranslateUi();
    void setHeight(double _height);
    [[nodiscard]] double getWidth() const
    {
        return width;
    }
    [[nodiscard]] int phaseCount() const
    {
        return buttonList.size();
    }
    [[nodiscard]] QString getLongPhaseName(int phase) const;
public slots:
    void setActivePhase(int phase);
    void triggerPhaseAction(int phase);
    /**
     * @brief Highlights the Pass Priority button (reusing PhaseButton's existing
     * active-phase pulse animation) while the local player currently holds priority. If
     * auto-pass is enabled (see actToggleAutoPassPriority), also immediately passes.
     */
    void setPriorityHolder(bool localPlayerHasPriority);
private slots:
    void phaseButtonClicked();
    void actNextTurn();
    void actUntapAll();
    void actDrawCard();
    void actPassPriority();
    /**
     * @brief Double-click on the Pass Priority button (mirroring the existing Untap/Draw
     * double-click-for-alternate-action convention already used elsewhere in this toolbar).
     * When enabling auto-pass while priority is currently held, passes immediately rather than
     * waiting for the next priority event, so turning it on never leaves you stuck holding
     * priority indefinitely.
     */
    void actToggleAutoPassPriority();
signals:
    void sendGameCommand(const ::google::protobuf::Message &command, int playerId);

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/) override;
};

#endif
