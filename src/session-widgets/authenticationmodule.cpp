/*
 * Copyright (C) 2021 ~ 2031 Deepin Technology Co., Ltd.
 *
 * Author:     Zhang Qipeng <zhangqipeng@uniontech.com>
 *
 * Maintainer: Zhang Qipeng <zhangqipeng@uniontech.com>
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

#include "authenticationmodule.h"

#include "authcommon.h"
#include "dlineeditex.h"

#include <DFontSizeManager>
#include <DHiDPIHelper>
#include <DPalette>

#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QTimer>

using namespace AuthCommon;

AuthenticationModule::AuthenticationModule(const int type, QWidget *parent)
    : QWidget(parent)
    , m_authType(type)
    , m_authStatus(nullptr)
    , m_capsStatus(nullptr)
    , m_numLockStatus(nullptr)
    , m_textLabel(nullptr)
    , m_lineEdit(nullptr)
    , m_keyboardButton(nullptr)
    , m_limitsInfo(new LimitsInfo())
    , m_status(StatusCodeStarted)
    , m_unlockTimer(new QTimer(this))
    , m_unlockTimerTmp(new QTimer(this))
    , m_showPrompt(true)
{
    init();

    m_unlockTimer->setSingleShot(false);
    m_unlockTimerTmp->setSingleShot(true);

    connect(m_unlockTimerTmp, &QTimer::timeout, this, [this] {
        m_integerMinutes--;
        if (m_integerMinutes <= 1) {
            m_integerMinutes = 0;
            m_unlockTimer->start(0);
        } else {
            emit unlockTimeChanged();
            m_unlockTimer->start(60 * 1000);
        }
    });
    connect(m_unlockTimer, &QTimer::timeout, this, [=] {
        if (m_integerMinutes > 0) {
            m_integerMinutes--;
        }
        if (m_integerMinutes == 0) {
            m_unlockTimer->stop();
        }
        emit unlockTimeChanged();
    });
}

AuthenticationModule::~AuthenticationModule()
{
    delete m_limitsInfo;
}

void AuthenticationModule::init()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(0);
    switch (m_authType) {
    case AuthTypePassword: {
        mainLayout->setContentsMargins(0, 0, 0, 0);
        m_lineEdit = new DLineEditEx(this);
        m_lineEdit->setClearButtonEnabled(false);
        m_lineEdit->setEchoMode(QLineEdit::Password);
        m_lineEdit->setContextMenuPolicy(Qt::NoContextMenu);
        m_lineEdit->lineEdit()->setAlignment(Qt::AlignCenter);
        QHBoxLayout *passwordLayout = new QHBoxLayout(m_lineEdit->lineEdit());
        passwordLayout->setContentsMargins(0, 0, 10, 0);
        passwordLayout->setSpacing(5);
        /* 键盘布局按钮 */
        m_keyboardButton = new QPushButton(m_lineEdit);
        m_keyboardButton->setContentsMargins(0, 0, 0, 0);
        m_keyboardButton->setFocusPolicy(Qt::NoFocus);
        m_keyboardButton->setCursor(Qt::ArrowCursor);
        m_keyboardButton->setFlat(true);
        passwordLayout->addWidget(m_keyboardButton, 0, Qt::AlignLeft | Qt::AlignVCenter);
        /* 大小写状态 */
        m_capsStatus = new DLabel(m_lineEdit);
        passwordLayout->addWidget(m_capsStatus, 1, Qt::AlignRight | Qt::AlignVCenter);
        /* 认证状态 */
        m_authStatus = new DLabel(m_lineEdit);
        passwordLayout->addWidget(m_authStatus, 0, Qt::AlignRight | Qt::AlignVCenter);
        mainLayout->addWidget(m_lineEdit);
        connect(m_keyboardButton, &QPushButton::clicked, this, &AuthenticationModule::requestShowKeyboardList);
        connect(m_lineEdit, &DLineEditEx::lineEditTextHasFocus, this, [this](const bool value) {
            if (value) {
                m_authStatus->hide();
            } else {
                m_authStatus->show();
            }
            emit lineEditTextHasFocus(value);
        });
        connect(m_lineEdit, &DLineEditEx::textChanged, this, &AuthenticationModule::lineEditTextChanged);
        connect(m_lineEdit, &DLineEditEx::returnPressed, this, &AuthenticationModule::requestAuthenticate);
        /* 解锁时间 */
        connect(this, &AuthenticationModule::unlockTimeChanged, this, [=] {
            if (m_integerMinutes == 1) {
                m_lineEdit->setPlaceholderText(tr("Please try again 1 minute later"));
            } else if (m_integerMinutes > 1) {
                m_lineEdit->setPlaceholderText(tr("Please try again %n minutes later", "", static_cast<int>(m_integerMinutes)));
            } else {
                QTimer::singleShot(1000, this, [=] {
                    emit activateAuthentication();
                });
                qInfo() << "Waiting authentication service...";
            }
        });
        m_lineEdit->installEventFilter(this);
        setFocusProxy(m_lineEdit);
    } break;
    case AuthTypePIN:
    case AuthTypeUkey: {
        mainLayout->setContentsMargins(0, 0, 0, 0);
        m_lineEdit = new DLineEditEx(this);
        m_lineEdit->setClearButtonEnabled(false);
        m_lineEdit->setEchoMode(QLineEdit::Password);
        m_lineEdit->setContextMenuPolicy(Qt::NoContextMenu);
        m_lineEdit->lineEdit()->setAlignment(Qt::AlignCenter);
        QHBoxLayout *PINLayout = new QHBoxLayout(m_lineEdit->lineEdit());
        PINLayout->setContentsMargins(0, 0, 10, 0);
        PINLayout->setSpacing(5);
        /* 大小写状态 */
        m_capsStatus = new DLabel(m_lineEdit);
        PINLayout->addWidget(m_capsStatus, 1, Qt::AlignRight | Qt::AlignVCenter);
        /* 认证状态 */
        m_authStatus = new DLabel(m_lineEdit);
        PINLayout->addWidget(m_authStatus, 0, Qt::AlignRight | Qt::AlignVCenter);
        mainLayout->addWidget(m_lineEdit);

        connect(m_lineEdit, &DLineEditEx::lineEditTextHasFocus, this, [this](const bool value) {
            if (value) {
                m_authStatus->hide();
            } else {
                m_authStatus->show();
            }
            emit lineEditTextHasFocus(value);
        });
        connect(m_lineEdit, &DLineEditEx::textChanged, this, &AuthenticationModule::lineEditTextChanged);
        connect(m_lineEdit, &DLineEditEx::returnPressed, this, &AuthenticationModule::requestAuthenticate);

        /* 解锁时间 */
        connect(this, &AuthenticationModule::unlockTimeChanged, this, [=] {
            if (m_integerMinutes == 1) {
                m_lineEdit->setPlaceholderText(tr("Please try again 1 minute later"));
            } else if (m_integerMinutes > 1) {
                m_lineEdit->setPlaceholderText(tr("Please try again %n minutes later", "", static_cast<int>(m_integerMinutes)));
            } else {
                QTimer::singleShot(500, this, [=] {
                    emit activateAuthentication();
                });
                qInfo() << "Waiting authentication service...";
            }
        });
        m_lineEdit->installEventFilter(this);
        setFocusProxy(m_lineEdit);
    } break;
    case AuthTypeFingerprint:
    case AuthTypeFace:
    case AuthTypeFingerVein:
    case AuthTypeIris: {
        mainLayout->setContentsMargins(27, 0, 10, 0);
        m_textLabel = new DLabel(this);
        mainLayout->addWidget(m_textLabel, 1, Qt::AlignHCenter | Qt::AlignVCenter);
        m_authStatus = new DLabel(this);
        mainLayout->addWidget(m_authStatus, 0, Qt::AlignRight | Qt::AlignVCenter);
        /* 解锁时间 */
        connect(this, &AuthenticationModule::unlockTimeChanged, this, [=] {
            if (m_integerMinutes == 1) {
                m_textLabel->setText(tr("Please try again 1 minute later"));
            } else if (m_integerMinutes > 1) {
                m_textLabel->setText(tr("Please try again %n minutes later", "", static_cast<int>(m_integerMinutes)));
            } else {
                QTimer::singleShot(1000, this, [=] {
                    emit activateAuthentication();
                });
                qInfo() << "Waiting authentication service...";
            }
        });
    } break;
    case AuthTypeActiveDirectory: {
        // TODO
    } break;
    default: {
        // TODO 默认认证控件类型
    } break;
    }
}

