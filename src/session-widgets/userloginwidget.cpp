/*
 * Copyright (C) 2019 ~ 2019 Deepin Technology Co., Ltd.
 *
 * Author:     lixin <lixin_cm@deepin.com>
 *
 * Maintainer: lixin <lixin_cm@deepin.com>
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

#include "authcommon.h"
#include "authsingle.h"
#include "userloginwidget.h"

#include "constants.h"
#include "dhidpihelper.h"
#include "dpasswordeditex.h"
#include "framedatabind.h"
#include "kblayoutwidget.h"
#include "keyboardmonitor.h"
#include "lockpasswordwidget.h"
#include "loginbutton.h"
#include "useravatar.h"
#include "userinfo.h"
#include "authenticationmodule.h"

#include <DFontSizeManager>
#include <DPalette>

#include <QAction>
#include <QImage>
#include <QPropertyAnimation>
#include <QVBoxLayout>

using namespace AuthCommon;

static const int BlurRectRadius = 15;
static const int LeftRightMargins = 16;
static const int NameSpace = 8;

static const QColor shutdownColor(QColor(247, 68, 68));
static const QColor disableColor(QColor(114, 114, 114));

UserLoginWidget::UserLoginWidget(const SessionBaseModel *model, const WidgetType widgetType, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_widgetType(widgetType)
    , m_blurEffectWidget(new DBlurEffectWidget(this))
    , m_userLoginLayout(new QVBoxLayout(this))
    , m_userAvatar(new UserAvatar(this))
    , m_nameWidget(new QWidget(this))
    , m_nameLabel(new QLabel(m_nameWidget))
    , m_loginStateLabel(new QLabel(m_nameWidget))
    , m_accountEdit(new DLineEditEx(this))
    , m_expiredStatusLabel(new QLabel(this))
    , m_lockButton(new DFloatingButton(DStyle::SP_LockElement, this))
    , m_singleAuth(nullptr)
    , m_passwordAuth(nullptr)
    , m_fingerprintAuth(nullptr)
    , m_faceAuth(nullptr)
    , m_ukeyAuth(nullptr)
    , m_activeDirectoryAuth(nullptr)
    , m_fingerVeinAuth(nullptr)
    , m_irisAuth(nullptr)
    , m_PINAuth(nullptr)

    , m_kbLayoutBorder(nullptr)

    , m_isLock(false)
    , m_loginState(true)
    , m_isSelected(false)
    , m_isLockNoPassword(false)
    , m_isAlertMessageShow(false)
    , m_aniTimer(new QTimer(this))
{
    if (widgetType == LoginType) {
        setMaximumWidth(280);                             // ????????????????????????????????????????????????????????????????????????
        setMinimumSize(UserFrameWidth, UserFrameHeight);  // ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????
    } else {
        setFixedSize(UserFrameWidth, UserFrameHeight); //???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
    }
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); // ?????????????????????fixed????????????????????????????????????????????????????????????
    setFocusPolicy(Qt::NoFocus);

    m_capslockMonitor = KeyboardMonitor::instance();
    m_capslockMonitor->start(QThread::LowestPriority);

    initUI();
    initConnections();

    m_accountEdit->installEventFilter(this);
    m_accountEdit->hide();

    if (m_widgetType == LoginType) {
        m_userAvatar->setAvatarSize(UserAvatar::AvatarLargeSize);
        m_loginStateLabel->hide();
        if (m_model->currentType() == SessionBaseModel::LightdmType && m_model->isServerModel()) {
            m_accountEdit->show();
            m_nameLabel->hide();
        }
    } else {
        m_userAvatar->setAvatarSize(UserAvatar::AvatarSmallSize);
        m_lockButton->hide();
    }
}

UserLoginWidget::~UserLoginWidget()
{
    for (auto it = m_registerFunctionIndexs.constBegin(); it != m_registerFunctionIndexs.constEnd(); ++it) {
        FrameDataBind::Instance()->unRegisterFunction(it.key(), it.value());
    }
    m_kbLayoutBorder->deleteLater();
}

/**
 * @brief ??????????????????
 */
void UserLoginWidget::initUI()
{
    if (m_widgetType == LoginType) {
        m_userLoginLayout->setContentsMargins(10, 0, 10, 0);
    } else {
        m_userLoginLayout->setContentsMargins(LeftRightMargins, 0, LeftRightMargins, 0);
    }
    m_userLoginLayout->setSpacing(10);
    /* ?????? */
    m_userAvatar->setFocusPolicy(Qt::NoFocus);
    m_userLoginLayout->addWidget(m_userAvatar, 0, Qt::AlignHCenter);
    /* ????????? */
    QHBoxLayout *nameLayout = new QHBoxLayout(m_nameWidget);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(NameSpace);
    QPixmap pixmap = DHiDPIHelper::loadNxPixmap(":/misc/images/select.svg");
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    m_loginStateLabel->setPixmap(pixmap);
    nameLayout->addWidget(m_loginStateLabel, 0, Qt::AlignVCenter | Qt::AlignRight);
    m_nameLabel->setTextFormat(Qt::TextFormat::PlainText);
    //LoginType???????????????????????????????????????UserListType?????????????????????????????????
    if (m_widgetType == LoginType) {
        m_nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    } else {
        m_nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_nameLabel->setFixedHeight(UserNameHeight);
    }
    DFontSizeManager::instance()->bind(m_nameLabel, DFontSizeManager::T2);
    QPalette palette = m_nameLabel->palette();
    palette.setColor(QPalette::WindowText, Qt::white);
    m_nameLabel->setPalette(palette);
    nameLayout->addWidget(m_nameLabel, 1, Qt::AlignVCenter | Qt::AlignLeft);
    m_userLoginLayout->addWidget(m_nameWidget, 0, Qt::AlignHCenter);
    /* ?????????????????? */
    m_accountEdit->setContextMenuPolicy(Qt::NoContextMenu);
    m_accountEdit->lineEdit()->setAlignment(Qt::AlignCenter);
    m_accountEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_accountEdit->setClearButtonEnabled(false);
    m_accountEdit->setPlaceholderText(tr("Account"));
    m_userLoginLayout->addWidget(m_accountEdit);
    /* ???????????????????????????????????? */
    if (m_widgetType != UserListType) {
        m_userLoginLayout->addSpacing(10);
    } else {
        m_userLoginLayout->addSpacing(20);
    }
    /* ?????????????????? */
    m_expiredStatusLabel->setWordWrap(true);
    m_expiredStatusLabel->setAlignment(Qt::AlignHCenter);
    m_userLoginLayout->addWidget(m_expiredStatusLabel, 0, Qt::AlignHCenter);
    m_expiredStatusLabel->hide();
    /* ???????????? */
    m_userLoginLayout->addWidget(m_lockButton, 0, Qt::AlignHCenter);
    /* ???????????? */
    m_blurEffectWidget->setMaskColor(DBlurEffectWidget::LightColor);
    m_blurEffectWidget->setMaskAlpha(76); // fix BUG 3400 ????????????????????????????????????30%
    m_blurEffectWidget->setBlurRectXRadius(BlurRectRadius);
    m_blurEffectWidget->setBlurRectYRadius(BlurRectRadius);
    /* ?????????????????? */
    m_kbLayoutBorder = new DArrowRectangle(DArrowRectangle::ArrowTop);
    m_kbLayoutWidget = new KbLayoutWidget(QStringList());
    m_kbLayoutBorder->setContent(m_kbLayoutWidget);
    m_kbLayoutBorder->setBackgroundColor(QColor(102, 102, 102)); //255*0.2
    m_kbLayoutBorder->setBorderColor(QColor(0, 0, 0, 0));
    m_kbLayoutBorder->setBorderWidth(0);
    m_kbLayoutBorder->setContentsMargins(0, 0, 0, 0);
    m_kbLayoutClip = new Dtk::Widget::DClipEffectWidget(m_kbLayoutBorder);
    updateClipPath();
}

