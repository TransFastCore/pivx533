// Copyright (c) 2019-2020 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef NAVMENUWIDGET_H
#define NAVMENUWIDGET_H

#include <QWidget>
#include "qt/trumpcoin/pwidget.h"

class TrumpCoinGUI;

namespace Ui {
class NavMenuWidget;
}

class NavMenuWidget : public PWidget
{
    Q_OBJECT

public:
    explicit NavMenuWidget(TrumpCoinGUI* mainWindow, QWidget *parent = nullptr);
    ~NavMenuWidget();

    void loadWalletModel() override;
    virtual void showEvent(QShowEvent *event) override;

public Q_SLOTS:
    void selectSettings();
    void onShowHideColdStakingChanged(bool show);

private Q_SLOTS:
    void onSendClicked();
    void onDashboardClicked();
    void onAddressClicked();
    void onPatriotNodesClicked();
    void onColdStakingClicked();
    void onGovClicked();
    void onSettingsClicked();
    void onReceiveClicked();
    void updateButtonStyles();
private:
    Ui::NavMenuWidget *ui;
    QList<QWidget*> btns;

    void connectActions();
    void onNavSelected(QWidget* active, bool startup = false);

    bool init = false;
};

#endif // NAVMENUWIDGET_H