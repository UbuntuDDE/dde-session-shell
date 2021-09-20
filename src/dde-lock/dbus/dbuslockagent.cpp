#include "dbuslockagent.h"
#include "sessionbasemodel.h"

DBusLockAgent::DBusLockAgent(QObject *parent) : QObject(parent), m_model(nullptr)
{

}

void DBusLockAgent::setModel(SessionBaseModel *const model)
{
    m_model = model;
}

void DBusLockAgent::Show()
{
    m_model->setIsBlackModel(false);
    m_model->setIsHibernateModel(false);
    m_model->setVisible(true);
}

void DBusLockAgent::ShowAuth(bool active)
{
    Show();
    emit m_model->activeAuthChanged(!active);
}

// 待机，enable=true：进入待机；enable=false：待机恢复
void DBusLockAgent::Suspend(bool enable)
{
    if (enable) {
        m_model->setIsBlackModel(true);
        m_model->setVisible(true);
    } else {
        QDBusInterface infc("com.deepin.daemon.Power","/com/deepin/daemon/Power","com.deepin.daemon.Power");
        // 待机恢复需要密码
        bool bSuspendLock = infc.property("SleepLock").toBool();

        if (bSuspendLock) {
            m_model->setIsBlackModel(false);
            m_model->setVisible(true);
        } else {
            m_model->setVisible(false);
            emit m_model->visibleChanged(false);
        }
    }
}

void DBusLockAgent::Hibernate(bool enable)
{
    m_model->setIsHibernateModel(enable);
    m_model->setVisible(true);
}

void DBusLockAgent::ShowUserList()
{
    emit m_model->showUserList();
}