void UserLoginWidget::initConnections()
{
    /* ?????? */
    connect(m_userAvatar, &UserAvatar::clicked, this, &UserLoginWidget::clicked);
    /* ????????? */
    connect(qGuiApp, &QGuiApplication::fontChanged, this, &UserLoginWidget::updateNameLabel);
    /* ?????????????????? */
    std::function<void(QVariant)> accountChanged = std::bind(&UserLoginWidget::onOtherPageAccountChanged, this, std::placeholders::_1);
    m_registerFunctionIndexs["UserLoginAccount"] = FrameDataBind::Instance()->registerFunction("UserLoginAccount", accountChanged);
    connect(m_accountEdit, &DLineEditEx::textChanged, this, [=] (const QString &value) {
        FrameDataBind::Instance()->updateValue("UserLoginAccount", value);
    });
    FrameDataBind::Instance()->refreshData("UserLoginAccount");
    connect(m_accountEdit, &DLineEditEx::returnPressed, this, [=] {
        if (m_accountEdit->isVisible() && !m_accountEdit->text().isEmpty()) {
            emit requestCheckAccount(m_accountEdit->text());
        }
    });
    connect(m_accountEdit, &DLineEditEx::editingFinished, this, [=] {
        emit m_accountEdit->returnPressed();
    });
    /* ???????????? */
    connect(m_lockButton, &DFloatingButton::clicked, this, [=] {
        if (m_model->currentUser()->isNoPasswordLogin()) {
            emit requestCheckAccount(m_model->currentUser()->name());
        } else if (m_passwordAuth == nullptr && m_ukeyAuth == nullptr && m_singleAuth == nullptr) {
            emit m_accountEdit->returnPressed();
        }
    });
    /* ?????????????????? */
    connect(m_kbLayoutWidget, &KbLayoutWidget::setButtonClicked, this, &UserLoginWidget::requestUserKBLayoutChanged);
    std::function<void(QVariant)> kblayoutChanged = std::bind(&UserLoginWidget::onOtherPageKBLayoutChanged, this, std::placeholders::_1);
    m_registerFunctionIndexs["UserLoginKBLayout"] = FrameDataBind::Instance()->registerFunction("UserLoginKBLayout", kblayoutChanged);
    FrameDataBind::Instance()->refreshData("UserLoginKBLayout");
}

/**
 * @brief ?????????????????????????????????????????????????????????
 *
 * @param type
 */
void UserLoginWidget::updateWidgetShowType(const int type)
{
    int index = 3;

    /**
     * @brief ????????????
     * ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
     */
    /* ?????? */
    if (type & AuthTypeFace) {
        initFaceAuth(index++);
    } else if (m_faceAuth != nullptr) {
        m_faceAuth->deleteLater();
        m_faceAuth = nullptr;
    }
    /* ?????? */
    if (type & AuthTypeFingerprint) {
        initFingerprintAuth(index++);
    } else if (m_fingerprintAuth != nullptr) {
        m_fingerprintAuth->deleteLater();
        m_fingerprintAuth = nullptr;
    }
    /* AD??? */
    if (type & AuthTypeActiveDirectory) {
        initActiveDirectoryAuth(index++);
    }
    /* Ukey */
    if (type & AuthTypeUkey) {
        initUkeyAuth(index++);
    } else if (m_ukeyAuth != nullptr) {
        m_ukeyAuth->deleteLater();
        m_ukeyAuth = nullptr;
    }
    /* ????????? */
    if (type & AuthTypeFingerVein) {
        initFingerVeinAuth(index++);
    }
    /* ?????? */
    if (type & AuthTypeIris) {
        initIrisAuth(index++);
    }
    /* PIN??? */
    if (type & AuthTypePIN) {
        initPINAuth(index++);
    } else if (m_PINAuth != nullptr) {
        m_PINAuth->deleteLater();
        m_PINAuth = nullptr;
    }
    /* ?????? */
    if (type & AuthTypePassword) {
        initPasswdAuth(index++);
    } else if (m_passwordAuth != nullptr) {
        m_passwordAuth->deleteLater();
        m_passwordAuth = nullptr;
    }
    /* ????????? */
    if (type & AuthTypeSingle) {
        initSingleAuth(index++);
    } else if (m_singleAuth != nullptr) {
        m_singleAuth->deleteLater();
        m_singleAuth = nullptr;
    }
    /* ?????? */
    if (type == AuthTypeNone) {
        if (m_model->currentUser()->isNoPasswordLogin()) {
            m_lockButton->setEnabled(true);
            m_accountEdit->hide();
            m_nameLabel->show();
        } else {
            m_accountEdit->clear();
            m_accountEdit->show();
            m_nameLabel->hide();
        }
    } else {
        const bool visible = m_model->isServerModel() && m_model->currentType() == SessionBaseModel::LightdmType;
        m_accountEdit->setVisible(visible);
        m_nameLabel->setVisible(!visible);
    }
    /* ?????????????????? */
    switch (m_model->currentUser()->expiredStatus()) {
    case User::ExpiredNormal:
        m_expiredStatusLabel->clear();
        m_expiredStatusLabel->hide();
        break;
    case User::ExpiredSoon:
        m_expiredStatusLabel->setText(tr("Your password will expire in %n days, please change it timely", "", m_model->currentUser()->expiredDayLeft()));
        m_expiredStatusLabel->show();
        break;
    case User::ExpiredAlready:
        m_expiredStatusLabel->setText(tr("Password expired, please change"));
        m_expiredStatusLabel->show();
        break;
    default:
        break;
    }

    updateGeometry();

    /**
     * @brief ????????????
     * ?????????: ?????????????????? > PIN???????????? > ??????????????? > ????????????
     * ?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
     */
    //?????????????????????????????????
    if (m_lockButton->isVisible() && m_lockButton->isEnabled()){
        setFocusProxy(m_lockButton);
    }
    if (m_passwordAuth != nullptr) {
        setFocusProxy(m_passwordAuth);
    }
    if (m_ukeyAuth != nullptr) {
        setFocusProxy(m_ukeyAuth);
    }
    if (m_PINAuth != nullptr) {
        setFocusProxy(m_PINAuth);
    }
    if (m_singleAuth != nullptr) {
        setFocusProxy(m_singleAuth);
    }
    if (m_accountEdit->isVisible()) {
        setFocusProxy(m_accountEdit);
    }

    m_widgetsList.clear();
    m_widgetsList << m_accountEdit
                  << m_faceAuth
                  << m_fingerprintAuth
                  << m_activeDirectoryAuth
                  << m_ukeyAuth
                  << m_fingerVeinAuth
                  << m_irisAuth
                  << m_PINAuth
                  << m_passwordAuth
                  << m_singleAuth
                  << m_lockButton;

    for (int i = 0; i < m_widgetsList.size(); i++) {
        if (m_widgetsList[i]) {
            for (int j = i + 1; j < m_widgetsList.size(); j++) {
                if (m_widgetsList[j]) {
                    setTabOrder(m_widgetsList[i]->focusProxy(), m_widgetsList[j]->focusProxy());
                    i = j - 1;
                    break;
                }
            }
        }
    }

    setFocus();
}

