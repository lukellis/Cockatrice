/**
 * @file card_menu_action_type.h
 * @ingroup GameMenusPlayers
 */
//! \todo Document this file.

#ifndef COCKATRICE_CARD_MENU_ACTION_TYPE_H
#define COCKATRICE_CARD_MENU_ACTION_TYPE_H

enum CardMenuActionType
{
    // Per-card attribute actions (must be <= cmClone for cardMenuAction() dispatch)
    cmTap,
    cmUntap,
    cmDoesntUntap,
    cmAttacking, // Phase 8 Stage 6: declare/remove as attacker, intercepted ahead of the generic
                 // per-card dispatch loop below (see cardMenuAction())
    cmFlip,
    cmPeek,
    cmClone,
    // Move actions (must be > cmClone for cardMenuAction() dispatch)
    cmMoveToTopLibrary,
    cmMoveToBottomLibrary,
    cmMoveToHand,
    cmMoveToGraveyard,
    cmMoveToExile,
    cmMoveToTable
};

#endif // COCKATRICE_CARD_MENU_ACTION_TYPE_H
