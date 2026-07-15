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

class PhasesToolbar : public QObject, public QGraphicsItem
{
    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)
private:
    QList<PhaseButton *> buttonList;
    PhaseButton *nextTurnButton;
    PhaseButton *passPriorityButton;
    bool commanderGame;
    double width, height, ySpacing, symbolSize;
    int buttonCount = 12;
    static const int spaceCount = 6;
    static const double marginSize;
    void rearrangeButtons();

public:
    explicit PhasesToolbar(QGraphicsItem *parent = nullptr);
    [[nodiscard]] QRectF boundingRect() const override;
    void retranslateUi();
    void setHeight(double _height);
    /**
     * @brief Shows or hides the Commander-only "Pass Priority" button (sends
     * Command_PassPriority; see COMMANDER_IMPLEMENTATION_STATUS.md's Phase 5 section).
     * Non-Commander games never see this button, matching how the rest of the toolbar's
     * phases are shared across all game types.
     */
    void setCommanderGame(bool isCommanderGame);
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
     * active-phase pulse animation) while the local player currently holds priority.
     */
    void setPriorityHolder(bool localPlayerHasPriority);
private slots:
    void phaseButtonClicked();
    void actNextTurn();
    void actUntapAll();
    void actDrawCard();
    void actPassPriority();
signals:
    void sendGameCommand(const ::google::protobuf::Message &command, int playerId);

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/) override;
};

#endif