/**
 * @brief 显示认证结果
 *
 * @param status
 * @param result
 */
void AuthenticationModule::setAuthResult(const int status, const QString &result)
{
    switch (status) {
    case StatusCodeSuccess:
        m_status = status;
        setEnabled(false);
        if (m_textLabel != nullptr) {
            m_textLabel->setText(tr("Verification successful"));
        }
        if (m_lineEdit != nullptr) {
            setLineEditInfo(tr("Verification successful"), PlaceHolderText);
            setAnimationState(false);
            m_lineEdit->clear();
        }
        if (m_authStatus != nullptr) {
            setAuthStatus(":/misc/images/login_check.svg");
        }
        m_showPrompt = true;
        emit authFinished(m_authType, StatusCodeSuccess);
        emit requestChangeFocus();
        break;
    case StatusCodeFailure:
        m_status = status;
        setEnabled(true);
        if (m_textLabel != nullptr) {
            if (m_limitsInfo->maxTries - m_limitsInfo->numFailures > 1) {
                m_textLabel->setText(tr("Verification failed, %n chances left", "", static_cast<int>(m_limitsInfo->maxTries - m_limitsInfo->numFailures)));
            } else if (m_limitsInfo->maxTries - m_limitsInfo->numFailures == 1) {
                m_textLabel->setText(tr("Verification failed, only one chance left"));
            }
        }
        if (m_lineEdit != nullptr) {
            setAnimationState(false);
            m_lineEdit->clear();
            m_lineEdit->setFocus();
            if (m_authType == AuthTypePassword) {
                if (m_limitsInfo->maxTries - m_limitsInfo->numFailures > 1) {
                    setLineEditInfo(tr("Verification failed, %n chances left", "", static_cast<int>(m_limitsInfo->maxTries - m_limitsInfo->numFailures)), PlaceHolderText);
                } else if (m_limitsInfo->maxTries - m_limitsInfo->numFailures == 1) {
                    setLineEditInfo(tr("Verification failed, only one chance left"), PlaceHolderText);
                }
                setLineEditInfo(tr("Wrong Password"), AlertText);
            } else if (m_authType == AuthTypeUkey) {
                setLineEditInfo(tr("Wrong PIN"), AlertText);
            }
        }
        if (m_authStatus != nullptr) {
            setAuthStatus(":/misc/images/login_wait.svg");
        }
        m_showPrompt = false;
        emit authFinished(m_authType, StatusCodeFailure);
        emit activateAuthentication(); // TODO retry times
        break;
    case StatusCodeCancel:
        setEnabled(true);
        if (m_lineEdit != nullptr) {
            setAnimationState(false);
        }
        if (m_authStatus != nullptr) {
            setAuthStatus(":/misc/images/login_wait.svg");
            m_authStatus->show();
        }
        m_showPrompt = true;
        break;
    case StatusCodeTimeout:
        setEnabled(true);
        if (m_textLabel != nullptr) {
            m_textLabel->setText(result);
        }
        if (m_lineEdit != nullptr) {
            setLineEditInfo(result, AlertText);
            setAnimationState(false);
        }
        if (m_authStatus != nullptr) {
            setAuthStatus(":/misc/images/login_wait.svg");
            m_authStatus->show();
        }
        break;
    case StatusCodeError:
        setEnabled(true);
        if (m_textLabel != nullptr) {
            m_textLabel->setText(result);
            //指纹断电重新激活
            emit activateAuthentication();
        }
        if (m_lineEdit != nullptr) {
            setLineEditInfo(result, AlertText);
            setAnimationState(false);
        }
        if (m_authStatus != nullptr) {
            setAuthStatus(":/misc/images/login_wait.svg");
            m_authStatus->show();
        }
        break;
    case StatusCodeVerify:
        setEnabled(true);
        if (m_textLabel != nullptr) {
            m_textLabel->setText(result);
        }
        if (m_lineEdit != nullptr) {
            setAnimationState(true);
        }
        if (m_authStatus != nullptr) {
            setAuthStatus(":/misc/images/login_spinner.svg");
            m_authStatus->show();
        }
        break;
    case StatusCodeException:
        setEnabled(true);
        if (m_textLabel != nullptr) {
            m_textLabel->setText(result);
        }
        if (m_lineEdit != nullptr) {
            setLineEditInfo(tr("UKey is required"), PlaceHolderText);
            setAnimationState(false);
        }
        if (m_authStatus != nullptr) {
            setAuthStatus(":/misc/images/login_wait.svg");
            m_authStatus->show();
        }
        break;
    case StatusCodePrompt:
        setEnabled(true);
        if (m_textLabel != nullptr) {
            if (m_showPrompt) {
                m_textLabel->setText(tr("Verify your fingerprint"));
            }
        }
        if (m_lineEdit != nullptr) {
            setAnimationState(false);
            if (m_showPrompt) {
                if (m_authType == AuthTypePassword) {
                    setLineEditInfo(tr("Password"), PlaceHolderText);
                } else if (m_authType == AuthTypeUkey) {
                    setLineEditInfo(tr("Enter your PIN"), PlaceHolderText);
                } else {
                    setLineEditInfo(result, PlaceHolderText);
                }
            }
        }
        if (m_authStatus != nullptr) {
            setAuthStatus(":/misc/images/login_wait.svg");
        }
        break;
    case StatusCodeStarted:
        setEnabled(true);
        break;
    case StatusCodeEnded:
        if (m_lineEdit != nullptr) {
            setAnimationState(false);
        }
        break;
    case StatusCodeLocked:
        m_status = status;
        setEnabled(false);
        if (m_textLabel != nullptr) {
            m_textLabel->setText(tr("Fingerprint locked, use password please"));
        }
        if (m_lineEdit != nullptr) {
            setAnimationState(false);
            if (m_integerMinutes == 1) {
                setLineEditInfo(tr("Please try again 1 minute later"), PlaceHolderText);
            } else {
                setLineEditInfo(tr("Please try again %n minutes later", "", static_cast<int>(m_integerMinutes)), PlaceHolderText);
            }
        }
        if (m_authStatus != nullptr) {
            setAuthStatus(":/misc/images/login_lock.svg");
        }
        m_showPrompt = true;
        break;
    case StatusCodeRecover:
        /** TODO
         * 设备异常后，认证会被 end，当设备状态恢复正常后，会发出这个状态
         * 需要在这里调用 start 重新开启认证
         */
        setEnabled(true);
        if (m_authStatus != nullptr) {
            setAuthStatus(":/misc/images/login_wait.svg");
        }
        m_showPrompt = true;
        break;
    case StatusCodeUnlocked:
        setEnabled(true);
        if (m_authStatus != nullptr) {
            setAuthStatus(":/misc/images/login_wait.svg");
        }
        m_showPrompt = true;
        emit activateAuthentication();
        break;
    default:
        setEnabled(false);
        qWarning() << "Error! The status of authentication is wrong!" << status << result;
        break;
    }
    update();
}