/**
 * @brief ????????????
 */
void UserLoginWidget::initSingleAuth(const int index)
{
    if (m_singleAuth != nullptr) {
        return;
    }
    m_singleAuth = new AuthSingle(this);
    m_singleAuth->setCapsStatus(m_capslockMonitor->isCapslockOn());
    m_userLoginLayout->insertWidget(index, m_singleAuth);

    connect(m_singleAuth, &AuthSingle::activeAuth, this, [this] {
        emit requestStartAuthentication(m_model->currentUser()->name(), AuthTypeSingle);
    });
    connect(m_singleAuth, &AuthSingle::requestAuthenticate, this, [this] {
        if (m_singleAuth->lineEditText().isEmpty()) {
            return;
        }
        emit sendTokenToAuth(m_model->currentUser()->name(), AuthTypeSingle, m_singleAuth->lineEditText());
    });
    connect(m_singleAuth, &AuthSingle::requestShowKeyboardList, this, &UserLoginWidget::showKeyboardList);
    connect(m_singleAuth, &AuthSingle::authFinished, this, [this](const bool status) {
        checkAuthResult(AuthTypeSingle, status);
    });

    connect(m_kbLayoutWidget, &KbLayoutWidget::setButtonClicked, m_singleAuth, &AuthSingle::setKeyboardButtonInfo);
    connect(m_capslockMonitor, &KeyboardMonitor::capslockStatusChanged, m_singleAuth, &AuthSingle::setCapsStatus);
    connect(m_lockButton, &QPushButton::clicked, m_singleAuth, &AuthSingle::requestAuthenticate);

    /* ??????????????????????????????????????????PIN??? */
    std::function<void(QVariant)> tokenChanged = std::bind(&UserLoginWidget::onOtherPageSingleChanged, this, std::placeholders::_1);
    m_registerFunctionIndexs["UserLoginToken"] = FrameDataBind::Instance()->registerFunction("UserLoginToken", tokenChanged);
    connect(m_singleAuth, &AuthSingle::lineEditTextChanged, this, [=](const QString &value) {
        FrameDataBind::Instance()->updateValue("UserLoginToken", value);
    });
    FrameDataBind::Instance()->refreshData("UserLoginToken");

    m_lockButton->setEnabled(true);
    m_singleAuth->setKeyboardButtonVisible(m_keyboardList.size() > 1 ? true : false);
    m_singleAuth->setKeyboardButtonInfo(m_keyboardInfo);
}

/**
 * @brief ????????????
 */
void UserLoginWidget::initPasswdAuth(const int index)
{
    if (m_passwordAuth != nullptr) {
        m_passwordAuth->setLineEditInfo(QString(""), AuthenticationModule::InputText);
        return;
    }
    m_passwordAuth = new AuthenticationModule(AuthTypePassword, this);
    m_passwordAuth->setCapsStatus(m_capslockMonitor->isCapslockOn());
    m_userLoginLayout->insertWidget(index, m_passwordAuth);

    connect(m_passwordAuth, &AuthenticationModule::activateAuthentication, this, [=] {
        emit requestStartAuthentication(m_model->currentUser()->name(), AuthTypePassword);
    });
    connect(m_passwordAuth, &AuthenticationModule::requestAuthenticate, this, [=] {
        if (m_passwordAuth->lineEditText().isEmpty()) {
            return;
        }
        m_passwordAuth->setAnimationState(true);
        m_passwordAuth->setEnabled(false);
        QString account = m_accountEdit->text().isEmpty() ? m_model->currentUser()->name() : m_accountEdit->text();
        emit sendTokenToAuth(account, AuthTypePassword, m_passwordAuth->lineEditText());
    });
    connect(m_passwordAuth, &AuthenticationModule::requestShowKeyboardList, this, &UserLoginWidget::showKeyboardList);
    connect(m_passwordAuth, &AuthenticationModule::authFinished, this, &UserLoginWidget::checkAuthResult);
    connect(m_lockButton, &QPushButton::clicked, m_passwordAuth, &AuthenticationModule::requestAuthenticate);
    connect(m_capslockMonitor, &KeyboardMonitor::capslockStatusChanged, m_passwordAuth, &AuthenticationModule::setCapsStatus);
    connect(m_kbLayoutWidget, &KbLayoutWidget::setButtonClicked, m_passwordAuth, &AuthenticationModule::setKeyboardButtonInfo);

    /* ????????????????????? */
    std::function<void(QVariant)> passwordChanged = std::bind(&UserLoginWidget::onOtherPagePasswordChanged, this, std::placeholders::_1);
    m_registerFunctionIndexs["UserLoginPassword"] = FrameDataBind::Instance()->registerFunction("UserLoginPassword", passwordChanged);
    connect(m_passwordAuth, &AuthenticationModule::lineEditTextChanged, this, [=] (const QString &value) {
        FrameDataBind::Instance()->updateValue("UserLoginPassword", value);
        if (value.length() > 0 || (m_ukeyAuth && (m_ukeyAuth->getAuthStatus() == StatusCodeSuccess)))
            m_lockButton->setEnabled(true);
        else if(m_ukeyAuth && m_ukeyAuth->lineEditText().isEmpty()) {
               m_lockButton->setEnabled(false);
        }
    });

    connect(m_passwordAuth, &AuthenticationModule::requestChangeFocus, this, &UserLoginWidget::updateNextFocusPosition);

    connect(m_passwordAuth, &AuthenticationModule::lineEditTextHasFocus, this, [=](bool focus) {
        if(m_passwordAuth != nullptr){
            if (!focus) {
                m_kbLayoutBorder->setVisible(false);
                m_passwordAuth->setLineEditBkColor(false);
            }
            if(m_passwordAuth)
                emit m_passwordAuth->lineEditTextChanged(m_passwordAuth->lineEditText());
        }
    });
    FrameDataBind::Instance()->refreshData("UserLoginPassword");
    m_passwordAuth->setKeyboardButtonVisible(m_keyboardList.size() > 1 ? true : false);
    m_passwordAuth->setKeyboardButtonInfo(m_keyboardInfo);
    m_passwordAuth->setLineEditInfo(QString(""), AuthenticationModule::InputText);
}

/**
 * @brief ????????????
 */
void UserLoginWidget::initFingerprintAuth(const int index)
{
    if (m_fingerprintAuth != nullptr) {
        return;
    }
    m_fingerprintAuth = new AuthenticationModule(AuthTypeFingerprint, this);
    //???????????????????????????????????????????????????????????????????????????????????????
    m_fingerprintAuth->setText(tr("Verify your fingerprint"));
    m_userLoginLayout->insertWidget(index, m_fingerprintAuth);

    connect(m_fingerprintAuth, &AuthenticationModule::activateAuthentication, this, [=] {
        emit requestStartAuthentication(m_model->currentUser()->name(), AuthTypeFingerprint);
    });
    connect(m_fingerprintAuth, &AuthenticationModule::authFinished, this, &UserLoginWidget::checkAuthResult);
}

