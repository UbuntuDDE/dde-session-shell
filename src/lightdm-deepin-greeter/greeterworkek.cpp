#include "authcommon.h"
#include "greeterworkek.h"
#include "userinfo.h"
#include "keyboardmonitor.h"

#include <unistd.h>
#include <libintl.h>
#include <DSysInfo>

#include <QGSettings>

#include <com_deepin_system_systempower.h>

#define LOCKSERVICE_PATH "/com/deepin/dde/LockService"
#define LOCKSERVICE_NAME "com.deepin.dde.LockService"

using PowerInter = com::deepin::system::Power;
using namespace Auth;
using namespace AuthCommon;
DCORE_USE_NAMESPACE

class UserNumlockSettings
{
public:
    UserNumlockSettings(const QString &username)
        : m_username(username)
        , m_settings(QSettings::UserScope, "deepin", "greeter")
    {
    }

    int get(const int defaultValue) { return m_settings.value(m_username, defaultValue).toInt(); }
    void set(const int value) { m_settings.setValue(m_username, value); }

private:
    QString m_username;
    QSettings m_settings;
};

GreeterWorkek::GreeterWorkek(SessionBaseModel *const model, QObject *parent)
    : AuthInterface(model, parent)
    , m_greeter(new QLightDM::Greeter(this))
    , m_authFramework(new DeepinAuthFramework(this, this))
    , m_lockInter(new DBusLockService(LOCKSERVICE_NAME, LOCKSERVICE_PATH, QDBusConnection::systemBus(), this))
    , m_xEventInter(new XEventInter("com.deepin.api.XEventMonitor", "/com/deepin/api/XEventMonitor", QDBusConnection::sessionBus(), this))
    , m_resetSessionTimer(new QTimer(this))
{
#ifndef QT_DEBUG
    if (!m_greeter->connectSync()) {
        qCritical() << "greeter connect fail !!!";
        exit(1);
    }
#endif

    checkDBusServer(m_accountsInter->isValid());

    initConnections();
    initData();
    initConfiguration();

    if (DSysInfo::deepinType() == DSysInfo::DeepinServer || m_model->isActiveDirectoryDomain()) {
        std::shared_ptr<User> user(new User());
        m_model->setIsServerModel(DSysInfo::deepinType() == DSysInfo::DeepinServer || !m_model->isActiveDirectoryDomain());
        m_model->addUser(user);
        if (DSysInfo::deepinType() == DSysInfo::DeepinServer) {
            m_model->updateCurrentUser(user);
        }
    } else {
        connect(m_login1Inter, &DBusLogin1Manager::SessionRemoved, this, [=] {
            qDebug() << "DBusLogin1Manager::SessionRemoved";
            // lockservice sometimes fails to call on olar server
            QDBusPendingReply<QString> replay = m_lockInter->CurrentUser();
            replay.waitForFinished();

            if (!replay.isError()) {
                const QJsonObject obj = QJsonDocument::fromJson(replay.value().toUtf8()).object();
                auto user_ptr = m_model->findUserByUid(static_cast<uint>(obj["Uid"].toInt()));

                m_model->updateCurrentUser(user_ptr);
            }
        });
    }

    //??????????????????
    m_resetSessionTimer->setInterval(15000);
    if (QGSettings::isSchemaInstalled("com.deepin.dde.session-shell")) {
         QGSettings gsetting("com.deepin.dde.session-shell", "/com/deepin/dde/session-shell/", this);
         if(gsetting.keys().contains("authResetTime")){
             int resetTime = gsetting.get("auth-reset-time").toInt();
             if(resetTime > 0)
                m_resetSessionTimer->setInterval(resetTime);
         }
    }

    m_resetSessionTimer->setSingleShot(true);
    connect(m_resetSessionTimer, &QTimer::timeout, this, [=] {
        endAuthentication(m_account, AuthTypeAll);
        m_model->updateAuthStatus(AuthTypeAll, StatusCodeCancel, "Cancel");
        destoryAuthentication(m_account);
        createAuthentication(m_account);
    });
    m_xEventInter->RegisterFullScreen();
}

GreeterWorkek::~GreeterWorkek()
{
}