/**
 * @brief 设置显示的文案
 *
 * @param text 要显示的内容
 */
void AuthenticationModule::setText(const QString &text)
{
    m_textLabel->setTextFormat(Qt::TextFormat::PlainText);
    DFontSizeManager::instance()->bind(m_textLabel, DFontSizeManager::T6);

    DPalette palette = m_textLabel->palette();
    palette.setColor(QPalette::WindowText, Qt::white);

    m_textLabel->setPalette(palette);
    m_textLabel->setText(text);
}

/**
 * @brief 判断输入框是否可用
 *
 */
bool AuthenticationModule::lineEditIsEnable()
{
    return m_lineEdit->isEnabled();
}

/**
 * @brief 设置输入框Alert
 *
 */
void AuthenticationModule::setLineEditBkColor(const bool value)
{
    if (m_lineEdit != nullptr)
        m_lineEdit->setAlert(value);
}

/**
 * @brief 设置认证状态  --- 不同认证方式可能用不同的图片，故预留此接口
 *
 * @param path 图片路径
 */
void AuthenticationModule::setAuthStatus(const QString &path)
{
    QPixmap pixmap = DHiDPIHelper::loadNxPixmap(path);
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    m_authStatus->setPixmap(pixmap);
}

/**
 * @brief 设置键盘大小写状态
 *
 * @param isCapsOn
 */
