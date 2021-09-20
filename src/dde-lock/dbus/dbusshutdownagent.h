/*
 * Copyright (C) 2011 ~ 2020 Uniontech Technology Co., Ltd.
 *
 * Author:     chenjun <chenjun@uniontech>
 *
 * Maintainer: chenjun <chenjun@uniontech>
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

#ifndef DBUSSHUTDOWNAGENT_H
#define DBUSSHUTDOWNAGENT_H

#include <QObject>

class SessionBaseModel;
class DBusShutdownAgent : public QObject
{
    Q_OBJECT
public:
    explicit DBusShutdownAgent(QObject *parent = nullptr);
    void setModel(SessionBaseModel *const model);
    void show();
    void Shutdown();
    void Restart();
    void Logout();
    void Suspend();
    void Hibernate();
    void SwitchUser();
    void Lock();

private:
    bool canShowShutDown();

private:
    SessionBaseModel *m_model;
};

#endif // DBUSSHUTDOWNAGENT_H