void GreeterWorkek::initConnections()
{
    /* greeter */
    connect(m_greeter, &QLightDM::Greeter::showPrompt, this, &GreeterWorkek::showPrompt);
    connect(m_greeter, &QLightDM::Greeter::showMessage, this, &GreeterWorkek::showMessage);
    connect(m_greeter, &QLightDM::Greeter::authenticationComplete, this, &GreeterWorkek::authenticationComplete);
    /* com.deepin.daemon.Accounts */
    connect(m_accountsInter, &AccountsInter::UserAdded, m_model, static_cast<void (SessionBaseModel::*)(const QString &)>(&SessionBaseModel::addUser));
    connect(m_accountsInter, &AccountsInter::UserDeleted, m_model, static_cast<void (SessionBaseModel::*)(const QString &)>(&SessionBaseModel::removeUser));
    // connect(m_accountsInter, &AccountsInter::UserListChanged, m_model, &SessionBaseModel::updateUserList); // UserListChanged?????????????????? ??????UserAdded???UserDeleted????????????
    connect(m_loginedInter, &LoginedInter::LastLogoutUserChanged, m_model, static_cast<void (SessionBaseModel::*)(const uid_t)>(&SessionBaseModel::updateLastLogoutUser));
    connect(m_loginedInter, &LoginedInter::UserListChanged, m_model, &SessionBaseModel::updateLoginedUserList);
    /* com.deepin.daemon.Authenticate */
    connect(m_authFramework, &DeepinAuthFramework::FramworkStateChanged, m_model, &SessionBaseModel::updateFrameworkState);
    connect(m_authFramework, &DeepinAuthFramework::LimitsInfoChanged, this, [this](const QString &account) {
        if (account == m_model->currentUser()->name()) {
            m_model->updateLimitedInfo(m_authFramework->GetLimitedInfo(account));
        }
    });
    connect(m_authFramework, &DeepinAuthFramework::SupportedEncryptsChanged, m_model, &SessionBaseModel::updateSupportedEncryptionType);
    connect(m_authFramework, &DeepinAuthFramework::SupportedMixAuthFlagsChanged, m_model, &SessionBaseModel::updateSupportedMixAuthFlags);
    /* com.deepin.daemon.Authenticate.Session */
    connect(m_authFramework, &DeepinAuthFramework::AuthStatusChanged, this, [=](const int type, const int status, const QString &message) {
        qDebug() << "DeepinAuthFramework::AuthStatusChanged:" << type << status << message;
        if (m_model->getAuthProperty().MFAFlag) {
            if (type == AuthTypeAll) {
                switch (status) {
                case StatusCodeSuccess:
                    m_model->updateAuthStatus(type, status, message);
                    m_resetSessionTimer->stop();
                    if (m_greeter->inAuthentication()) {
                        m_greeter->respond(m_authFramework->AuthSessionPath(m_account) + QString(";") + m_password);
                    } else {
                        qWarning() << "The lightdm is not in authentication!";
                    }
                    break;
                case StatusCodeCancel:
                    m_model->updateAuthStatus(type, status, message);
                    destoryAuthentication(m_account);
                    break;
                default:
                    break;
                }
            } else {
                switch (status) {
                case StatusCodeSuccess:
                    if (m_model->currentModeState() != SessionBaseModel::ModeStatus::PasswordMode)
                        m_model->setCurrentModeState(SessionBaseModel::ModeStatus::PasswordMode);
                    m_resetSessionTimer->start();
                    m_model->updateAuthStatus(type, status, message);
                    break;
                case StatusCodeFailure:
                case StatusCodeLocked:
                    if (m_model->currentModeState() != SessionBaseModel::ModeStatus::PasswordMode)
                        m_model->setCurrentModeState(SessionBaseModel::ModeStatus::PasswordMode);
                    endAuthentication(m_account, type);
                    // TODO: ??????????????????,????????????,Bug 89056
                    QTimer::singleShot(50, this, [=] {
                        m_model->updateAuthStatus(type, status, message);
                    });
                    break;
                case StatusCodeTimeout:
                case StatusCodeError:
                    m_model->updateAuthStatus(type, status, message);
                    endAuthentication(m_account, type);
                    break;
                default:
                    m_model->updateAuthStatus(type, status, message);
                    break;
                }
            }
        } else {
            if (m_model->currentModeState() != SessionBaseModel::ModeStatus::PasswordMode && (status == StatusCodeSuccess || status == StatusCodeFailure))
                m_model->setCurrentModeState(SessionBaseModel::ModeStatus::PasswordMode);
            m_model->updateAuthStatus(type, status, message);
        }
    });
    connect(m_authFramework, &DeepinAuthFramework::FactorsInfoChanged, m_model, &SessionBaseModel::updateFactorsInfo);
    connect(m_authFramework, &DeepinAuthFramework::FuzzyMFAChanged, m_model, &SessionBaseModel::updateFuzzyMFA);
    connect(m_authFramework, &DeepinAuthFramework::MFAFlagChanged, m_model, &SessionBaseModel::updateMFAFlag);
    connect(m_authFramework, &DeepinAuthFramework::PINLenChanged, m_model, &SessionBaseModel::updatePINLen);
    connect(m_authFramework, &DeepinAuthFramework::PromptChanged, m_model, &SessionBaseModel::updatePrompt);
    /* org.freedesktop.login1.Session */
    connect(m_login1SessionSelf, &Login1SessionSelf::ActiveChanged, this, [=](bool active) {
        qDebug() << "Login1SessionSelf::ActiveChanged:" << active;
        if (m_model->currentUser() == nullptr || m_model->currentUser()->name().isEmpty()) {
            return;
        }
        if (active) {
            if (!m_model->isServerModel() && !m_model->currentUser()->isNoPasswordLogin()) {
                createAuthentication(m_model->currentUser()->name());
            }
            if (!m_model->isServerModel() && m_model->currentUser()->isNoPasswordLogin() && m_model->currentUser()->isAutomaticLogin()) {
                m_greeter->authenticate(m_model->currentUser()->name());
            }
        } else {
            endAuthentication(m_account, AuthTypeAll);
            destoryAuthentication(m_account);
        }
    });
    /* org.freedesktop.login1.Manager */
    connect(m_login1Inter, &DBusLogin1Manager::PrepareForSleep, this, [=](bool isSleep) {
        if (isSleep) {
            endAuthentication(m_account, AuthTypeAll);
        } else {
            createAuthentication(m_account);
        }
    });
    /* com.deepin.dde.LockService */
    connect(m_lockInter, &DBusLockService::UserChanged, this, [=](const QString &json) {
        qDebug() << "DBusLockService::UserChanged:" << json;
        m_resetSessionTimer->stop();
        m_model->updateCurrentUser(json);
        std::shared_ptr<User> user_ptr = m_model->currentUser();
        const QString &account = user_ptr->name();
        if (user_ptr.get()->isNoPasswordLogin()) {
            emit m_model->authTypeChanged(AuthTypeNone);
            m_account = account;
        }
        emit m_model->switchUserFinished();
    });

    connect(m_xEventInter, &XEventInter::CursorMove, this, [=] {
        if(m_model->visible() && m_resetSessionTimer->isActive()){
            m_resetSessionTimer->start();
        }
    });

    connect(m_xEventInter, &XEventInter::KeyRelease, this, [=] {
        if(m_model->visible() && m_resetSessionTimer->isActive()){
            m_resetSessionTimer->start();
        }
    });
    /* model */
    connect(m_model, &SessionBaseModel::authTypeChanged, this, [=](const int type) {
        if (type > 0 && !m_model->currentUser()->limitsInfo()->value(type).locked) {
            startAuthentication(m_account, m_model->getAuthProperty().AuthType);
        } else {
            QTimer::singleShot(10, this, [=] {
                m_model->updateLimitedInfo(m_authFramework->GetLimitedInfo(m_account));
            });
        }
    });
    connect(m_model, &SessionBaseModel::onPowerActionChanged, this, &GreeterWorkek::doPowerAction);
    connect(m_model, &SessionBaseModel::lockLimitFinished, this, [=] {
        if (!m_greeter->inAuthentication()) {
            m_greeter->authenticate(m_account);
        }
        startAuthentication(m_account, m_model->getAuthProperty().AuthType);
    });
    connect(m_model, &SessionBaseModel::currentUserChanged, this, &GreeterWorkek::recoveryUserKBState);
    connect(m_model, &SessionBaseModel::visibleChanged, this, [=] (bool visible) {
        if (visible) {
            if (!m_model->isServerModel() && !m_model->currentUser()->isNoPasswordLogin()) {
                createAuthentication(m_model->currentUser()->name());
            }
        } else {
            m_resetSessionTimer->stop();
        }
    });
    /* others */
    connect(KeyboardMonitor::instance(), &KeyboardMonitor::numlockStatusChanged, this, [=](bool on) {
        saveNumlockStatus(m_model->currentUser(), on);
    });
}

