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

#ifndef FULLSCREENBACKGROUND_H
#define FULLSCREENBACKGROUND_H

#include <QWidget>

#include <com_deepin_daemon_imageeffect.h>

using ImageEffectInter = com::deepin::daemon::ImageEffect;

class FullscreenBackground : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool contentVisible READ contentVisible WRITE setContentVisible NOTIFY contentVisibleChanged)

public:
    explicit FullscreenBackground(QWidget *parent = nullptr);
    ~FullscreenBackground();

    bool contentVisible() const;
    void enableEnterEvent(bool enable);

public slots:
    void updateBackground(const QString &path);
    void updateBlurBackground(const QString &path);
    void setScreen(QScreen *screen);
    void setContentVisible(bool contentVisible);
    void setIsBlackMode(bool is_black);
    void setIsHibernateMode();

signals:
    void contentVisibleChanged(bool contentVisible);

protected:
    void setContent(QWidget *const w);
    void keyPressEvent(QKeyEvent *e) Q_DECL_OVERRIDE;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *e) override;
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;

private:
    void paintEvent(QPaintEvent *e) Q_DECL_OVERRIDE;
    void enterEvent(QEvent *event) Q_DECL_OVERRIDE;
    void leaveEvent(QEvent *event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    const QPixmap pixmapHandle(const QPixmap &pixmap);
    void updateScreen(QScreen *screen);
    void updateGeometry();
    bool isPicture(const QString &file);
    QString getLocalFile(const QString &file);

private:
    QPixmap m_background;                 // ????????????
    QPixmap m_backgroundCache;            // ??????????????????
    QPixmap m_blurBackground;             // ????????????
    QPixmap m_blurBackgroundCache;        // ??????????????????
    QVariantAnimation *m_fadeOutAni;      // ????????????
    ImageEffectInter *m_imageEffectInter; // ????????????????????????

    QPointer<QWidget> m_content;
    QScreen *m_screen = nullptr;
    bool m_primaryShowFinished = false;
    bool m_isBlackMode = false;
    bool m_enableEnterEvent = true;
    QCursor m_originalCursor;
    bool m_blackModeContentVisible = true;
};

#endif // FULLSCREENBACKGROUND_H
