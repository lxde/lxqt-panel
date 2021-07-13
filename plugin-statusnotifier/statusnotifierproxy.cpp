/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * LXQt - a lightweight, Qt based, desktop toolset
 * https://lxqt.org
 *
 * Copyright: 2021 LXQt team
 *
 * This program or library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * END_COMMON_COPYRIGHT_HEADER */

#include "statusnotifierproxy.h"
#include "statusnotifierwatcher.h"
#include "statusnotifierwidget.h"

#include <QApplication>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QDBusConnectionInterface>
#include <QDebug>

Q_GLOBAL_STATIC(StatusNotifierProxy, statusNotifierProxy)

StatusNotifierProxy::StatusNotifierProxy()
    : mWatcher(nullptr),
    mWidgetCount(0),
    mCreatingWatcher(false)
{
    createWatcher();
}

void StatusNotifierProxy::createWatcher()
{
    mCreatingWatcher = true;
    QFutureWatcher<StatusNotifierWatcher *> * future_watcher = new QFutureWatcher<StatusNotifierWatcher *>;
    connect(future_watcher, &QFutureWatcher<StatusNotifierWatcher *>::finished, this, [this, future_watcher]
        {
            mWatcher = future_watcher->future().result();
            watcherCreated();

            connect(mWatcher, &StatusNotifierWatcher::StatusNotifierItemRegistered,
                    this, &StatusNotifierProxy::onStatusNotifierItemRegistered);
            connect(mWatcher, &StatusNotifierWatcher::StatusNotifierItemUnregistered,
                    this, &StatusNotifierProxy::onStatusNotifierItemUnregistered);

            qDebug() << mWatcher->RegisteredStatusNotifierItems();

            future_watcher->deleteLater();
        });

    QFuture<StatusNotifierWatcher *> future = QtConcurrent::run([]
        {
            QString dbusName = QStringLiteral("org.kde.StatusNotifierHost-%1-%2").arg(QApplication::applicationPid()).arg(1);
            if (QDBusConnectionInterface::ServiceNotRegistered == QDBusConnection::sessionBus().interface()->registerService(dbusName, QDBusConnectionInterface::DontQueueService))
                qDebug() << "unable to register service for " << dbusName;

            StatusNotifierWatcher * watcher = new StatusNotifierWatcher;
            watcher->RegisterStatusNotifierHost(dbusName);
            watcher->moveToThread(QApplication::instance()->thread());
            return watcher;
        });

    future_watcher->setFuture(future);
}

void StatusNotifierProxy::watcherCreated()
{
    mCreatingWatcher = false;
}

StatusNotifierProxy::~StatusNotifierProxy()
{
    qDebug() << "deleting Proxy";
}

StatusNotifierProxy *StatusNotifierProxy::instance()
{
    return statusNotifierProxy();
}

void StatusNotifierProxy::registerWidget(StatusNotifierWidget *widget)
{
    ++mWidgetCount;
    if (nullptr == mWatcher && mCreatingWatcher == false)
        createWatcher();
    for (auto i = mServices.cbegin(); i != mServices.cend(); ++i)
        widget->itemAdded(*i);

    connect(this, &StatusNotifierProxy::StatusNotifierItemRegistered,
                    widget, &StatusNotifierWidget::itemAdded);
    connect(this, &StatusNotifierProxy::StatusNotifierItemUnregistered,
                    widget, &StatusNotifierWidget::itemRemoved);
}

void StatusNotifierProxy::unregisterWidget(StatusNotifierWidget */*widget*/)
{
    --mWidgetCount;
    if (mWidgetCount == 0) {
        mWatcher->deleteLater();
        mWatcher = nullptr;
        mServices.clear();
    }
}

void StatusNotifierProxy::onStatusNotifierItemRegistered(const QString &service)
{
    mServices.append(service);
    emit StatusNotifierItemRegistered(service);
}

void StatusNotifierProxy::onStatusNotifierItemUnregistered(const QString &service)
{
    mServices.removeAll(service);
    emit StatusNotifierItemUnregistered(service);
}