void GreeterWorkek::initData()
{
    /* com.deepin.daemon.Accounts */
    m_model->updateUserList(m_accountsInter->userList());
    m_model->updateLastLogoutUser(m_loginedInter->lastLogoutUser());
    m_model->updateLoginedUserList(m_loginedInter->userList());
    /* com.deepin.dde.LockService */
    m_model->updateCurrentUser(m_lockInter->CurrentUser());
    /* com.deepin.daemon.Authenticate */
    m_model->updateFrameworkState(m_authFramework->GetFrameworkState());
    m_model->updateSupportedEncryptionType(m_authFramework->GetSupportedEncrypts());
    m_model->updateSupportedMixAuthFlags(m_authFramework->GetSupportedMixAuthFlags());
    m_model->updateLimitedInfo(m_authFramework->GetLimitedInfo(m_model->currentUser()->name()));
}

void GreeterWorkek::initConfiguration()
{
    const QString &switchUserButtonValue {valueByQSettings<QString>("Lock", "showSwitchUserButton", "ondemand")};
    m_model->setAlwaysShowUserSwitchButton(switchUserButtonValue == "always");
    m_model->setAllowShowUserSwitchButton(switchUserButtonValue == "ondemand");

    m_model->setActiveDirectoryEnabled(valueByQSettings<bool>("", "loginPromptInput", false));

    checkPowerInfo();

    if (QFile::exists("/etc/deepin/no_suspend")) {
        m_model->setCanSleep(false);
    }

    //??????????????????????????????????????????????????????????????????????????????????????????????????? 0???????????? 1???????????? 2??????????????????????????????????????????key???
    if (m_model->currentUser() != nullptr && UserNumlockSettings(m_model->currentUser()->name()).get(2) == 2) {
        PowerInter powerInter("com.deepin.system.Power", "/com/deepin/system/Power", QDBusConnection::systemBus(), this);
        if (powerInter.hasBattery()) {
            saveNumlockStatus(m_model->currentUser(), 0);
        } else {
            saveNumlockStatus(m_model->currentUser(), 1);
        }
        recoveryUserKBState(m_model->currentUser());
    }
}

