/**
 * @file dlg_choose_variable_mana_cost.h
 * @ingroup GameDialogs
 */

#ifndef DLG_CHOOSE_VARIABLE_MANA_COST_H
#define DLG_CHOOSE_VARIABLE_MANA_COST_H

#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>
#include <libcockatrice/card/ability/card_effects.h>
#include <libcockatrice/rules/rules_engine.h>

class QButtonGroup;
class QSpinBox;

/**
 * @brief Real spell casting from hand (phase6-mana.md's addendum): resolves a cost's hybrid/
 * Phyrexian/{X} components -- the parts RulesEngine::planManaCostChoices() can't silently default
 * because a genuine choice exists (or, for {X}, always needs announcing) -- into concrete choices,
 * before RulesEngine::resolveManaCost() folds them into a plain payable ManaCost. One optional row
 * per component, same "only ask when needed" precedent as DlgChooseGenericManaPayment:
 *
 * - An X spinbox, shown only when the cost has any "{X}" symbol.
 * - One radio-button pair per Rules::RulesEngine::HybridPipChoice with `ambiguous == true` (every
 *   non-ambiguous pip is silently folded in via its own defaultColor, no row at all).
 * - One radio-button pair per Rules::RulesEngine::PhyrexianPipChoice with `ambiguous == true`
 *   ("Pay 1 <color>" / "Pay 2 life"; non-ambiguous pips are forced to pay life with no row).
 *
 * If nothing above ends up needing a row (xCount == 0 and no pip is ambiguous), the caller should
 * skip constructing this dialog entirely -- ambiguous()/hasXPrompt() let it check first, so a cost
 * that merely contains hybrid/Phyrexian symbols but has no real choice to make still casts with
 * zero extra clicks.
 *
 * Deliberately instantiated and exec()'d directly by its caller (PlayerActions::
 * gateManaCostForHandPlay()), same non-PlayerDialogs-indirected pattern as
 * DlgChooseGenericManaPayment -- see that dialog's own header comment for why.
 */
class DlgChooseVariableManaCost : public QDialog
{
    Q_OBJECT

public:
    // pool is only used to bound the X spinbox (sum of all six colors -- a loose upper bound, not
    // an exact affordability check; RulesEngine::planManaPayment() catches any genuinely unpayable
    // X choice downstream, same division of responsibility as everywhere else in this addendum).
    DlgChooseVariableManaCost(QWidget *parent,
                              const ManaCost &cost,
                              const Rules::RulesEngine::ManaCostChoices &choices,
                              const QMap<QString, int> &pool);

    // True iff this dialog actually has any row to show -- the caller should skip exec()'ing (and
    // just use the choices' own defaults / an X value of 0) when this is false.
    [[nodiscard]] bool hasAnythingToAsk() const;

    // Valid only after exec() == QDialog::Accepted.
    [[nodiscard]] int chosenXValue() const;
    [[nodiscard]] QList<QString> chosenHybridColors() const;
    [[nodiscard]] QList<bool> chosenPhyrexianPayLife() const;

private:
    QSpinBox *xSpinBox = nullptr;
    QList<QButtonGroup *> hybridGroups;    // parallel to Rules::RulesEngine::ManaCostChoices::hybridChoices
    QList<QButtonGroup *> phyrexianGroups; // parallel to ...::phyrexianChoices
    Rules::RulesEngine::ManaCostChoices manaCostChoices;
};

#endif // DLG_CHOOSE_VARIABLE_MANA_COST_H