/**
 * @brief ????????????
 */
void UserLoginWidget::initFaceAuth(const int index)
{
    if (m_faceAuth != nullptr) {
        return;
    }
    m_faceAuth = new AuthenticationModule(AuthTypeFace, this);
    m_faceAuth->setText("Face ID");
    m_userLoginLayout->insertWidget(index, m_faceAuth);

    connect(m_faceAuth, &AuthenticationModule::activateAuthentication, this, [=] {
        emit requestStartAuthentication(m_model->currentUser()->name(), AuthTypeFace);
    });
    connect(m_faceAuth, &AuthenticationModule::authFinished, this, &UserLoginWidget::checkAuthResult);
}

/**
 * @brief AD?????????
 */
void UserLoginWidget::initActiveDirectoryAuth(const int index)
{
    Q_UNUSED(index)
    // TODO
}

/**
 * @brief Ukey??????
 */
void UserLoginWidget::initUkeyAuth(const int index)
{
    if (m_ukeyAuth != nullptr) {
        m_ukeyAuth->setLineEditInfo(QString(""), AuthenticationModule::InputText);
        return;
    }
    m_ukeyAuth = new AuthenticationModule(AuthTypeUkey, this);
    m_ukeyAuth->setCapsStatus(m_capslockMonitor->isCapslockOn());
    m_userLoginLayout->insertWidget(index, m_ukeyAuth);

    connect(m_ukeyAuth, &AuthenticationModule::activateAuthentication, this, [=] {
        emit requestStartAuthentication(m_model->currentUser()->name(), AuthTypeUkey);
    });
    connect(m_ukeyAuth, &AuthenticationModule::requestAuthenticate, this, [=] {
        if (m_ukeyAuth->lineEditText().isEmpty()) {
            return;
        }
        m_ukeyAuth->setAnimationState(true);
        m_ukeyAuth->setEnabled(false);
        emit sendTokenToAuth(m_model->currentUser()->name(), AuthTypeUkey, m_ukeyAuth->lineEditText());
    });
    connect(m_lockButton, &QPushButton::clicked, m_ukeyAuth, &AuthenticationModule::requestAuthenticate);
    connect(m_ukeyAuth, &AuthenticationModule::authFinished, this, &UserLoginWidget::checkAuthResult);
    connect(m_capslockMonitor, &KeyboardMonitor::capslockStatusChanged, m_ukeyAuth, &AuthenticationModule::setCapsStatus);

    /* ????????????????????? */
    std::function<void(QVariant)> PINChanged = std::bind(&UserLoginWidget::onOtherPageUKeyChanged, this, std::placeholders::_1);
    m_registerFunctionIndexs["UserLoginUKey"] = FrameDataBind::Instance()->registerFunction("UserLoginUKey", PINChanged);
    connect(m_ukeyAuth, &AuthenticationModule::lineEditTextChanged, this, [=] (const QString &value) {
        if (m_model->getAuthProperty().PINLen > 0 && value.size() >= m_model->getAuthProperty().PINLen) {
            emit m_ukeyAuth->requestAuthenticate();
        }
        FrameDataBind::Instance()->updateValue("UserLoginUKey", value);

        if (value.length() > 0 || (m_passwordAuth && (m_passwordAuth->getAuthStatus() == StatusCodeSuccess))) {
            m_lockButton->setEnabled(true);
        } else if (m_passwordAuth && m_passwordAuth->lineEditText().isEmpty()) {
            m_lockButton->setEnabled(false);
        }
    });

    connect(m_ukeyAuth, &AuthenticationModule::requestChangeFocus, this, &UserLoginWidget::updateNextFocusPosition);

    connect(m_ukeyAuth, &AuthenticationModule::lineEditTextHasFocus, this, [ = ] (bool focus) {

        if(m_ukeyAuth != nullptr){
            if (!focus)
                m_ukeyAuth->setLineEditBkColor(false);
            if(m_ukeyAuth)
                emit m_ukeyAuth->lineEditTextChanged(m_ukeyAuth->lineEditText());
        }
    });
    FrameDataBind::Instance()->refreshData("UserLoginUKey");
    m_ukeyAuth->setLineEditInfo(QString(""), AuthenticationModule::InputText);
}

/**
 * @brief ???????????????
 */
void UserLoginWidget::initFingerVeinAuth(const int index)
{
    Q_UNUSED(index)
    // TODO
}

/**
 * @brief ????????????
 */
void UserLoginWidget::initIrisAuth(const int index)
{
    Q_UNUSED(index)
    // TODO
}

/**
 * @brief PIN?????????
 */
void UserLoginWidget::initPINAuth(const int index)
{
    if (m_PINAuth != nullptr) {
        return;
    }
    m_PINAuth = new AuthenticationModule(AuthTypePIN, this);
    m_userLoginLayout->insertWidget(index, m_PINAuth);

    std::function<void(QVariant)> PINChanged = std::bind(&UserLoginWidget::onOtherPagePINChanged, this, std::placeholders::_1);
    m_registerFunctionIndexs["UserLoginPIN"] = FrameDataBind::Instance()->registerFunction("UserLoginPIN", PINChanged);
    connect(m_PINAuth, &AuthenticationModule::lineEditTextChanged, this, [=] (const QString &value) {
        FrameDataBind::Instance()->updateValue("UserLoginPIN", value);
    });
    connect(m_PINAuth, &AuthenticationModule::activateAuthentication, this, [=] {
        emit requestStartAuthentication(m_model->currentUser()->name(), AuthTypePIN);
    });
    connect(m_PINAuth, &AuthenticationModule::requestAuthenticate, this, [=] {
        qDebug() << "PIN:" << m_PINAuth->lineEditText();
        if (m_PINAuth->lineEditText().isEmpty()) {
            return;
        }
        m_PINAuth->setAnimationState(true);
        m_PINAuth->setEnabled(false);
        QString account = m_accountEdit->text().isEmpty() ? m_model->currentUser()->name() : m_accountEdit->text();
        emit sendTokenToAuth(account, AuthTypePIN, m_PINAuth->lineEditText());
    });
    connect(m_lockButton, &QPushButton::clicked, m_PINAuth, &AuthenticationModule::requestAuthenticate);
    connect(m_PINAuth, &AuthenticationModule::authFinished, this, &UserLoginWidget::checkAuthResult);
    connect(m_capslockMonitor, &KeyboardMonitor::capslockStatusChanged, m_PINAuth, &AuthenticationModule::setCapsStatus);
    FrameDataBind::Instance()->refreshData("UserLoginPIN");
}

/**
 * @brief ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * @param authType   ????????????
 * @param status     ????????????
 * @param message    ????????????????????????????????????????????????
 */