void GreeterWorkek::doPowerAction(const SessionBaseModel::PowerAction action)
{
    switch (action) {
    case SessionBaseModel::PowerAction::RequireShutdown:
        m_login1Inter->PowerOff(true);
        break;
    case SessionBaseModel::PowerAction::RequireRestart:
        m_login1Inter->Reboot(true);
        break;
    case SessionBaseModel::PowerAction::RequireSuspend:
        if (m_model->currentModeState() != SessionBaseModel::ModeStatus::PasswordMode)
            m_model->setCurrentModeState(SessionBaseModel::ModeStatus::PasswordMode);
        m_login1Inter->Suspend(true);
        break;
    case SessionBaseModel::PowerAction::RequireHibernate:
        if (m_model->currentModeState() != SessionBaseModel::ModeStatus::PasswordMode)
            m_model->setCurrentModeState(SessionBaseModel::ModeStatus::PasswordMode);
        m_login1Inter->Hibernate(true);
        break;
    default:
        break;
    }

    m_model->setPowerAction(SessionBaseModel::PowerAction::None);
}

/**
 * @brief ????????????????????????????????? LockService ??????
 *
 * @param user
 */
void GreeterWorkek::setCurrentUser(const std::shared_ptr<User> user)
{
    QJsonObject json;
    json["Name"] = user->name();
    json["Type"] = user->type();
    json["Uid"] = static_cast<int>(user->uid());
    m_lockInter->SwitchToUser(QString(QJsonDocument(json).toJson(QJsonDocument::Compact))).waitForFinished();
}

void GreeterWorkek::switchToUser(std::shared_ptr<User> user)
{
    if (user->name() == m_account) {
        return;
    }
    qInfo() << "switch user from" << m_account << " to " << user->name() << user->uid() << user->isLogin();
    endAuthentication(m_account, AuthTypeAll);

    if (user->uid() == INT_MAX) {
        m_greeter->authenticate();
        m_model->setAuthType(AuthTypeNone);
    }
    setCurrentUser(user);
    if (user->isLogin()) { // switch to user Xorg
        m_greeter->authenticate();
        QProcess::startDetached("dde-switchtogreeter", QStringList() << user->name());
    } else {
        m_model->updateAuthStatus(AuthTypeAll, StatusCodeCancel, "Cancel");
        destoryAuthentication(m_account);
        m_model->updateCurrentUser(user);
        if (!user->isNoPasswordLogin()) {
            createAuthentication(user->name());
        }
    }
}

