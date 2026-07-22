#include "counter_general.h"

#include "../../interface/pixel_map_generator.h"
#include "abstract_graphics_item.h"
#include "translate_counter_name.h"

#include <QFontMetrics>
#include <QPainter>
#include <QSet>
#include <libcockatrice/rules/commander_counter_names.h>

namespace
{
// Counters with their own themed icon (mana colors, storm's lightning bolt, poison's droplet --
// see cockatrice/resources/counters/) already read fine without a label; every other counter
// (per-commander tax/damage, and any future colorless-fallback counter) renders as the same
// generic circle (see CounterPixmapGenerator::generatePixmap's fallback), so those get a short
// text label underneath instead.
bool hasOwnIcon(const QString &name)
{
    static const QSet<QString> namesWithIcons = {"w", "u", "b", "r", "g", "storm", "poison"};
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
                               bool shownInCounterArea,
                               bool interactive)
    : AbstractCounter(state, player, shownInCounterArea, useNameForShortcut, parent, interactive)
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
        // Local (0,0)-based rect matching the pixmap drawn above -- using mapRect directly here
        // (its position component, not just its size) drew the numeral offset from the sphere
        // it's supposed to sit on top of.
        const QRectF textRect(0, 0, translatedHeight, translatedHeight);
        const QString text = QString::number(value);

        // Shrink-to-fit: a fixed size regardless of digit count let 3-digit values overflow the
        // circle (confirmed live: 100 spilled past both edges). Starts smaller than before, too
        // (0.62x radius instead of 1x), then steps down further only if the text is still wider
        // than the circle leaves room for.
        QFont f; // inherits the app-wide sans-serif default (see main.cpp)
        f.setWeight(QFont::Bold);
        int pixelSize = qMax((int)(radius * scaleFactor * 0.62), 8);
        const qreal maxTextWidth = translatedHeight * 0.82;
        f.setPixelSize(pixelSize);
        while (QFontMetrics(f).horizontalAdvance(text) > maxTextWidth && pixelSize > 6) {
            f.setPixelSize(--pixelSize);
        }
        painter->setFont(f);

        // White fill with a dark outline (drawn as an 8-direction 1px-offset shadow) so the
        // numeral stays readable regardless of what's underneath.
        painter->setPen(Qt::black);
        for (qreal dx = -1; dx <= 1; ++dx) {
            for (qreal dy = -1; dy <= 1; ++dy) {
                if (dx != 0 || dy != 0) {
                    painter->drawText(textRect.translated(dx, dy), Qt::AlignCenter, text);
                }
            }
        }
        painter->setPen(Qt::white);
        painter->drawText(textRect, Qt::AlignCenter, text);
    }
    painter->restore();

    if (needsLabel()) {
        // Drawn in plain item-local coordinates (painter is back to its normal transform after
        // the restore() above), unlike the icon/value above which deliberately draws in
        // device-pixel space for crisp caching (see resetPainterTransform()).
        QFont labelFont; // inherits the app-wide sans-serif default (see main.cpp)
        labelFont.setPixelSize(9);
        painter->setFont(labelFont);
        painter->setPen(Qt::white);
        painter->drawText(QRectF(0, radius * 2, radius * 2, 12), Qt::AlignHCenter | Qt::AlignTop, shortLabelFor(name));
    }
}
