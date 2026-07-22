/**
 * @file card_menu.h
 * @ingroup GameMenusCards
 */
//! \todo Document this file.

#ifndef COCKATRICE_CARD_MENU_H
#define COCKATRICE_CARD_MENU_H

#include <QMenu>
#include <libcockatrice/utility/card_ref.h>

class CardItem;
class PlayerGraphicsItem;
class PlayerLogic;
class CardMenu : public QMenu
{
    Q_OBJECT

signals:
    void cardInfoRequested(const CardRef &cardRef);

public:
    explicit CardMenu(PlayerGraphicsItem *player, const CardItem *card, bool shortcutsActive);
    void removePlayer(PlayerLogic *playerToRemove);
    void createTableMenu(bool canModifyCard);
    void createStackMenu(bool canModifyCard);
    void createGraveyardOrExileMenu(bool canModifyCard);
    void createHandOrCustomZoneMenu(bool canModifyCard);
    void createZonelessMenu(bool canModifyCard);

    QMenu *mCardCounters;

    QAction *aPlay, *aPlayFacedown;
    QAction *aRevealToAll;
    QAction *aHide;
    QAction *aClone;
    QAction *aSelectAll, *aSelectRow, *aSelectColumn;
    QAction *aDrawArrow;
    QAction *aTap, *aDoesntUntap;
    QAction *aAttacking;
    QAction *aBlocking;
    QAction *aFlip, *aPeek;
    QAction *aAttach, *aUnattach;
    QAction *aSetAnnotation;
    QAction *aReduceLifeByPower;

    QList<QAction *> aAddCounter, aSetCounter, aRemoveCounter;

    // Dedicated +1/+1 and -1/-1 counter actions, distinct from the generic lettered
    // aAddCounter/aRemoveCounter slots above -- see PLUS_ONE_ONE_COUNTER_ID's doc comment
    // (card_effects.h) for why they aren't just two more entries in that loop.
    QAction *aAddPlusOneOneCounter, *aRemovePlusOneOneCounter;
    QAction *aAddMinusOneOneCounter, *aRemoveMinusOneOneCounter;

private:
    PlayerGraphicsItem *player;
    const CardItem *card;
    QList<QPair<QString, int>> playersInfo;
    bool shortcutsActive;

    void addRelatedCardActions();
    void retranslateUi();
    void initContextualPlayersMenu(QMenu *menu, QAction *allPlayersAction);
    void initAttackTargetMenu(QMenu *menu);
    void initBlockerMenu(QMenu *menu);
    void setShortcutsActive();
    void addRelatedCardView();
};

#endif // COCKATRICE_CARD_MENU_H