/**
 * @brief ??????????????????????????????
 *
 * @param password
 */
void GreeterWorkek::authUser(const QString &password)
{
    // auth interface
    std::shared_ptr<User> user = m_model->currentUser();
    m_password = password;

    qWarning() << "greeter authenticate user: " << m_greeter->authenticationUser() << " current user: " << user->name();
    if (m_greeter->authenticationUser() != user->name()) {
        resetLightdmAuth(user, 100, false);
    } else {
        if (m_greeter->inAuthentication()) {
            // m_authFramework->AuthenticateByUser(user);
            // m_authFramework->Responsed(password);
            // m_greeter->respond(password);
        } else {
            m_greeter->authenticate(user->name());
        }
    }
}

/**
 * @brief ??????????????????
 * ?????????????????????dbus????????????user?????????????????????????????????????????????????????????????????????????????????????????????
 * @param account
 */
void GreeterWorkek::createAuthentication(const QString &account)
{
    qDebug() << "GreeterWorkek::createAuthentication:" << account;
    m_account = account;
    if (account.isEmpty()) {
        m_model->setAuthType(AuthTypeNone);
        return;
    }
    m_retryAuth = false;
    std::shared_ptr<User> user_ptr = m_model->findUserByName(account);
    if (user_ptr) {
        user_ptr->updatePasswordExpiredInfo();
    }
    switch (m_model->getAuthProperty().FrameworkState) {
    case 0:
        m_authFramework->CreateAuthController(account, m_authFramework->GetSupportedMixAuthFlags(), AppTypeLogin);
        m_authFramework->SetAuthQuitFlag(account, DeepinAuthFramework::ManualQuit);
        if (m_model->getAuthProperty().MFAFlag) {
            if (!m_authFramework->SetPrivilegesEnable(account, QString("/usr/sbin/lightdm"))) {
                qWarning() << "Failed to set privileges!";
            }
            m_greeter->authenticate(account);
        }
        break;
    default:
        m_model->updateFactorsInfo(MFAInfoList());
        break;
    }
}

/**
 * @brief ??????????????????
 *
 * @param account
 */
void GreeterWorkek::destoryAuthentication(const QString &account)
{
    qDebug() << "GreeterWorkek::destoryAuthentication:" << account;
    switch (m_model->getAuthProperty().FrameworkState) {
    case 0:
        m_authFramework->DestoryAuthController(account);
        break;
    default:
        break;
    }
}

/**
 * @brief ??????????????????    -- ????????????????????????????????????????????????
 *
 * @param account   ??????
 * @param authType  ??????????????????????????????????????????
 * @param timeout   ??????????????????????????? -1???
 */
void GreeterWorkek::startAuthentication(const QString &account, const int authType)
{
    qDebug() << "GreeterWorkek::startAuthentication:" << account << authType;
    switch (m_model->getAuthProperty().FrameworkState) {
    case 0:
        if (m_model->getAuthProperty().MFAFlag) {
            m_authFramework->StartAuthentication(account, authType, -1);
        } else {
            m_greeter->authenticate(account);
        }
        break;
    default:
        m_greeter->authenticate(account);
        break;
    }
    QTimer::singleShot(10, this, [=] {
        m_model->updateLimitedInfo(m_authFramework->GetLimitedInfo(account));
    });
}

/**
 * @brief ??????????????????????????????
 *
 * @param account   ??????
 * @param authType  ????????????
 * @param token     ??????
 */
void GreeterWorkek::sendTokenToAuth(const QString &account, const int authType, const QString &token)
{
    qDebug() << "GreeterWorkek::sendTokenToAuth:" << account << authType;
    switch (m_model->getAuthProperty().FrameworkState) {
    case 0:
        if (m_model->getAuthProperty().MFAFlag) {
            m_authFramework->SendTokenToAuth(account, authType, token);
            if (authType == AuthTypePassword) {
                m_password = token; // ?????????????????????
            }
        } else {
            m_greeter->respond(token);
        }
        break;
    default:
        m_greeter->respond(token);
        break;
    }
}

