#include "authcommon.h"
#include "lockworker.h"

#include "sessionbasemodel.h"
#include "userinfo.h"

#include <DSysInfo>

#include <QApplication>
#include <QDebug>
#include <QProcess>
#include <QRegularExpression>

#include <grp.h>
#include <libintl.h>
#include <pwd.h>
#include <unistd.h>

#define DOMAIN_BASE_UID 10000

using namespace Auth;
using namespace AuthCommon;
DCORE_USE_NAMESPACE

LockWorker::LockWorker(SessionBaseModel *const model, QObject *parent)
    : AuthInterface(model, parent)
    , m_authenticating(false)
    , m_isThumbAuth(false)
    , m_authFramework(new DeepinAuthFramework(this, this))
    , m_lockInter(new DBusLockService("com.deepin.dde.LockService", "/com/deepin/dde/LockService", QDBusConnection::systemBus(), this))
    , m_hotZoneInter(new DBusHotzone("com.deepin.daemon.Zone", "/com/deepin/daemon/Zone", QDBusConnection::sessionBus(), this))
    , m_resetSessionTimer(new QTimer(this))
    , m_sessionManagerInter(new SessionManagerInter("com.deepin.SessionManager", "/com/deepin/SessionManager", QDBusConnection::sessionBus(), this))
    , m_switchosInterface(new HuaWeiSwitchOSInterface("com.huawei", "/com/huawei/switchos", QDBusConnection::sessionBus(), this))
    , m_accountsInter(new AccountsInter("com.deepin.daemon.Accounts", "/com/deepin/daemon/Accounts", QDBusConnection::systemBus(), this))
    , m_loginedInter(new LoginedInter("com.deepin.daemon.Accounts", "/com/deepin/daemon/Logined", QDBusConnection::systemBus(), this))
    , m_xEventInter(new XEventInter("com.deepin.api.XEventMonitor","/com/deepin/api/XEventMonitor",QDBusConnection::sessionBus(), this))
{
    initConnections();
    initData();
    initConfiguration();

    if (DSysInfo::deepinType() == DSysInfo::DeepinServer || m_model->isActiveDirectoryDomain()) {
        std::shared_ptr<User> user(new User());
        m_model->setIsServerModel(DSysInfo::deepinType() == DSysInfo::DeepinServer || !m_model->isActiveDirectoryDomain());
        m_model->addUser(user);
    }

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
        destoryAuthentication(m_account);
        createAuthentication(m_account);
    });

    m_xEventInter->RegisterFullScreen();
}

LockWorker::~LockWorker()
{
}

/**
 * @brief ?????????????????????
 */