void UserLoginWidget::updateAuthResult(const int type, const int status, const QString &message)
{
    qDebug() << "UserLoginWidget::updateAuthResult:" << type << status << message;
    switch (type) {
    case AuthTypePassword:
        if (m_passwordAuth != nullptr) {
            m_passwordAuth->setAuthResult(status, message);
            FrameDataBind::Instance()->updateValue("PasswordAuthStatus", status);
            FrameDataBind::Instance()->updateValue("PasswordAuthMsg", message);
        }
        break;
    case AuthTypeFingerprint:
        if (m_fingerprintAuth != nullptr) {
            m_fingerprintAuth->setAuthResult(status, message);
            FrameDataBind::Instance()->updateValue("FingerprintAuthStatus", status);
            FrameDataBind::Instance()->updateValue("FingerprintAuthMsg", message);
        }
        break;
    case AuthTypeFace:
        if (m_faceAuth != nullptr) {
            m_faceAuth->setAuthResult(status, message);
            FrameDataBind::Instance()->updateValue("FaceAuthStatus", status);
            FrameDataBind::Instance()->updateValue("FaceAuthMsg", message);
        }
        break;
    case AuthTypeActiveDirectory:
        if (m_activeDirectoryAuth != nullptr) {
            m_activeDirectoryAuth->setAuthResult(status, message);
            FrameDataBind::Instance()->updateValue("ActiveDirectoryAuthStatus", status);
            FrameDataBind::Instance()->updateValue("ActiveDirectoryAuthMsg", message);
        }
        break;
    case AuthTypeUkey:
        if (m_ukeyAuth != nullptr) {
            m_ukeyAuth->setAuthResult(status, message);
            FrameDataBind::Instance()->updateValue("UKeyAuthStatus", status);
            FrameDataBind::Instance()->updateValue("UKeyAuthMsg", message);
        }
        break;
    case AuthTypeFingerVein:
        if (m_fingerVeinAuth != nullptr) {
            m_fingerVeinAuth->setAuthResult(status, message);
            FrameDataBind::Instance()->updateValue("FingerVeinAuthStatus", status);
            FrameDataBind::Instance()->updateValue("FingerVeinAuthMsg", message);
        }
        break;
    case AuthTypeIris:
        if (m_irisAuth != nullptr) {
            m_irisAuth->setAuthResult(status, message);
            FrameDataBind::Instance()->updateValue("IrisAuthStatus", status);
            FrameDataBind::Instance()->updateValue("IrisAuthMsg", message);
        }
        break;
    case AuthTypeSingle:
        if (m_singleAuth != nullptr) {
            m_singleAuth->setAuthResult(status, message);
            FrameDataBind::Instance()->updateValue("SingleAuthStatus", status);
            FrameDataBind::Instance()->updateValue("SingleAuthMsg", message);
        }
        break;
    case AuthTypeAll:
        checkAuthResult(type, status);
        break;
    default:
        break;
    }
}

/**
 * @brief ?????????????????????
 *
 * @param avatar ??????????????????
 */
void UserLoginWidget::updateAvatar(const QString &path)
{
    if (m_userAvatar == nullptr) {
        return;
    }

    m_userAvatar->setIcon(path);
}

/**
 * @brief ???????????????????????????
 *
 * @param message
 */
void UserLoginWidget::setFaildTipMessage(const QString &message)
{
    if (m_accountEdit->isVisible()) {
        m_accountEdit->showAlertMessage(message);
    }
}

/**
 * @brief ????????????
 *
 * @param action
 */
void UserLoginWidget::ShutdownPrompt(SessionBaseModel::PowerAction action)
{
    m_action = action;

    QPalette lockPalatte;
    switch (m_action) {
    case SessionBaseModel::PowerAction::RequireRestart:
        m_lockButton->setIcon(QIcon(":/img/bottom_actions/reboot.svg"));
        lockPalatte.setColor(QPalette::Highlight, shutdownColor);
        break;
    case SessionBaseModel::PowerAction::RequireShutdown:
        m_lockButton->setIcon(QIcon(":/img/bottom_actions/shutdown.svg"));
        lockPalatte.setColor(QPalette::Highlight, shutdownColor);
        break;
    default:
        if (m_authType == SessionBaseModel::LightdmType) {
            m_lockButton->setIcon(DStyle::SP_ArrowNext);
            return;
        } else {
            m_lockButton->setIcon(DStyle::SP_LockElement);
        }
    }
    m_lockButton->setPalette(lockPalatte);
}

/**
 * @brief ????????????
 *
 * @param value
 */
void UserLoginWidget::onOtherPageFocusChanged(const QVariant &value)
{
    Q_UNUSED(value)
}

/**
 * @brief ???????????????????????????????????????
 *
 * @param value
 */
void UserLoginWidget::onOtherPageSingleChanged(const QVariant &value)
{
    m_singleAuth->setLineEditInfo(value.toString(), AuthSingle::InputText);
}

/**
 * @brief ??????????????????????????????
 *
 * @param value
 */
void UserLoginWidget::onOtherPageAccountChanged(const QVariant &value)
{
    int cursorIndex = m_accountEdit->lineEdit()->cursorPosition();
    m_accountEdit->setText(value.toString());
    m_accountEdit->lineEdit()->setCursorPosition(cursorIndex);
}

/**
 * @brief PIN????????????????????????
 *
 * @param value
 */
void UserLoginWidget::onOtherPagePINChanged(const QVariant &value)
{
    m_PINAuth->setLineEditInfo(value.toString(), AuthenticationModule::InputText);
}

/**
 * @brief ukey ?????????????????????
 * @param value
 */
void UserLoginWidget::onOtherPageUKeyChanged(const QVariant &value)
{
    if(m_ukeyAuth != nullptr)
        m_ukeyAuth->setLineEditInfo(value.toString(), AuthenticationModule::InputText);
}

/**
 * @brief ???????????????????????????
 *
 * @param value
 */
void UserLoginWidget::onOtherPagePasswordChanged(const QVariant &value)
{
    if(m_passwordAuth != nullptr)
        m_passwordAuth->setLineEditInfo(value.toString(), AuthenticationModule::InputText);
}

/**
 * @brief ??????????????????
 *
 * @param value
 */
void UserLoginWidget::onOtherPageKBLayoutChanged(const QVariant &value)
{
    if (value.toBool()) {
        m_kbLayoutBorder->setParent(window());
    }

    m_kbLayoutBorder->setVisible(value.toBool());

    if (m_kbLayoutBorder->isVisible()) {
        m_kbLayoutBorder->raise();
    }

    updateKeyboardListPosition();
}

/**
 * @brief ??????/????????????????????????
 */
void UserLoginWidget::showKeyboardList()
{
    if (m_kbLayoutBorder->isVisible()) {
        m_kbLayoutBorder->hide();
    } else {
        // ???????????????????????????????????????????????????
        // ??????????????????????????????????????????
        m_kbLayoutBorder->setParent(parentWidget());
        m_kbLayoutBorder->setVisible(true);
        m_kbLayoutBorder->raise();
        updateKeyboardListPosition();
    }
    updateClipPath();
}

/**
 * @brief ??????????????????????????????
 */
void UserLoginWidget::updateKeyboardListPosition()
{
    const QPoint &point = mapTo(m_kbLayoutBorder->parentWidget(), QPoint(m_blurEffectWidget->geometry().left() + m_blurEffectWidget->width() / 2,
                                                                         m_blurEffectWidget->geometry().bottom() - 10));
    m_kbLayoutBorder->move(point.x(), point.y());
    m_kbLayoutBorder->setArrowX(15);
    updateClipPath();
}

