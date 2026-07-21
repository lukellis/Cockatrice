/**
 * @file dlg_choose_generic_mana_payment.h
 * @ingroup GameDialogs
 */

#ifndef DLG_CHOOSE_GENERIC_MANA_PAYMENT_H
#define DLG_CHOOSE_GENERIC_MANA_PAYMENT_H

#include <QDialog>
#include <QMap>
#include <QString>
#include <libcockatrice/card/ability/card_effects.h>

class QDialogButtonBox;
class QLabel;
class QSpinBox;

/**
 * @brief Real spell casting from hand: shown only when RulesEngine::isGenericPaymentAmbiguous()
 * says paying a spell's generic mana cost has a genuine color choice (e.g. a {1} generic cost with
 * both untapped W and U available) -- see doc/commander-status/phase6-mana.md. One QSpinBox per
 * color with a nonzero remaining balance (bounds [0, that color's availability]), OK enabled only
 * once the spinboxes' total exactly equals the cost's generic amount. Pre-filled with the same
 * split RulesEngine::planManaPayment()'s existing deterministic w/u/b/r/g/x order would have
 * picked, so accepting immediately with no changes reproduces this fork's pre-existing behavior
 * exactly -- the player only needs to interact if they want a different split.
 *
 * Deliberately instantiated and exec()'d directly by its caller (PlayerActions'
 * gateManaCostForHandPlay(), called from several structurally different "a card left hand" call
 * sites) rather than routed through the PlayerActions -> PlayerDialogs signal indirection every
 * other real dialog in this codebase uses -- that indirection only works cleanly when there's a
 * single call site that can fully hand off control to PlayerDialogs' own completion logic; here,
 * each caller needs the chosen split back synchronously to finish building its own bespoke
 * Command_MoveCard. See gateManaCostForHandPlay()'s own doc comment for the full rationale.
 */
class DlgChooseGenericManaPayment : public QDialog
{
    Q_OBJECT

public:
    // remainingPool is the pool after cost's colored pips are paid (see
    // RulesEngine::remainingPoolAfterColoredPips()) -- only colors with a nonzero entry here get a
    // spinbox. prefill is the initial per-color split to populate the spinboxes with (must already
    // sum to cost.generic -- typically the generic-only portion of planManaPayment()'s own plan).
    DlgChooseGenericManaPayment(QWidget *parent,
                                const ManaCost &cost,
                                const QMap<QString, int> &remainingPool,
                                const QMap<QString, int> &prefill);

    // Valid only after exec() == QDialog::Accepted. Keyed by the same w/u/b/r/g/x counter-name
    // convention as ManaCost::coloredPips/RulesEngine::manaCounterNames().
    [[nodiscard]] QMap<QString, int> chosenGenericSplit() const;

private:
    int genericAmount;
    QMap<QString, QSpinBox *> spinBoxes;
    QLabel *totalLabel;
    QDialogButtonBox *buttonBox;

    void updateTotal();
};

#endif // DLG_CHOOSE_GENERIC_MANA_PAYMENT_H
