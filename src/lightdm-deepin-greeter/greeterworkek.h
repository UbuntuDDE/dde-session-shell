#ifndef GREETERWORKEK_H
#define GREETERWORKEK_H

#include <QLightDM/Greeter>
#include <QLightDM/SessionsModel>
#include <QObject>

#include "dbuslockservice.h"
#include "authinterface.h"
#include "deepinauthframework.h"
#include "dbuslogin1manager.h"
#include "sessionbasemodel.h"
#include "interface/deepinauthinterface.h"
#include <com_deepin_daemon_authenticate.h>
#include <com_deepin_api_xeventmonitor.h>

using XEventInter = com::deepin::api::XEventMonitor;
using com::deepin::daemon::Authenticate;

class GreeterWorkek : public Auth::AuthInterface, public DeepinAuthInterface
{
    Q_OBJECT
public:
    enum AuthFlag {
        Password = 1 << 0,
        Fingerprint = 1 << 1,
        Face = 1 << 2,
        ActiveDirectory = 1 << 3
    };

    explicit GreeterWorkek(SessionBaseModel *const model, QObject *parent = nullptr);
    ~GreeterWorkek();

    void switchToUser(std::shared_ptr<User> user) override;

    /* Old authentication methods */
    void onDisplayErrorMsg(const QString &msg) override;
    void onDisplayTextInfo(const QString &msg) override;
    void onPasswordResult(const QString &msg) override;

signals:
    void requestUpdateBackground(const QString &path);

public slots:
    /* Old authentication methods */
    void authUser(const QString &password) override;
    /* New authentication framework */
    void createAuthentication(const QString &account);
    void destoryAuthentication(const QString &account);
    void startAuthentication(const QString &account, const int authType);
    void endAuthentication(const QString &account, const int authType);
    void sendTokenToAuth(const QString &account, const int authType, const QString &token);

    void checkAccount(const QString &account);

private:
    void initConnections();
    void initData();
    void initConfiguration();

    void doPowerAction(const SessionBaseModel::PowerAction action);
    void setCurrentUser(const std::shared_ptr<User> user);

    void checkDBusServer(bool isvalid);
    void oneKeyLogin();
    void userAuthForLightdm(std::shared_ptr<User> user);
    void showPrompt(const QString &text, const QLightDM::Greeter::PromptType type);
    void showMessage(const QString &text, const QLightDM::Greeter::MessageType type);
    void authenticationComplete();
    void saveNumlockStatus(std::shared_ptr<User> user, const bool &on);
    void recoveryUserKBState(std::shared_ptr<User> user);
    void resetLightdmAuth(std::shared_ptr<User> user, int delay_time, bool is_respond);

private:
    QLightDM::Greeter *m_greeter;
    DeepinAuthFramework *m_authFramework;
    DBusLockService *m_lockInter;
    XEventInter *m_xEventInter;
    QTimer *m_resetSessionTimer;
    QString m_account;
    QString m_password;
    bool m_retryAuth;
};

#endif  // GREETERWORKEK_H
