/*
 * Copyright (C) 2015 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     sbw <sbw@sbw.so>
 *             kirigaya <kirigaya@mkacg.com>
 *             Hualet <mr.asianwang@gmail.com>
 *
 * Maintainer: sbw <sbw@sbw.so>
 *             kirigaya <kirigaya@mkacg.com>
 *             Hualet <mr.asianwang@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CONTENTVIEWWIDGET
#define CONTENTVIEWWIDGET
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

#include <memory>

#include <com_deepin_wm.h>
#include <com_deepin_daemon_appearance.h>

#include "src/widgets/rounditembutton.h"
#include "src/global_util/util_updateui.h"
#include "src/global_util/dbus/dbusvariant.h"
#include "src/global_util/dbus/dbuslogin1manager.h"
#include "src/dde-shutdown/common.h"

#include "systemmonitor.h"
#include "warningview.h"
#include "inhibitwarnview.h"
#include "switchos_interface.h"

#include <com_deepin_sessionmanager.h>

//com.deepin.SessionManager接口统一使用frameworkdbus中的声明
using Appearance = com::deepin::daemon::Appearance;
using SessionManager = com::deepin::SessionManager;

class MultiUsersWarningView;
class SessionBaseModel;
class User;
class DBusControlCenter;
class ContentWidget: public QFrame
{
    Q_OBJECT
public:
    ContentWidget(QWidget *parent = nullptr);
    void setModel(SessionBaseModel *const model);
    void initBackground();
    ~ContentWidget() override;

signals:
    void requestBackground(const QString &path) const;
    void buttonClicked(const Actions action);

protected:
    void mouseReleaseEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void showEvent(QShowEvent *event) Q_DECL_OVERRIDE;
    void hideEvent(QHideEvent *event) Q_DECL_OVERRIDE;
    bool event(QEvent *event) Q_DECL_OVERRIDE;

public slots:
    void setConfirm(const bool confirm);
    bool powerAction(const Actions action);
    void setPreviousChildFocus();
    void setNextChildFocus();
    void showTips(const QString &tips);
    void hideBtns(const QStringList &btnsName);
    void disableBtns(const QStringList &btnsName);
    void onCancel();
    QList<InhibitWarnView::InhibitorData> listInhibitors(const Actions action);
    void recoveryLayout();
    void runSystemMonitor();

private:
    void initUI();
    void initConnect();
    void initData();
    void enterKeyPushed();
    void hideBtn(const QString &btnName);
    void disableBtn(const QString &btnName);
    bool beforeInvokeAction(const Actions action);
    void hideToplevelWindow();
    void shutDownFrameActions(const Actions action);
    bool handleKeyPress(QKeyEvent *event);

    void currentWorkspaceChanged();
    void updateWallpaper(const QString &path);
    void onUserListChanged(int users_size);
    void enableHibernateBtn(bool enable);
    void enableSleepBtn(bool enable);
    void tryGrabKeyboard();

    RoundItemButton *m_currentSelectedBtn = nullptr;
    RoundItemButton *m_shutdownButton;
    RoundItemButton *m_restartButton;
    RoundItemButton *m_suspendButton;
    RoundItemButton *m_hibernateButton;
    RoundItemButton *m_lockButton;
    RoundItemButton *m_logoutButton;
    RoundItemButton *m_switchUserBtn;
    RoundItemButton *m_switchSystemBtn = nullptr;
    QList<RoundItemButton *> *m_btnsList;

    QWidget *m_tipsWidget;
    QLabel *m_tipsLabel;
    DBusLogin1Manager *m_login1Inter;
    HuaWeiSwitchOSInterface* m_switchosInterface = nullptr;
    DBusControlCenter *m_controlCenterInter;

    WarningView *m_warningView = nullptr;
    QWidget *m_normalView = nullptr;
    QStackedLayout *m_mainLayout;

    bool m_confirm = false;

    SessionManager *m_sessionInterface = nullptr;
    SystemMonitor *m_systemMonitor;
    com::deepin::wm *m_wmInter;
    Appearance *m_dbusAppearance = nullptr;
    SessionBaseModel *m_model;
    QStringList m_inhibitorBlacklists;
    int m_failures = 0;
};

class InhibitHint
{
public:
    QString name, icon, why;

    friend const QDBusArgument &operator>>(const QDBusArgument &argument, InhibitHint &obj)
    {
        argument.beginStructure();
        argument >> obj.name >> obj.icon >> obj.why;
        argument.endStructure();
        return argument;
    }
};
#endif // CONTENTVIEWWIDGET