/**
 * @brief ???????????????????????????????????????????????????????????????
 *
 * @param account   ??????
 * @param authType  ????????????
 */
void GreeterWorkek::endAuthentication(const QString &account, const int authType)
{
    qDebug() << "GreeterWorkek::endAuthentication:" << account << authType;
    switch (m_model->getAuthProperty().FrameworkState) {
    case 0:
        if (authType == AuthTypeAll) {
            m_authFramework->SetPrivilegesDisable(account);
        }
        m_authFramework->EndAuthentication(account, authType);
        break;
    default:
        break;
    }
}

/**
 * @brief ???????????????????????????????????????
 *
 * @param account
 */
void GreeterWorkek::checkAccount(const QString &account)
{
    qDebug() << "GreeterWorkek::checkAccount:" << account;
    if (m_greeter->authenticationUser() == account) {
        return;
    }
    const QString userPath = m_accountsInter->FindUserByName(account);
    std::shared_ptr<User> user_ptr;
    if (!userPath.startsWith("/")) {
        if (account.startsWith("@")) {
            user_ptr = std::make_shared<ADDomainUser>(INT_MAX - 1);
            dynamic_cast<ADDomainUser *>(user_ptr.get())->setName(account);
            dynamic_cast<ADDomainUser *>(user_ptr.get())->setFullName(account.midRef(QString("@").size()).toString());
        } else {
            qWarning() << userPath;
            onDisplayErrorMsg(tr("Wrong account"));
            m_model->setAuthType(AuthTypeNone);
            m_greeter->authenticate();
            return;
        }
    } else {
        user_ptr = std::make_shared<NativeUser>(userPath);
    }
    m_model->updateCurrentUser(user_ptr);
    if (user_ptr->isNoPasswordLogin()) {
        m_greeter->authenticate(account);
    } else {
        m_resetSessionTimer->stop();
        endAuthentication(m_account, AuthTypeAll);
        m_model->updateAuthStatus(AuthTypeAll, StatusCodeCancel, "Cancel");
        destoryAuthentication(m_account);
        createAuthentication(account);
    }
}

void GreeterWorkek::checkDBusServer(bool isvalid)
{
    if (isvalid) {
        m_accountsInter->userList();
    } else {
        // FIXME: ??????????????????????????????QThread::msleep?????????????????????
        QTimer::singleShot(300, this, [=] {
            qWarning() << "com.deepin.daemon.Accounts is not start, rechecking!";
            checkDBusServer(m_accountsInter->isValid());
        });
    }
}

void GreeterWorkek::oneKeyLogin()
{
    // ?????????????????????
    QDBusPendingCall call = m_authenticateInter->PreOneKeyLogin(AuthFlag::Fingerprint);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, [=] {
        if (!call.isError()) {
            QDBusReply<QString> reply = call.reply();
            qWarning() << "one key Login User Name is : " << reply.value();

            auto user_ptr = m_model->findUserByName(reply.value());
            if (user_ptr.get() != nullptr && reply.isValid()) {
                m_model->updateCurrentUser(user_ptr);
                userAuthForLightdm(user_ptr);
            }
        } else {
            qWarning() << "pre one key login: " << call.error().message();
        }

        watcher->deleteLater();
    });
}

void GreeterWorkek::userAuthForLightdm(std::shared_ptr<User> user)
{
    if (user.get() != nullptr && !user->isNoPasswordLogin()) {
        //??????????????????600ms???????????????????????????
        resetLightdmAuth(user, 100, true);
    }
}

/**
 * @brief ??????????????????
 *
 * @param text
 * @param type
 */
void GreeterWorkek::showPrompt(const QString &text, const QLightDM::Greeter::PromptType type)
{
    qDebug() << "GreeterWorkek::showPrompt:" << text << type;
    switch (type) {
    case QLightDM::Greeter::PromptTypeSecret:
        m_retryAuth = true;
        m_model->updateAuthStatus(AuthTypeSingle, StatusCodePrompt, text);
        break;
    case QLightDM::Greeter::PromptTypeQuestion:
        break;
    }
}

/**
 * @brief ??????????????????/???????????????
 *
 * @param text
 * @param type
 */