void AuthenticationModule::setCapsStatus(const bool isCapsOn)
{
    const QString path = isCapsOn ? ":/misc/images/caps_lock.svg" : "";
    QPixmap pixmap = DHiDPIHelper::loadNxPixmap(path);
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    m_capsStatus->setPixmap(pixmap);
}

/**
 * @brief 认证限制信息
 *
 * @param info
 */
void AuthenticationModule::setLimitsInfo(const LimitsInfo &info)
{
    m_limitsInfo->unlockTime = info.unlockTime;
    m_limitsInfo->locked = info.locked;
    m_limitsInfo->maxTries = info.maxTries;
    m_limitsInfo->numFailures = info.numFailures;
    m_limitsInfo->unlockSecs = info.unlockSecs;
    updateUnlockTime();
    if (info.locked) {
        setAuthResult(StatusCodeLocked, QString("Locked"));
    }
}

/**
 * @brief 设置小键盘锁状态
 *
 * @param path
 */
void AuthenticationModule::setNumLockStatus(const QString &path)
{
    QPixmap pixmap = DHiDPIHelper::loadNxPixmap(path);
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    m_numLockStatus->setPixmap(pixmap);
}

/**
 * @brief 设置要在输入框中显示的信息
 *
 * @param text  要显示的内容
 * @param type  要显示的类型
 */
