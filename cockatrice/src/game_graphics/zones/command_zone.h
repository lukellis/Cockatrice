/**
 * @file command_zone.h
 * @ingroup GameGraphicsZones
 */

#ifndef COMMANDZONE_H
#define COMMANDZONE_H

#include "pile_zone.h"

/**
 * A PileZone for the Commander/EDH command zone, visually distinguished from the other,
 * otherwise-identical-looking piles (deck, graveyard, exile, sideboard) with a colored border
 * and a "CMD" label, so a player can spot their commander at a glance rather than having to
 * remember pile order or mouse over each one — matching how physical Commander play sets the
 * commander card somewhere visibly distinct on the table. Unlike other piles, which render
 * rotated 90° to stack compactly, this one shows the commander in its natural portrait/untapped
 * orientation, since it's meant to be immediately recognizable rather than just a pile marker.
 */
class CommandZone : public PileZone
{
    Q_OBJECT
public:
    CommandZone(PileZoneLogic *_logic, QGraphicsItem *parent);
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
};

#endif