void GreeterWorkek::showMessage(const QString &text, const QLightDM::Greeter::MessageType type)
{
    qDebug() << "GreeterWorkek::showMessage:" << text << type;
    switch (type) {
    case QLightDM::Greeter::MessageTypeInfo:
        m_model->updateAuthStatus(AuthTypeSingle, StatusCodeSuccess, text);
        break;
    case QLightDM::Greeter::MessageTypeError:
        if (m_retryAuth && m_model->getAuthProperty().MFAFlag) {
            m_model->updateMFAFlag(false);
            m_model->setAuthType(AuthTypeSingle);
        }
        m_retryAuth = false;
        m_model->updateAuthStatus(AuthTypeSingle, StatusCodeFailure, text);
        break;
    }
}

/**
 * @brief ????????????
 */
void GreeterWorkek::authenticationComplete()
{
    qInfo() << "authentication complete, authenticated " << m_greeter->isAuthenticated() << m_retryAuth;

    if (!m_greeter->isAuthenticated()) {
        if (m_retryAuth && !m_model->getAuthProperty().MFAFlag) {
            showMessage(tr("Wrong Password"), QLightDM::Greeter::MessageTypeError);
            m_greeter->authenticate(m_account);
        }
        return;
    }

    emit m_model->authFinished(m_greeter->isAuthenticated());

    m_password.clear();

    switch (m_model->powerAction()) {
    case SessionBaseModel::PowerAction::RequireRestart:
        m_login1Inter->Reboot(true);
        return;
    case SessionBaseModel::PowerAction::RequireShutdown:
        m_login1Inter->PowerOff(true);
        return;
    default:
        break;
    }

    qInfo() << "start session = " << m_model->sessionKey();

    auto startSessionSync = [=]() {
        setCurrentUser(m_model->currentUser());
        m_greeter->startSessionSync(m_model->sessionKey());
    };

#ifndef DISABLE_LOGIN_ANI
    QTimer::singleShot(1000, this, startSessionSync);
#else
    startSessionSync();
#endif
    endAuthentication(m_account, AuthTypeAll);
    destoryAuthentication(m_account);
}

void GreeterWorkek::saveNumlockStatus(std::shared_ptr<User> user, const bool &on)
{
    UserNumlockSettings(user->name()).set(on);
}

void GreeterWorkek::recoveryUserKBState(std::shared_ptr<User> user)
{
    //FIXME(lxz)
    //    PowerInter powerInter("com.deepin.system.Power", "/com/deepin/system/Power", QDBusConnection::systemBus(), this);
    //    const BatteryPresentInfo info = powerInter.batteryIsPresent();
    //    const bool defaultValue = !info.values().first();
    if (user.get() == nullptr)
        return;

    const bool enabled = UserNumlockSettings(user->name()).get(false);

    qWarning() << "restore numlock status to " << enabled;

    // Resync numlock light with numlock status
    bool cur_numlock = KeyboardMonitor::instance()->isNumlockOn();
    KeyboardMonitor::instance()->setNumlockStatus(!cur_numlock);
    KeyboardMonitor::instance()->setNumlockStatus(cur_numlock);

    KeyboardMonitor::instance()->setNumlockStatus(enabled);
}

//TODO ????????????
void GreeterWorkek::onDisplayErrorMsg(const QString &msg)
{
    emit m_model->authFaildTipsMessage(msg);
}

void GreeterWorkek::onDisplayTextInfo(const QString &msg)
{
    emit m_model->authFaildMessage(msg);
}

void GreeterWorkek::onPasswordResult(const QString &msg)
{
    //onUnlockFinished(!msg.isEmpty());

    //if(msg.isEmpty()) {
    //    m_authFramework->AuthenticateByUser(m_model->currentUser());
    //}
}

void GreeterWorkek::resetLightdmAuth(std::shared_ptr<User> user, int delay_time, bool is_respond)
{
    QTimer::singleShot(delay_time, this, [=] {
        // m_greeter->authenticate(user->name());
        // m_authFramework->AuthenticateByUser(user);
        if (is_respond && !m_password.isEmpty()) {
            // if (m_framworkState == 0) {
            //??????
            // } else if (m_framworkState == 1) {
            //???????????????
            // m_authFramework->Responsed(m_password);
            // } else if (m_framworkState == 2) {
            //????????????pam
            // m_greeter->respond(m_password);
            // }
        }
    });
}
