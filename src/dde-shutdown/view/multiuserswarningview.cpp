/*
 * Copyright (C) 2011 ~ 2018 Deepin Technology Co., Ltd.
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

#include "multiuserswarningview.h"

#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDebug>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QPushButton>

const static QSize UserAvatarSize = QSize(64, 64);
const static QSize UserListItemSize = QSize(180, 80);

MultiUsersWarningView::MultiUsersWarningView(QWidget *parent)
    : WarningView(parent)
    , m_vLayout(new QVBoxLayout(this))
    , m_userList(new QListWidget)
    , m_warningTip(new QLabel)
    , m_cancelBtn(new QPushButton(tr("Cancel")))
    , m_actionBtn(new QPushButton(QString()))
    , m_currentBtn(nullptr)
{
    m_userList->setAttribute(Qt::WA_TranslucentBackground);
//    m_userList->setSelectionRectVisible(false);
    m_userList->setSelectionMode(QListView::NoSelection);
    m_userList->setEditTriggers(QListView::NoEditTriggers);
    m_userList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_userList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
//    m_userList->viewport()->setAttribute(Qt::WA_TranslucentBackground);
    m_userList->setFrameStyle(QFrame::NoFrame);
    m_userList->setGridSize(UserListItemSize);
    m_userList->setFocusPolicy(Qt::NoFocus);
    m_userList->setStyleSheet("background-color:transparent;");

    m_warningTip->setFixedWidth(300);
    m_warningTip->setStyleSheet("color: white;");
    m_warningTip->setWordWrap(true);
    m_warningTip->setAlignment(Qt::AlignCenter);
    m_warningTip->setFocusPolicy(Qt::NoFocus);
    m_warningTip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_actionBtn->setIconSize(QSize(m_buttonIconSize, m_buttonIconSize));
    m_actionBtn->setFixedSize(m_buttonWidth, m_buttonHeight);
    m_cancelBtn->setIconSize(QSize(m_buttonIconSize, m_buttonIconSize));
    m_cancelBtn->setFixedSize(m_buttonWidth, m_buttonHeight);

    const auto ratio = devicePixelRatioF();
    QIcon icon_pix = QIcon::fromTheme(":/img/cancel_normal.svg").pixmap(m_cancelBtn->iconSize() * ratio);
    m_cancelBtn->setIcon(icon_pix);

    QVBoxLayout *btnLayout = new QVBoxLayout;
    btnLayout->addStretch(1);
    btnLayout->addWidget(m_cancelBtn, 0, Qt::AlignHCenter);
    btnLayout->addSpacing(20);
    btnLayout->addWidget(m_actionBtn, 0, Qt::AlignHCenter);
    btnLayout->addStretch(1);

    m_vLayout->addStretch();
    m_vLayout->addWidget(m_userList, 0, Qt::AlignHCenter);
    m_vLayout->addSpacing(40);
    m_vLayout->addWidget(m_warningTip, 1, Qt::AlignHCenter);
    m_vLayout->addSpacing(40);
    m_vLayout->addLayout(btnLayout);
    m_vLayout->addStretch();

    m_cancelBtn->setChecked(true);
    m_currentBtn = m_cancelBtn;

    connect(m_cancelBtn, &QPushButton::clicked, this, &MultiUsersWarningView::cancelled);
    connect(m_actionBtn, &QPushButton::clicked, this, &MultiUsersWarningView::actionInvoked);
}

MultiUsersWarningView::~MultiUsersWarningView()
{
}

void MultiUsersWarningView::setUsers(QList<std::shared_ptr<User>> users)
{
    m_userList->clear();

    m_userList->setFixedSize(UserListItemSize.width(),
                             UserListItemSize.height() * qMin(users.length(), 4));

    for (std::shared_ptr<User> user : users) {
        QListWidgetItem * item = new QListWidgetItem;
        m_userList->addItem(item);

        QString icon = getUserIcon(user->avatarPath());
        m_userList->setItemWidget(item, new UserListItem(icon, user->name()));
    }
}

Actions MultiUsersWarningView::action() const
{
    return m_action;
}

void MultiUsersWarningView::setAction(const Actions action)
{
    QString icon_string;
    switch (action) {
    case Actions::Shutdown:
        icon_string = ":/img/poweroff_warning_normal.svg";
        m_warningTip->setText(tr("The above users are still logged in and data will be lost due to shutdown, are you sure you want to shut down?"));
        break;
    default:
        icon_string = ":/img/reboot_warning_normal.svg";
        m_warningTip->setText(tr("The above users are still logged in and data will be lost due to reboot, are you sure you want to reboot?"));
        break;
    }

    const auto ratio = devicePixelRatioF();
    QIcon icon_pix = QIcon::fromTheme(icon_string).pixmap(m_actionBtn->iconSize() * ratio);
    m_actionBtn->setIcon(icon_pix);
}

void MultiUsersWarningView::toggleButtonState()
{
    if (m_actionBtn->isChecked())
        setCurrentButton(ButtonType::Cancel);
    else
        setCurrentButton(ButtonType::Accept);
}

void MultiUsersWarningView::buttonClickHandle()
{
    emit m_currentBtn->clicked();
}

void MultiUsersWarningView::setAcceptReason(const QString &reason)
{
    m_actionBtn->setText(reason);
}

bool MultiUsersWarningView::focusNextPrevChild(bool next)
{
    if (!next) {
        qWarning() << "focus handling error, nextPrevChild is False";
        return WarningView::focusNextPrevChild(next);
    }

    if (m_actionBtn->hasFocus())
        setCurrentButton(ButtonType::Cancel);
    else
        setCurrentButton(ButtonType::Accept);

    return WarningView::focusNextPrevChild(next);
}

void MultiUsersWarningView::setCurrentButton(const ButtonType btntype)
{
    switch (btntype) {
    case ButtonType::Cancel:
        m_actionBtn->setChecked(false);
        m_cancelBtn->setChecked(true);
        m_currentBtn = m_cancelBtn;
        break;

    case ButtonType::Accept:
        m_cancelBtn->setChecked(false);
        m_actionBtn->setChecked(true);
        m_currentBtn = m_actionBtn;
        break;
    }
}

QString MultiUsersWarningView::getUserIcon(const QString &path)
{
    const QUrl url(path);
    if (url.isLocalFile())
        return url.path();

    return path;
}

UserListItem::UserListItem(const QString &icon, const QString &name) :
    QFrame(),
    m_icon(new QLabel(this)),
    m_name(new QLabel(name, this))
{
    setFixedSize(UserListItemSize);

    m_icon->setFixedSize(UserAvatarSize);
    m_icon->setScaledContents(true);
    m_icon->setPixmap(getRoundPixmap(icon));

    m_name->setStyleSheet("color: white;");
    m_name->move(80, 20);
}

QPixmap UserListItem::getRoundPixmap(const QString &path)
{
    QPixmap source(path);
    QPixmap result(source.size());
    result.fill(Qt::transparent);

    QPainter p(&result);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath pp;
    pp.addEllipse(result.rect());
    p.setClipPath(pp);
    p.drawPixmap(result.rect(), source);
    p.end();

    return result;
}