/**
 * @brief ????????????????????????  TODO
 *
 * @param type
 */
void UserLoginWidget::updateAuthType(SessionBaseModel::AuthType type)
{
    m_authType = type;

    if (m_authType == SessionBaseModel::LightdmType) {
        m_lockButton->setIcon(DStyle::SP_ArrowNext);
    } else {
        m_lockButton->setIcon(DStyle::SP_LockElement);
    }
}

/**
 * @brief ????????????????????????
 */
void UserLoginWidget::updateBlurEffectGeometry()
{
    QRect rect = layout()->geometry();
    rect.setTop(rect.top() + m_userAvatar->height() / 2);
    if (m_widgetType == LoginType) {
        if (m_model->currentUser()->expiredStatus() && !m_expiredStatusLabel->text().isEmpty()) {
            rect.setBottom(rect.bottom() - m_lockButton->height() - m_expiredStatusLabel->height() - layout()->spacing() * 2);
        } else {
            rect.setBottom(rect.bottom() - m_lockButton->height() - layout()->spacing());
        }
    } else {
        rect.setBottom(rect.bottom() - 15);
    }
    m_blurEffectWidget->setGeometry(rect);
}

/**
 * @brief ??????????????????????????????????????????????????????????????????
 *
 * @param kbLayoutList
 */
void UserLoginWidget::updateKeyboardList(const QStringList &list)
{
    if (list == m_keyboardList) {
        return;
    }
    m_keyboardList = list;
    if (m_kbLayoutWidget != nullptr && m_kbLayoutBorder != nullptr) {
        m_kbLayoutWidget->updateButtonList(list);
        m_kbLayoutBorder->setContent(m_kbLayoutWidget);
        updateClipPath();
    }
    if (m_passwordAuth != nullptr) {
        m_passwordAuth->setKeyboardButtonVisible(list.size() > 1 ? true : false);
    }
    if (m_singleAuth != nullptr) {
        m_singleAuth->setKeyboardButtonVisible(list.size() > 1 ? true : false);
    }
}

/**
 * @brief ?????????????????????????????????
 */
void UserLoginWidget::updateNextFocusPosition()
{
    AuthenticationModule *module = static_cast<AuthenticationModule *>(sender());
    if (module == m_passwordAuth) {
        if (m_ukeyAuth != nullptr && m_ukeyAuth->isEnabled()) {
            setFocusProxy(m_ukeyAuth);
        } else {
            setFocusProxy(m_lockButton);
        }
    } else {
        if (m_passwordAuth != nullptr && m_passwordAuth->isEnabled()) {
            setFocusProxy(m_passwordAuth);
        } else {
            setFocusProxy(m_lockButton);
        }
    }
    setFocus();
}

/**
 * @brief ????????????????????????
 *
 * @param limitsInfo
 */
void UserLoginWidget::updateLimitsInfo(const QMap<int, User::LimitsInfo> *limitsInfo)
{
    AuthenticationModule::LimitsInfo limitsInfoTmpA;
    User::LimitsInfo limitsInfoTmpU;
    LimitsInfo limitsInfoTmpC;

    QMap<int, User::LimitsInfo>::const_iterator i = limitsInfo->constBegin();
    while (i != limitsInfo->end()) {
        limitsInfoTmpU = i.value();
        switch (i.key()) {
        case AuthTypePassword:
            if (m_passwordAuth != nullptr) {
                limitsInfoTmpA.locked = limitsInfoTmpU.locked;
                limitsInfoTmpA.maxTries = limitsInfoTmpU.maxTries;
                limitsInfoTmpA.numFailures = limitsInfoTmpU.numFailures;
                limitsInfoTmpA.unlockSecs = limitsInfoTmpU.unlockSecs;
                limitsInfoTmpA.unlockTime = limitsInfoTmpU.unlockTime;
                m_passwordAuth->setLimitsInfo(limitsInfoTmpA);
            }
            if (m_singleAuth != nullptr) {
                limitsInfoTmpC.locked = limitsInfoTmpU.locked;
                limitsInfoTmpC.maxTries = limitsInfoTmpU.maxTries;
                limitsInfoTmpC.numFailures = limitsInfoTmpU.numFailures;
                limitsInfoTmpC.unlockSecs = limitsInfoTmpU.unlockSecs;
                limitsInfoTmpC.unlockTime = limitsInfoTmpU.unlockTime;
                m_singleAuth->setLimitsInfo(limitsInfoTmpC);
            }
            break;
        case AuthTypeFingerprint:
            if (m_fingerprintAuth != nullptr) {
                limitsInfoTmpA.locked = limitsInfoTmpU.locked;
                limitsInfoTmpA.maxTries = limitsInfoTmpU.maxTries;
                limitsInfoTmpA.numFailures = limitsInfoTmpU.numFailures;
                limitsInfoTmpA.unlockSecs = limitsInfoTmpU.unlockSecs;
                limitsInfoTmpA.unlockTime = limitsInfoTmpU.unlockTime;
                m_fingerprintAuth->setLimitsInfo(limitsInfoTmpA);
            }
            break;
        case AuthTypeFace:
            if (m_faceAuth != nullptr) {
                limitsInfoTmpA.locked = limitsInfoTmpU.locked;
                limitsInfoTmpA.maxTries = limitsInfoTmpU.maxTries;
                limitsInfoTmpA.numFailures = limitsInfoTmpU.numFailures;
                limitsInfoTmpA.unlockSecs = limitsInfoTmpU.unlockSecs;
                limitsInfoTmpA.unlockTime = limitsInfoTmpU.unlockTime;
                m_faceAuth->setLimitsInfo(limitsInfoTmpA);
            }
            break;
        case AuthTypeActiveDirectory:
            if (m_activeDirectoryAuth != nullptr) {
                limitsInfoTmpA.locked = limitsInfoTmpU.locked;
                limitsInfoTmpA.maxTries = limitsInfoTmpU.maxTries;
                limitsInfoTmpA.numFailures = limitsInfoTmpU.numFailures;
                limitsInfoTmpA.unlockSecs = limitsInfoTmpU.unlockSecs;
                limitsInfoTmpA.unlockTime = limitsInfoTmpU.unlockTime;
                m_activeDirectoryAuth->setLimitsInfo(limitsInfoTmpA);
            }
            break;
        case AuthTypeUkey:
            if (m_ukeyAuth != nullptr) {
                limitsInfoTmpA.locked = limitsInfoTmpU.locked;
                limitsInfoTmpA.maxTries = limitsInfoTmpU.maxTries;
                limitsInfoTmpA.numFailures = limitsInfoTmpU.numFailures;
                limitsInfoTmpA.unlockSecs = limitsInfoTmpU.unlockSecs;
                limitsInfoTmpA.unlockTime = limitsInfoTmpU.unlockTime;
                m_ukeyAuth->setLimitsInfo(limitsInfoTmpA);
            }
            break;
        case AuthTypeFingerVein:
            if (m_fingerVeinAuth != nullptr) {
                limitsInfoTmpA.locked = limitsInfoTmpU.locked;
                limitsInfoTmpA.maxTries = limitsInfoTmpU.maxTries;
                limitsInfoTmpA.numFailures = limitsInfoTmpU.numFailures;
                limitsInfoTmpA.unlockSecs = limitsInfoTmpU.unlockSecs;
                limitsInfoTmpA.unlockTime = limitsInfoTmpU.unlockTime;
                m_fingerVeinAuth->setLimitsInfo(limitsInfoTmpA);
            }
            break;
        case AuthTypeIris:
            if (m_irisAuth != nullptr) {
                limitsInfoTmpA.locked = limitsInfoTmpU.locked;
                limitsInfoTmpA.maxTries = limitsInfoTmpU.maxTries;
                limitsInfoTmpA.numFailures = limitsInfoTmpU.numFailures;
                limitsInfoTmpA.unlockSecs = limitsInfoTmpU.unlockSecs;
                limitsInfoTmpA.unlockTime = limitsInfoTmpU.unlockTime;
                m_irisAuth->setLimitsInfo(limitsInfoTmpA);
            }
            break;
        case AuthTypePIN:
            if (m_PINAuth != nullptr) {
                limitsInfoTmpA.locked = limitsInfoTmpU.locked;
                limitsInfoTmpA.maxTries = limitsInfoTmpU.maxTries;
                limitsInfoTmpA.numFailures = limitsInfoTmpU.numFailures;
                limitsInfoTmpA.unlockSecs = limitsInfoTmpU.unlockSecs;
                limitsInfoTmpA.unlockTime = limitsInfoTmpU.unlockTime;
                m_PINAuth->setLimitsInfo(limitsInfoTmpA);
            }
            break;
        default:
            qWarning() << "Error! Authentication type is wrong." << i.key();
            break;
        }
        ++i;
    }
}

