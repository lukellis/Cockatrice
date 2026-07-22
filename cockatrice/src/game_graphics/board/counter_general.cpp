#include "counter_general.h"

#include "../../interface/pixel_map_generator.h"
#include "abstract_graphics_item.h"
#include "translate_counter_name.h"

#include <QPainter>
#include <QSet>
#include <libcockatrice/rules/commander_counter_names.h>

namespace
{
// The six mana-color counters already read fine via their own colored icon; every other
// counter (poison, per-commander damage, and any future colorless-fallback counter) renders as
// the same generic circle (see CounterPixmapGenerator::generatePixmap's fallback), so those get
// a short text label underneath instead.
bool hasOwnIcon(const QString &name)
{
    static const QSet<QString> namesWithIcons = {"w", "u", "b", "r", "g"};
    return namesWithIcons.contains(name);
}

QString shortLabelFor(const QString &name)
{
    // Per-commander damage/tax counter names are the full "Commander Damage: <name>"/
    // "Commander Tax: <name>" strings (see commander_counter_names.h) -- too long to fit under a
    // 30px badge, so abbreviate; the full name is still available via the AbstractCounter tooltip.
    if (CommanderCounterNames::isDamageCounter(name)) {
        return QStringLiteral("Dmg");
    }
    if (CommanderCounterNames::isTaxCounter(name)) {
        return QStringLiteral("Tax");
    }
    return TranslateCounterName::getDisplayName(name);
}
} // namespace

GeneralCounter::GeneralCounter(CounterState *state,
                               PlayerLogic *player,
                               bool useNameForShortcut,
                               QGraphicsItem *parent,
                               bool shownInCounterArea)
    : AbstractCounter(state, player, shownInCounterArea, useNameForShortcut, parent)
{
    setCacheMode(DeviceCoordinateCache);
}

bool GeneralCounter::needsLabel() const
{
    // Only counters that actually live in the shared, position-ambiguous counter column need a
    // label to tell them apart -- one rendered elsewhere (e.g. the Commander Tax badge on the
    // command zone, see PlayerGraphicsItem::onCounterAdded()) is already unambiguous from its
    // position alone, and that corner has no room to spare for one.
    return getShownInCounterArea() && !hasOwnIcon(name);
}

QRectF GeneralCounter::boundingRect() const
{
    qreal labelHeight = needsLabel() ? 12 : 0;
    return QRectF(0, 0, radius * 2, radius * 2 + labelHeight);
}

void GeneralCounter::paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/)
{
    QRectF iconRect(0, 0, radius * 2, radius * 2);
    QRectF mapRect = painter->combinedTransform().mapRect(iconRect);
    int translatedHeight = mapRect.size().height();
    qreal scaleFactor = translatedHeight / iconRect.height();
    QPixmap pixmap = CounterPixmapGenerator::generatePixmap(translatedHeight, name, hovered);

    painter->save();
    resetPainterTransform(painter);
    painter->drawPixmap(QPoint(0, 0), pixmap);

    if (value) {
        QFont f("Serif");
        f.setPixelSize(qMax((int)(radius * scaleFactor), 10));
        f.setWeight(QFont::Bold);
        painter->setPen(Qt::black);
        painter->setFont(f);
        painter->drawText(mapRect, Qt::AlignCenter, QString::number(value));
    }
    painter->restore();

    if (needsLabel()) {
        // Drawn in plain item-local coordinates (painter is back to its normal transform after
        // the restore() above), unlike the icon/value above which deliberately draws in
        // device-pixel space for crisp caching (see resetPainterTransform()).
        QFont labelFont("Serif");
        labelFont.setPixelSize(9);
        painter->setFont(labelFont);
        painter->setPen(Qt::white);
        painter->drawText(QRectF(0, radius * 2, radius * 2, 12), Qt::AlignHCenter | Qt::AlignTop, shortLabelFor(name));
    }
}