void AuthenticationModule::setLineEditInfo(const QString &text, const TextType type)
{
    switch (type) {
    case AlertText: {
        m_lineEdit->showAlertMessage(text, this, 3000);
        m_lineEdit->setAlert(true);
        // m_lineEdit->lineEdit()->selectAll();
        break;
    }
    case InputText: {
        m_lineEdit->hideAlertMessage();
        int pos = m_lineEdit->lineEdit()->cursorPosition();
        m_lineEdit->setText(text);
        m_lineEdit->lineEdit()->setCursorPosition(pos);
        break;
    }
    case PlaceHolderText:
        m_lineEdit->setPlaceholderText(text);
        break;
    }
}

/**
 * @brief 获取输入框中的内容
 *
 * @return 输入框中的内容
 */
QString AuthenticationModule::lineEditText() const
{
    if (m_lineEdit != nullptr) {
        return m_lineEdit->text();
    } else {
        return QString();
    }
}

/**
 * @brief 设置键盘布局按钮是否显示
 *
 * @param visible
 */
void AuthenticationModule::setKeyboardButtonVisible(const bool visible)
{
    m_keyboardButton->setVisible(visible);
    if (visible) {
        setKeyboardButtontext(m_iconText);
    }
}

/**
 * @brief 设置键盘布局按钮上面显示的内容
 *
 * @param text      要显示的内容
 * @param visable   按钮是否显示
 */
void AuthenticationModule::setKeyboardButtonInfo(const QString &text)
{
    m_iconText = text;
    setKeyboardButtontext(m_iconText);
}

void AuthenticationModule::setKeyboardButtontext(const QString &text)
{
    QString tmpText = text;
    tmpText = tmpText.split(";").first();
    /* special mark in Japanese */
    if (tmpText.contains("/")) {
        tmpText = tmpText.split("/").last();
    }

    QFont font = m_lineEdit->font();
    font.setPixelSize(32);
    font.setWeight(QFont::DemiBold);
    int word_size = QFontMetrics(font).height();
    qreal device_ratio = this->devicePixelRatioF();
    QImage image(QSize(word_size, word_size) * device_ratio, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    image.setDevicePixelRatio(device_ratio);

    QPainter painter(&image);
    painter.setFont(font);
    painter.setPen(Qt::white);

    QRect image_rect = image.rect();
    QRect r(image_rect.left(), image_rect.top(), word_size, word_size);
    painter.drawText(r, Qt::AlignCenter, tmpText);

    m_keyboardButton->setIcon(QIcon(QPixmap::fromImage(image)));
}

/**
 * @brief 设置动画执行状态  ---  后续可添加更多动画
 *
 * @param start
 */
void AuthenticationModule::setAnimationState(const bool start)
{
    if (start) {
        m_lineEdit->startAnimation();
    } else {
        m_lineEdit->stopAnimation();
    }
}

/**
 * @brief 更新解锁剩余的时间
 *
 * @return QString
 */
void AuthenticationModule::updateUnlockTime()
{
    if (QDateTime::fromString(m_limitsInfo->unlockTime, Qt::ISODateWithMs) <= QDateTime::currentDateTime()) {
        m_integerMinutes = 0;
        m_unlockTimerTmp->stop();
        m_unlockTimer->stop();
        return;
    }
    uint intervalSeconds = QDateTime::fromString(m_limitsInfo->unlockTime, Qt::ISODateWithMs).toLocalTime().toTime_t()
                           - QDateTime::currentDateTimeUtc().toTime_t();
    uint remainderSeconds = intervalSeconds % 60;
    m_integerMinutes = (intervalSeconds - remainderSeconds) / 60 + 1;
    emit unlockTimeChanged();
    m_unlockTimerTmp->start(static_cast<int>(remainderSeconds * 1000));
}

bool AuthenticationModule::eventFilter(QObject *watched, QEvent *event)
{
    if (qobject_cast<DLineEditEx *>(watched) == m_lineEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->matches(QKeySequence::Cut)
            || keyEvent->matches(QKeySequence::Copy)
            || keyEvent->matches(QKeySequence::Paste)) {
            return true;
        }
    }
    return false;
}

void AuthenticationModule::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit activateAuthentication();
    }
}