void UserLoginWidget::updateAuthStatus()
{
    if (m_singleAuth != nullptr) {
        QVariant authStatus = FrameDataBind::Instance()->getValue("SingleAuthStatus");
        QVariant authMsg = FrameDataBind::Instance()->getValue("SingleAuthMsg");
        if (!authStatus.isNull() && !authMsg.isNull()) {
            m_singleAuth->setAuthResult(authStatus.toInt(), authMsg.toString());
        }
    }

    if (m_passwordAuth != nullptr) {
        QVariant authStatus = FrameDataBind::Instance()->getValue("PasswordAuthStatus");
        QVariant authMsg = FrameDataBind::Instance()->getValue("PasswordAuthMsg");
        if (!authStatus.isNull() && !authMsg.isNull()) {
            m_passwordAuth->setAuthResult(authStatus.toInt(), authMsg.toString());
        }
    }

    if (m_fingerprintAuth != nullptr) {
        QVariant authStatus = FrameDataBind::Instance()->getValue("FingerprintAuthStatus");
        QVariant authMsg = FrameDataBind::Instance()->getValue("FingerprintAuthMsg");
        if (authStatus.isDetached() && !authMsg.isNull()) {
            m_fingerprintAuth->setAuthResult(authStatus.toInt(), authMsg.toString());
        }
    }

    if (m_faceAuth != nullptr) {
        QVariant authStatus = FrameDataBind::Instance()->getValue("FaceAuthStatus");
        QVariant authMsg = FrameDataBind::Instance()->getValue("FaceAuthMsg");
        if (!authStatus.isNull() && !authMsg.isNull()) {
            m_faceAuth->setAuthResult(authStatus.toInt(), authMsg.toString());
        }
    }

    if (m_ukeyAuth != nullptr) {
        QVariant authStatus = FrameDataBind::Instance()->getValue("UKeyAuthStatus");
        QVariant authMsg = FrameDataBind::Instance()->getValue("UKeyAuthMsg");
        if (!authStatus.isNull() && !authMsg.isNull()) {
            m_ukeyAuth->setAuthResult(authStatus.toInt(), authMsg.toString());
        }
    }

    if (m_fingerVeinAuth != nullptr) {
        QVariant authStatus = FrameDataBind::Instance()->getValue("FingerVeinAuthStatus");
        QVariant authMsg = FrameDataBind::Instance()->getValue("FingerVeinAuthMsg");
        if (!authStatus.isNull() && !authMsg.isNull()) {
            m_fingerVeinAuth->setAuthResult(authStatus.toInt(), authMsg.toString());
        }
    }

    if (m_irisAuth != nullptr) {
        QVariant authStatus = FrameDataBind::Instance()->getValue("FrisAuthStatus");
        QVariant authMsg = FrameDataBind::Instance()->getValue("IrisVeinAuthMsg");
        if (!authStatus.isNull() && !authMsg.isNull()) {
            m_irisAuth->setAuthResult(authStatus.toInt(), authMsg.toString());
        }
    }

    if (m_PINAuth != nullptr) {
        QVariant authStatus = FrameDataBind::Instance()->getValue("PINAuthStatus");
        QVariant authMsg = FrameDataBind::Instance()->getValue("PINAuthMsg");
        if (!authStatus.isNull() && !authMsg.isNull()) {
            m_PINAuth->setAuthResult(authStatus.toInt(), authMsg.toString());
        }
    }
}

/**
 * @brief ????????????????????????
 *
 * @param layout
 */
void UserLoginWidget::updateKeyboardInfo(const QString &text)
{
    m_kbLayoutBorder->hide();
    if (text == m_keyboardInfo) {
        return;
    }
    m_keyboardInfo = text;
    m_kbLayoutWidget->setDefault(text);
    if (m_passwordAuth != nullptr) {
        m_passwordAuth->setKeyboardButtonInfo(text);
    }
    if (m_singleAuth != nullptr) {
        m_singleAuth->setKeyboardButtonInfo(text);
    }
}

/**
 * @brief ??????????????? uid ????????????????????????
 *
 * @param uid
 */
void UserLoginWidget::setUid(const uint uid)
{
    if (uid == m_uid) {
        return;
    }
    m_uid = uid;
}

/**
 * @brief ????????????????????????????????????
 *
 * @param isSelected
 */
void UserLoginWidget::setSelected(bool isSelected)
{
    m_isSelected = isSelected;
    update();
}

/**
 * @brief ????????????????????????????????????
 *
 * @param isSelected
 */
void UserLoginWidget::setFastSelected(bool isSelected)
{
    m_isSelected = isSelected;
    repaint();
}

/**
 * @brief ????????????????????????????????????
 */
void UserLoginWidget::updateClipPath()
{
    if (!m_kbLayoutClip)
        return;
    QRectF rc(0, 0, DDESESSIONCC::PASSWDLINEEIDT_WIDTH - 15, m_kbLayoutBorder->height());
    int iRadius = 20;
    QPainterPath path;
    path.lineTo(0, 0);
    path.lineTo(rc.width(), 0);
    path.lineTo(rc.width(), rc.height() - iRadius);
    path.arcTo(rc.width() - iRadius, rc.height() - iRadius, iRadius, iRadius, 0, -90);
    path.lineTo(rc.width() - iRadius, rc.height());
    path.lineTo(iRadius, rc.height());
    path.arcTo(0, rc.height() - iRadius, iRadius, iRadius, -90, -90);
    path.lineTo(0, rc.height() - iRadius);
    path.lineTo(0, 0);
    m_kbLayoutClip->setClipPath(path);
}

/**
 * @brief ???????????????????????????
 *
 * @param type
 * @param succeed
 */