void LockWorker::initConnections()
{
    /* com.deepin.daemon.Accounts */
    connect(m_accountsInter, &AccountsInter::UserAdded, m_model, static_cast<void (SessionBaseModel::*)(const QString &)>(&SessionBaseModel::addUser));
    connect(m_accountsInter, &AccountsInter::UserDeleted, m_model, static_cast<void (SessionBaseModel::*)(const QString &)>(&SessionBaseModel::removeUser));
    // connect(m_accountsInter, &AccountsInter::UserListChanged, m_model, &SessionBaseModel::updateUserList);  // UserListChanged?????????????????? ??????UserAdded???UserDeleted????????????
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
    connect(m_authFramework, &DeepinAuthFramework::FuzzyMFAChanged, m_model, &SessionBaseModel::updateFuzzyMFA);
    connect(m_authFramework, &DeepinAuthFramework::MFAFlagChanged, m_model, &SessionBaseModel::updateMFAFlag);
    connect(m_authFramework, &DeepinAuthFramework::PINLenChanged, m_model, &SessionBaseModel::updatePINLen);
    connect(m_authFramework, &DeepinAuthFramework::PromptChanged, m_model, &SessionBaseModel::updatePrompt);
    connect(m_authFramework, &DeepinAuthFramework::AuthStatusChanged, this, [=](const int type, const int status, const QString &message) {
        qDebug() << "DeepinAuthFramework::AuthStatusChanged:" << type << status << message << m_model->getAuthProperty().MFAFlag;
        if (m_model->getAuthProperty().MFAFlag) {
            if (type == AuthTypeAll) {
                switch (status) {
                case StatusCodeSuccess:
                    m_model->updateAuthStatus(type, status, message);
                    destoryAuthentication(m_account);
                    onUnlockFinished(true);
                    m_resetSessionTimer->stop();
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
                    if (m_model->currentModeState() != SessionBaseModel::ModeStatus::PasswordMode
                        && m_model->currentModeState() != SessionBaseModel::ModeStatus::ConfirmPasswordMode) {
                        m_model->setCurrentModeState(SessionBaseModel::ModeStatus::PasswordMode);
                    }
                    m_resetSessionTimer->start();
                    m_model->updateAuthStatus(type, status, message);
                    break;
                case StatusCodeFailure:
                case StatusCodeLocked:
                    if (m_model->currentModeState() != SessionBaseModel::ModeStatus::PasswordMode
                        && m_model->currentModeState() != SessionBaseModel::ModeStatus::ConfirmPasswordMode) {
                        m_model->setCurrentModeState(SessionBaseModel::ModeStatus::PasswordMode);
                    }
                    endAuthentication(m_account, type);
                    // TODO: ??????????????????,????????????,Bug 89056
                    QTimer::singleShot(50, this, [=]{
                        m_model->updateAuthStatus(type, status, message);
                    });
                    break;
                case StatusCodeTimeout:
                case StatusCodeError:
                    endAuthentication(m_account, type);
                    m_model->updateAuthStatus(type, status, message);
                    break;
                default:
                    m_model->updateAuthStatus(type, status, message);
                    break;
                }
            }
        } else {
            switch (status) {
            case StatusCodeSuccess:
                onUnlockFinished(true);
                if (m_model->currentModeState() != SessionBaseModel::ModeStatus::PasswordMode
                    && m_model->currentModeState() != SessionBaseModel::ModeStatus::ConfirmPasswordMode) {
                    m_model->setCurrentModeState(SessionBaseModel::ModeStatus::PasswordMode);
                }
                break;
            case StatusCodeFailure:
                if (m_model->currentModeState() != SessionBaseModel::ModeStatus::PasswordMode
                    && m_model->currentModeState() != SessionBaseModel::ModeStatus::ConfirmPasswordMode) {
                    m_model->setCurrentModeState(SessionBaseModel::ModeStatus::PasswordMode);
                }
                break;
            case StatusCodeCancel:
                destoryAuthentication(m_account);
                break;
            }
            m_model->updateAuthStatus(type, status, message);
        }
    });
    connect(m_authFramework, &DeepinAuthFramework::FactorsInfoChanged, m_model, &SessionBaseModel::updateFactorsInfo);
    /* com.deepin.dde.LockService */
    connect(m_lockInter, &DBusLockService::UserChanged, this, [=](const QString &json) {
        qDebug() << "DBusLockService::UserChanged:" << json;
        emit m_model->switchUserFinished();
        m_resetSessionTimer->stop();
    });
    connect(m_lockInter, &DBusLockService::Event, this, &LockWorker::lockServiceEvent);
    /* com.deepin.SessionManager */
    connect(m_sessionManagerInter, &SessionManagerInter::Unlock, this, [=] {
        m_authenticating = false;
        m_password.clear();
        emit m_model->authFinished(true);
    });
    /* org.freedesktop.login1.Session */
    connect(m_login1SessionSelf, &Login1SessionSelf::ActiveChanged, this, [=](bool active) {
        qDebug() << "DBusLockService::ActiveChanged:" << active;
        if (active) {
            createAuthentication(m_account);
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
        emit m_model->prepareForSleep(isSleep);
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
    connect(m_model, &SessionBaseModel::onPowerActionChanged, this, &LockWorker::doPowerAction);
    connect(m_model, &SessionBaseModel::visibleChanged, this, [=] (bool visible) {
        if (visible) {
            createAuthentication(m_model->currentUser()->name());
        } else {
            m_resetSessionTimer->stop();
            endAuthentication(m_account, AuthTypeAll);
        }
        setLocked(visible);
    });
    connect(m_model, &SessionBaseModel::onStatusChanged, this, [=] (SessionBaseModel::ModeStatus status) {
        if (status == SessionBaseModel::ModeStatus::PowerMode || status == SessionBaseModel::ModeStatus::ShutDownMode) {
            checkPowerInfo();
        }
    });
}

void LockWorker::initData()
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

void LockWorker::initConfiguration()
{
    const bool &LockNoPasswordValue = valueByQSettings<bool>("", "lockNoPassword", false);
    m_model->setIsLockNoPassword(LockNoPasswordValue);

    const QString &switchUserButtonValue = valueByQSettings<QString>("Lock", "showSwitchUserButton", "ondemand");
    m_model->setAlwaysShowUserSwitchButton(switchUserButtonValue == "always");
    m_model->setAllowShowUserSwitchButton(switchUserButtonValue == "ondemand");

    m_model->setActiveDirectoryEnabled(valueByQSettings<bool>("", "loginPromptInput", false));

    checkPowerInfo();
}

void LockWorker::doPowerAction(const SessionBaseModel::PowerAction action)
{
    switch (action) {
    case SessionBaseModel::PowerAction::RequireSuspend:
        m_model->setIsBlackModel(true);
        m_model->setCurrentModeState(SessionBaseModel::ModeStatus::PasswordMode);
        QTimer::singleShot(0, this, [=] {
            m_sessionManagerInter->RequestSuspend();
        });
        break;
    case SessionBaseModel::PowerAction::RequireHibernate:
        m_model->setIsBlackModel(true);
        m_model->setCurrentModeState(SessionBaseModel::ModeStatus::PasswordMode);
        QTimer::singleShot(0, this, [=] {
            m_sessionManagerInter->RequestHibernate();
        });
        break;
    case SessionBaseModel::PowerAction::RequireRestart:
        if (m_model->currentModeState() == SessionBaseModel::ModeStatus::ShutDownMode) {
            m_sessionManagerInter->RequestReboot();
        } else {
            m_model->setCurrentModeState(SessionBaseModel::ModeStatus::ConfirmPasswordMode);
        }
        return;
    case SessionBaseModel::PowerAction::RequireShutdown:
        if (m_model->currentModeState() == SessionBaseModel::ModeStatus::ShutDownMode) {
            m_sessionManagerInter->RequestShutdown();
        } else {
            m_model->setCurrentModeState(SessionBaseModel::ModeStatus::ConfirmPasswordMode);
        }
        return;
    case SessionBaseModel::PowerAction::RequireLock:
        m_sessionManagerInter->SetLocked(true);
        m_model->setCurrentModeState(SessionBaseModel::ModeStatus::PasswordMode);
        // m_authFramework->AuthenticateByUser(m_model->currentUser());
        break;
    case SessionBaseModel::PowerAction::RequireLogout:
        m_sessionManagerInter->RequestLogout();
        return;
    case SessionBaseModel::PowerAction::RequireSwitchSystem:
        m_switchosInterface->setOsFlag(!m_switchosInterface->getOsFlag());
        QTimer::singleShot(200, this, [=] { m_sessionManagerInter->RequestReboot(); });
        break;
    case SessionBaseModel::PowerAction::RequireSwitchUser:
        m_model->setCurrentModeState(SessionBaseModel::ModeStatus::UserMode);
        // m_authFramework->AuthenticateByUser(m_model->currentUser());
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
void LockWorker::setCurrentUser(const std::shared_ptr<User> user)
{
    QJsonObject json;
    json["Name"] = user->name();
    json["Type"] = user->type();
    json["Uid"] = static_cast<int>(user->uid());
    m_lockInter->SwitchToUser(QString(QJsonDocument(json).toJson(QJsonDocument::Compact))).waitForFinished();
}

void LockWorker::switchToUser(std::shared_ptr<User> user)
{
    if (user->name() == m_account) {
        return;
    }
    qInfo() << "switch user from" << m_account << " to " << user->name() << user->isLogin();
    endAuthentication(m_account, AuthTypeAll);
    setCurrentUser(user);
    if (user->isLogin()) {
        QProcess::startDetached("dde-switchtogreeter", QStringList() << user->name());
    } else {
        QProcess::startDetached("dde-switchtogreeter", QStringList());
    }
}

/**
 * @brief ?????? Locked ?????????
 *
 * @param locked
 */
void LockWorker::setLocked(const bool locked)
{
#ifndef QT_DEBUG
    if (m_model->currentModeState() != SessionBaseModel::ShutDownMode) {
        /** FIXME
         * ???????????????????????????????????????????????????????????????Locked?????????????????????true???????????????????????????????????????????????????????????????????????????????????????
         * ??????????????????????????????show??????????????????visibleChanged????????????????????????????????????????????????????????????arm??????????????????????????????????????????????????????????????????
         * ????????????????????????Locked=true??????????????????????????????????????????????????????
         * ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
         * ?????????????????????????????????????????????????????????CPU??????????????????????????????
         * ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
         * ????????????????????????????????????????????????????????????
         */
        QTimer::singleShot(200, this, [=] {
            m_sessionManagerInter->SetLocked(locked);
        });
    }
#else
    Q_UNUSED(locked)
#endif
}

/**
 * @brief ??????????????????????????????
 *
 * @param password
 */
void LockWorker::authUser(const QString &password)
{
    QString userPath = m_accountsInter->FindUserByName(m_model->currentUser()->name());
    if (!userPath.startsWith("/")) {
        qWarning() << userPath;
        onDisplayErrorMsg(userPath);
        return;
    }
    m_authFramework->CreateAuthenticate(m_model->currentUser()->name());
    m_authFramework->SendToken(password);
}

void LockWorker::enableZoneDetected(bool disable)
{
    m_hotZoneInter->EnableZoneDetected(disable);
}

/**
 * @brief ??????????????????
 *
 * @param msg
 */
void LockWorker::onDisplayErrorMsg(const QString &msg)
{
    emit m_model->authFaildTipsMessage(msg);
}

/**
 * @brief ??????????????????
 *
 * @param msg
 */
void LockWorker::onDisplayTextInfo(const QString &msg)
{
    m_authenticating = false;
    emit m_model->authFaildMessage(msg);
}

/**
 * @brief ??????????????????
 *
 * @param msg
 */
void LockWorker::onPasswordResult(const QString &msg)
{
    onUnlockFinished(!msg.isEmpty());

    if (msg.isEmpty()) {
        // m_authFramework->AuthenticateByUser(m_model->currentUser());
    }
}

/**
 * @brief ??????????????????
 * ?????????????????????dbus????????????user?????????????????????????????????????????????????????????????????????????????????????????????
 * @param account
 */
void LockWorker::createAuthentication(const QString &account)
{
    qDebug() << "LockWorker::createAuthentication:" << account;
    QString userPath = m_accountsInter->FindUserByName(account);
    if (!userPath.startsWith("/")) {
        qWarning() << userPath;
        return;
    }
    m_account = account;
    switch (m_model->getAuthProperty().FrameworkState) {
    case 0:
        m_authFramework->CreateAuthController(account, m_authFramework->GetSupportedMixAuthFlags(), AppTypeLock);
        break;
    default:
        m_authFramework->CreateAuthenticate(account);
        m_model->updateFactorsInfo(MFAInfoList());
        break;
    }
}

/**
 * @brief ??????????????????
 *
 * @param account
 */
void LockWorker::destoryAuthentication(const QString &account)
{
    qDebug() << "LockWorker::destoryAuthentication:" << account;
    switch (m_model->getAuthProperty().FrameworkState) {
    case 0:
        if (m_model->getAuthProperty().MFAFlag) {
            m_authFramework->DestoryAuthController(account);
        } else {
            m_authFramework->DestoryAuthController(account);
            m_authFramework->DestoryAuthenticate();
        }
        break;
    default:
        m_authFramework->DestoryAuthenticate();
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
void LockWorker::startAuthentication(const QString &account, const int authType)
{
    qDebug() << "LockWorker::startAuthentication:" << account << authType;
    switch (m_model->getAuthProperty().FrameworkState) {
    case 0:
        if (m_model->getAuthProperty().MFAFlag) {
            m_authFramework->StartAuthentication(account, authType, -1);
        } else {
            m_authFramework->CreateAuthenticate(account);
        }
        break;
    default:
        m_authFramework->CreateAuthenticate(account);
        break;
    }
    QTimer::singleShot(10, this, [=]  {
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
void LockWorker::sendTokenToAuth(const QString &account, const int authType, const QString &token)
{
    qDebug() << "LockWorker::sendTokenToAuth:" << account << authType;
    switch (m_model->getAuthProperty().FrameworkState) {
    case 0:
        if (m_model->getAuthProperty().MFAFlag) {
            m_authFramework->SendTokenToAuth(account, authType, token);
        } else {
            m_authFramework->SendToken(token);
        }
        break;
    default:
        m_authFramework->SendToken(token);
        break;
    }
}

/**
 * @brief ???????????????????????????????????????????????????????????????
 *
 * @param account   ??????
 * @param authType  ????????????
 */
void LockWorker::endAuthentication(const QString &account, const int authType)
{
    qDebug() << "LockWorker::endAuthentication:" << account << authType;
    switch (m_model->getAuthProperty().FrameworkState) {
    case 0:
        m_authFramework->EndAuthentication(account, authType);
        break;
    default:
        break;
    }
}

void LockWorker::lockServiceEvent(quint32 eventType, quint32 pid, const QString &username, const QString &message)
{
    if (!m_model->currentUser())
        return;

    if (username != m_model->currentUser()->name())
        return;

    // Don't show password prompt from standard pam modules since
    // we'll provide our own prompt or just not.
    const QString msg = message.simplified() == "Password:" ? "" : message;

    m_authenticating = false;

    if (msg == "Verification timed out") {
        m_isThumbAuth = true;
        emit m_model->authFaildMessage(tr("Fingerprint verification timed out, please enter your password manually"));
        return;
    }

    switch (eventType) {
    case DBusLockService::PromptQuestion:
        qWarning() << "prompt quesiton from pam: " << message;
        emit m_model->authFaildMessage(message);
        break;
    case DBusLockService::PromptSecret:
        qWarning() << "prompt secret from pam: " << message;
        if (m_isThumbAuth && !msg.isEmpty()) {
            emit m_model->authFaildMessage(msg);
        }
        break;
    case DBusLockService::ErrorMsg:
        qWarning() << "error message from pam: " << message;
        if (msg == "Failed to match fingerprint") {
            emit m_model->authFaildTipsMessage(tr("Failed to match fingerprint"));
            emit m_model->authFaildMessage("");
        }
        break;
    case DBusLockService::TextInfo:
        emit m_model->authFaildMessage(QString(dgettext("fprintd", message.toLatin1())));
        break;
    case DBusLockService::Failure:
        onUnlockFinished(false);
        break;
    case DBusLockService::Success:
        onUnlockFinished(true);
        break;
    default:
        break;
    }
}

void LockWorker::onUnlockFinished(bool unlocked)
{
    qWarning() << "LockWorker::onUnlockFinished -- unlocked status : " << unlocked;

    m_authenticating = false;

    //To Do: ???????????????????????????????????????????????????????????????
    if (m_model->currentModeState() == SessionBaseModel::ModeStatus::UserMode)
        m_model->setCurrentModeState(SessionBaseModel::ModeStatus::PasswordMode);

    //    if (!unlocked && m_authFramework->GetAuthType() == AuthFlag::Password) {
    //        qWarning() << "Authorization password failed!";
    //        emit m_model->authFaildTipsMessage(tr("Wrong Password"));
    //        return;
    //    }

    switch (m_model->powerAction()) {
    case SessionBaseModel::PowerAction::RequireRestart:
        if (unlocked) {
            m_sessionManagerInter->RequestReboot();
        }
        break;
    case SessionBaseModel::PowerAction::RequireShutdown:
        if (unlocked) {
            m_sessionManagerInter->RequestShutdown();
        }
        break;
    default:
        break;
    }

    emit m_model->authFinished(unlocked);
}
