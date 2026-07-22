/**
 * @file player_graphics_item.h
 * @ingroup GameGraphicsPlayers
 */
//! \todo Document this file.

#ifndef COCKATRICE_PLAYER_GRAPHICS_ITEM_H
#define COCKATRICE_PLAYER_GRAPHICS_ITEM_H
#include "../../game/player/player_logic.h"
#include "../board/abstract_counter.h"
#include "../game_scene.h"

#include <QGraphicsObject>

class CounterGroupBox;
class HandZone;
class ManaPentagonWidget;
class PileZone;
class PlayerDialogs;
class PlayerMenu;
class PlayerTarget;
class StackZone;
class TableZone;
class ZoneViewZone;

class PlayerGraphicsItem : public QGraphicsObject
{
    Q_OBJECT

public:
    enum
    {
        Type = typeOther
    };
    int type() const override
    {
        return Type;
    }

    // Wide enough to fit ManaPentagonWidget (a bordered circle ~88px across) centered, plus its
    // own small margin either side -- was 55 back when counters just stacked individually.
    static constexpr int counterAreaWidth = 90;

    explicit PlayerGraphicsItem(PlayerLogic *player);
    void initializeZones();

    [[nodiscard]] QRectF boundingRect() const override;
    qreal getMinimumWidth() const;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    void processSceneSizeChange(int newPlayerWidth);

    void setMirrored(bool _mirrored);

    bool getMirrored() const
    {
        return mirrored;
    }

    GameScene *getGameScene() const
    {
        return static_cast<GameScene *>(scene());
    }

    PlayerLogic *getLogic() const
    {
        return player;
    }

    [[nodiscard]] PlayerMenu *getPlayerMenu() const
    {
        return playerMenu;
    }

    PlayerArea *getPlayerArea() const
    {
        return playerArea;
    }

    PlayerTarget *getPlayerTarget() const
    {
        return playerTarget;
    }

    CardZone *getZoneGraphicsItem(const QString &name) const
    {
        return zoneGraphicsItems.value(name, nullptr);
    }

    [[nodiscard]] PileZone *getDeckZoneGraphicsItem() const
    {
        return deckZoneGraphicsItem;
    }

    [[nodiscard]] PileZone *getSideboardZoneGraphicsItem() const
    {
        return sideboardGraphicsItem;
    }

    [[nodiscard]] PileZone *getGraveyardZoneGraphicsItem() const
    {
        return graveyardZoneGraphicsItem;
    }
    [[nodiscard]] PileZone *getRfgZoneGraphicsItem() const
    {
        return rfgZoneGraphicsItem;
    }
    [[nodiscard]] PileZone *getCommandZoneGraphicsItem() const
    {
        return commandZoneGraphicsItem;
    }
    [[nodiscard]] TableZone *getTableZoneGraphicsItem() const
    {
        return tableZoneGraphicsItem;
    }
    [[nodiscard]] StackZone *getStackZoneGraphicsItem() const
    {
        return stackZoneGraphicsItem;
    }
    [[nodiscard]] HandZone *getHandZoneGraphicsItem() const
    {
        return handZoneGraphicsItem;
    }

public slots:
    void onPlayerActiveChanged(bool _active);
    void onCustomZoneAdded(QString customZoneName);
    void onCounterAdded(CounterState *state);
    void onCounterRemoved(int counterId);
    void rearrangeCounters();
    void retranslateUi();

signals:
    void sizeChanged();
    void playerCountChanged();
    void mirroredChanged(bool isMirrored);
    void cardInfoRequested(const CardRef &cardRef);

private:
    PlayerLogic *player;
    PlayerMenu *playerMenu;
    PlayerDialogs *playerDialogs;
    PlayerArea *playerArea;
    PlayerTarget *playerTarget;
    QMap<int, AbstractCounter *> counterWidgets;
    CounterGroupBox *specialCounterGroup = nullptr;
    ManaPentagonWidget *manaPentagon = nullptr;
    QMap<QString, CardZone *> zoneGraphicsItems;
    PileZone *deckZoneGraphicsItem;
    PileZone *sideboardGraphicsItem;
    PileZone *graveyardZoneGraphicsItem;
    PileZone *rfgZoneGraphicsItem;
    PileZone *commandZoneGraphicsItem;
    TableZone *tableZoneGraphicsItem;
    StackZone *stackZoneGraphicsItem;
    HandZone *handZoneGraphicsItem;
    QRectF bRect;
    bool mirrored;
    bool handVisible = false;

private slots:
    void updateBoundingRect();
    void rearrangeZones();
};

#endif // COCKATRICE_PLAYER_GRAPHICS_ITEM_H