void UserLoginWidget::checkAuthResult(const int type, const int status)
{
    if (type == AuthTypePassword && status == StatusCodeSuccess) {
        if (m_fingerprintAuth != nullptr && m_fingerprintAuth->getAuthStatus() == StatusCodeFailure) {
            m_fingerprintAuth->setText(tr("Verify your fingerprint"));
        }
    }
    if (status == StatusCodeCancel) {
        if (m_ukeyAuth != nullptr) {
            m_ukeyAuth->setAuthResult(StatusCodeCancel, "Cancel");
        }
        if (m_passwordAuth != nullptr) {
            m_passwordAuth->setAuthResult(StatusCodeCancel, "Cancel");
        }
        if (m_fingerprintAuth != nullptr) {
            m_fingerprintAuth->setAuthResult(StatusCodeCancel, "Cancel");
        }
        if (m_singleAuth != nullptr) {
            m_singleAuth->setAuthResult(StatusCodeCancel, "Cancel");
        }
    }
}

/**
 * @brief ???????????????
 *
 * @param name
 */
void UserLoginWidget::updateName(const QString &name)
{
    if (name == m_name || m_nameLabel == nullptr) {
        return;
    }
    m_name = name;
    updateNameLabel(m_nameLabel->font());
}

/**
 * @brief ????????????????????????
 *
 * @param loginState
 */
void UserLoginWidget::updateLoginState(const bool loginState)
{
    if (loginState == m_loginState) {
        return;
    }
    m_loginState = loginState;
    m_loginStateLabel->setVisible(loginState);
    updateNameLabel(m_nameLabel->font());
}

/**
 * @brief ????????????????????????
 *
 * @param font
 */
void UserLoginWidget::updateNameLabel(const QFont &font)
{
    if (font != m_nameLabel->font()) {
        m_nameLabel->setFont(font);
        m_nameLabel->setTextFormat(Qt::TextFormat::PlainText);
        DFontSizeManager::instance()->bind(m_nameLabel, DFontSizeManager::T2);
        QPalette palette = m_nameLabel->palette();
        palette.setColor(QPalette::WindowText, Qt::white);
        m_nameLabel->setPalette(palette);
    }
    int nameWidth = m_nameLabel->fontMetrics().width(m_name);
    int labelMaxWidth = width() - 10 * 2;
    if (m_loginStateLabel->isVisible()) {
        labelMaxWidth -= m_loginStateLabel->width() - m_nameWidget->layout()->spacing();
    }
    if (nameWidth > labelMaxWidth) {
        QString str = m_nameLabel->fontMetrics().elidedText(m_name, Qt::ElideRight, labelMaxWidth);
        m_nameLabel->setText(str);
    } else {
        m_nameLabel->setText(m_name);
    }

    //LoginType???????????????????????????????????????UserListType?????????????????????????????????
    if (m_widgetType == LoginType) {
        m_nameLabel->adjustSize();
    } else {
        int margin = m_nameLabel->margin();
        QFont tmpFont(m_nameLabel->font());
        int fontHeightTmp = m_nameLabel->rect().height() - margin * 2;
        while (QFontMetrics(tmpFont).boundingRect(m_nameLabel->text()).height() > fontHeightTmp && tmpFont.pixelSize() > 6) {
            tmpFont.setPixelSize(tmpFont.pixelSize() - 1);
        }
        m_nameLabel->setFont(tmpFont);
        m_nameLabel->update();
    }
}

/**
 * @brief obsolete
 */
void UserLoginWidget::unlockSuccessAni()
{
    m_timerIndex = 0;
    m_lockButton->setIcon(DStyle::SP_LockElement);

    disconnect(m_connection);
    m_connection = connect(m_aniTimer, &QTimer::timeout, [&]() {
        if (m_timerIndex <= 11) {
            m_lockButton->setIcon(QIcon(QString(":/img/unlockTrue/unlock_%1.svg").arg(m_timerIndex)));
        } else {
            m_aniTimer->stop();
            emit unlockActionFinish();
            m_lockButton->setIcon(DStyle::SP_LockElement);
        }

        m_timerIndex++;
    });

    m_aniTimer->start(15);
}

/**
 * @brief obsolete
 */
void UserLoginWidget::unlockFailedAni()
{
    //    m_passwordEdit->lineEdit()->clear();
    //    m_passwordEdit->hideLoadSlider();

    m_timerIndex = 0;
    m_lockButton->setIcon(DStyle::SP_LockElement);

    disconnect(m_connection);
    m_connection = connect(m_aniTimer, &QTimer::timeout, [&]() {
        if (m_timerIndex <= 15) {
            m_lockButton->setIcon(QIcon(QString(":/img/unlockFalse/unlock_error_%1.svg").arg(m_timerIndex)));
        } else {
            m_aniTimer->stop();
        }

        m_timerIndex++;
    });

    m_aniTimer->start(15);
}

/**
 * @brief ????????????????????????
 */
void UserLoginWidget::updateAccoutLocale()
{
    m_accountEdit->setPlaceholderText(tr("Account"));
}

/**
 * @brief ???????????????????????????????????????
 *
 * @param watched   ???????????????
 * @param event     ????????????
 * @return true     ??????
 * @return false    ??????
 */
bool UserLoginWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *key_event = static_cast<QKeyEvent *>(event);
        if (key_event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
            if ((key_event->modifiers() & Qt::ControlModifier) && key_event->key() == Qt::Key_A)
                return false;
            return true;
        }
    }

    return QObject::eventFilter(watched, event);
}

void UserLoginWidget::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
}

//??????resize??????,???????????????????????????
void UserLoginWidget::resizeEvent(QResizeEvent *event)
{
    updateBlurEffectGeometry();
    //    refreshKBLayoutWidgetPosition();
    QWidget::resizeEvent(event);
}

void UserLoginWidget::mousePressEvent(QMouseEvent *event)
{
    emit clicked();

    QWidget::mousePressEvent(event);
}

/**
 * @brief ??????????????????????????????????????????92*4??????????????????????????????????????????????????????
 *
 * @param event
 */
void UserLoginWidget::paintEvent(QPaintEvent *event)
{
    if (!m_isSelected) {
        return;
    }
    QPainter painter(this);
    painter.setPen(QColor(255, 255, 255, 76));
    painter.setBrush(QColor(255, 255, 255, 76));
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.drawRoundedRect(QRect(width() / 2 - 46, rect().bottom() - 4, 92, 4), 2, 2);

    QWidget::paintEvent(event);
}

void UserLoginWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    /**
     * @brief ????????????
     * ?????????: ?????????????????? > PIN???????????? > ??????????????? > ????????????
     * ?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
     */
    //?????????????????????????????????
    if (m_lockButton->isVisible() && m_lockButton->isEnabled()){
        setFocusProxy(m_lockButton);
    }
    if (m_passwordAuth != nullptr) {
        setFocusProxy(m_passwordAuth);
    }
    if (m_ukeyAuth != nullptr) {
        setFocusProxy(m_ukeyAuth);
    }
    if (m_PINAuth != nullptr) {
        setFocusProxy(m_PINAuth);
    }
    if (m_singleAuth != nullptr) {
        setFocusProxy(m_singleAuth);
    }
    if (m_accountEdit->isVisible()) {
        setFocusProxy(m_accountEdit);
    }
    setFocus();
}
