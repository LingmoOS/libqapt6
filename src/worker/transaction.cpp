/***************************************************************************
 *   Copyright © 2012 Jonathan Thomas <echidnaman@kubuntu.org>             *
 *   Copyright © 2008-2009 Sebastian Heinlein <devel@glatzor.de>           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License as        *
 *   published by the Free Software Foundation; either version 2 of        *
 *   the License or (at your option) version 3 or any later version        *
 *   accepted by the membership of KDE e.V. (or its successor approved     *
 *   by the membership of KDE e.V.), which shall act as a proxy            *
 *   defined in Section 14 of version 3 of the license.                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "transaction.h"

// Qt includes
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtDBus/QDBusConnection>

// Own includes
#include "qaptauthorization.h"
#include "transactionadapter.h"
#include "transactionqueue.h"

Transaction::Transaction(TransactionQueue *queue, int userId)
    : Transaction(queue, userId, QApt::EmptyRole, QVariantMap())
{
}

Transaction::Transaction(TransactionQueue *queue, int userId,
                         QApt::TransactionRole role, QVariantMap packagesList)
    : QObject(queue)
    , m_queue(queue)
    , m_tid(QUuid::createUuid().toString())
    , m_uid(userId)
    , m_role(role)
    , m_status(QApt::SetupStatus)
    , m_error(QApt::Success)
    , m_packages(packagesList)
    , m_isCancellable(true)
    , m_isCancelled(false)
    , m_exitStatus(QApt::ExitUnfinished)
    , m_isPaused(false)
    , m_progress(0)
    , m_dataMutex(QMutex::Recursive)
{
    new TransactionAdaptor(this);
    QDBusConnection connection = QDBusConnection::systemBus();

    QString tid = QUuid::createUuid().toString();
    // Remove parts of the uuid that can't be part of a D-Bus path
    tid.remove('{').remove('}').remove('-');
    m_tid = "/org/kubuntu/qaptworker/transaction" + tid;

    if (!connection.registerObject(m_tid, this))
        qWarning() << "Unable to register transaction on DBus";

    m_roleActionMap[QApt::EmptyRole] = QString();
    m_roleActionMap[QApt::UpdateCacheRole] = QLatin1String("org.kubuntu.qaptworker.updatecache");
    m_roleActionMap[QApt::UpgradeSystemRole] = QLatin1String("org.kubuntu.qaptworker.commitchanges");
    m_roleActionMap[QApt::CommitChangesRole] = QLatin1String("org.kubuntu.qaptworker.commitchanges");
    m_roleActionMap[QApt::UpdateXapianRole] = QString();
    m_roleActionMap[QApt::DownloadArchivesRole] = QString();
    m_roleActionMap[QApt::InstallFileRole] = QLatin1String("org.kubuntu.qaptworker.commitchanges");

    m_queue->addPending(this);
    m_idleTimer = new QTimer(this);
    connect(m_idleTimer, SIGNAL(timeout()),
            this, SLOT(emitIdleTimeout()));
}

Transaction::~Transaction()
{
    QDBusConnection::systemBus().unregisterObject(m_tid);
}

QString Transaction::transactionId() const
{
    return m_tid;
}

int Transaction::userId() const
{
    return m_uid;
}

int Transaction::role()
{
    QMutexLocker lock(&m_dataMutex);

    return m_role;
}

void Transaction::setRole(int role)
{
    QMutexLocker lock(&m_dataMutex);
    // Cannot change role for an already determined transaction
    if (m_role != QApt::EmptyRole) {
        sendErrorReply(QDBusError::Failed);

        return;
    }

    m_role = (QApt::TransactionRole)role;

    emit propertyChanged(QApt::RoleProperty, QDBusVariant(role));
}

int Transaction::status()
{
    QMutexLocker lock(&m_dataMutex);

    return m_status;
}

void Transaction::setStatus(QApt::TransactionStatus status)
{
    QMutexLocker lock(&m_dataMutex);
    m_status = status;
    emit propertyChanged(QApt::StatusProperty, QDBusVariant((int)status));

    if (m_status != QApt::SetupStatus && m_idleTimer) {
        m_idleTimer->stop(); // We are now queued and are no longer idle
        // We don't need the timer anymore
        m_idleTimer->deleteLater();
        m_idleTimer = nullptr;
    }
}

int Transaction::error()
{
    QMutexLocker lock(&m_dataMutex);

    return m_error;
}

void Transaction::setError(QApt::ErrorCode code)
{
    m_error = code;
    emit propertyChanged(QApt::ErrorProperty, QDBusVariant((int)code));
}

QString Transaction::locale()
{
    QMutexLocker lock(&m_dataMutex);

    return m_locale;
}

void Transaction::setLocale(QString locale)
{
    QMutexLocker lock(&m_dataMutex);

    if (m_status != QApt::SetupStatus) {
        sendErrorReply(QDBusError::Failed);
        return;
    }

    m_locale = locale;
    emit propertyChanged(QApt::LocaleProperty, QDBusVariant(locale));
}

QString Transaction::proxy()
{
    QMutexLocker lock(&m_dataMutex);

    return m_proxy;
}

void Transaction::setProxy(QString proxy)
{
    QMutexLocker lock(&m_dataMutex);

    if (m_status != QApt::SetupStatus) {
        sendErrorReply(QDBusError::Failed);
        return;
    }

    m_proxy = proxy;
    emit propertyChanged(QApt::ProxyProperty, QDBusVariant(proxy));
}

QString Transaction::debconfPipe()
{
    QMutexLocker lock(&m_dataMutex);

    return m_debconfPipe;
}

void Transaction::setDebconfPipe(QString pipe)
{
    QMutexLocker lock(&m_dataMutex);

    if (m_status != QApt::SetupStatus) {
        sendErrorReply(QDBusError::Failed);
        return;
    }

    QFileInfo pipeInfo(pipe);

    if (!pipeInfo.exists() || pipeInfo.ownerId() != m_uid) {
        sendErrorReply(QDBusError::InvalidArgs);
        return;
    }

    m_debconfPipe = pipe;
    emit propertyChanged(QApt::DebconfPipeProperty, QDBusVariant(pipe));
}

QVariantMap Transaction::packages()
{
    QMutexLocker lock(&m_dataMutex);

    return m_packages;
}

void Transaction::setPackages(QVariantMap packageList)
{
    QMutexLocker lock(&m_dataMutex);

    if (m_status != QApt::SetupStatus) {
        sendErrorReply(QDBusError::Failed);
        return;
    }

    m_packages = packageList;
    emit propertyChanged(QApt::PackagesProperty, QDBusVariant(packageList));
}

bool Transaction::isCancellable()
{
    QMutexLocker lock(&m_dataMutex);

    return m_isCancellable;
}

void Transaction::setCancellable(bool cancellable)
{
    QMutexLocker lock(&m_dataMutex);

    m_isCancellable = cancellable;
    emit propertyChanged(QApt::CancellableProperty, QDBusVariant(cancellable));
}

bool Transaction::isCancelled()
{
    QMutexLocker lock(&m_dataMutex);

    return m_isCancelled;
}

int Transaction::exitStatus()
{
    QMutexLocker lock(&m_dataMutex);

    return (int)m_exitStatus;
}

void Transaction::setExitStatus(QApt::ExitStatus exitStatus)
{
    QMutexLocker lock(&m_dataMutex);

    setStatus(QApt::FinishedStatus);
    m_exitStatus = exitStatus;
    emit propertyChanged(QApt::ExitStatusProperty, QDBusVariant(exitStatus));
    emit finished(exitStatus);
}

QString Transaction::medium()
{
    QMutexLocker lock(&m_dataMutex);

    return m_medium;
}

void Transaction::setMediumRequired(const QString &label, const QString &medium)
{
    QMutexLocker lock(&m_dataMutex);

    m_medium = medium;
    m_isPaused = true;

    emit mediumRequired(label, medium);
}

bool Transaction::isPaused()
{
    QMutexLocker lock(&m_dataMutex);

    return m_isPaused;
}

void Transaction::setIsPaused(bool paused)
{
    QMutexLocker lock(&m_dataMutex);

    m_isPaused = paused;
}

QString Transaction::statusDetails()
{
    QMutexLocker lock(&m_dataMutex);

    return m_statusDetails;
}

void Transaction::setStatusDetails(const QString &details)
{
    QMutexLocker lock(&m_dataMutex);

    m_statusDetails = details;
    emit propertyChanged(QApt::StatusDetailsProperty, QDBusVariant(details));
}

int Transaction::progress()
{
    QMutexLocker lock(&m_dataMutex);

    return m_progress;
}

void Transaction::setProgress(int progress)
{
    QMutexLocker lock(&m_dataMutex);

    m_progress = progress;
    emit propertyChanged(QApt::ProgressProperty, QDBusVariant(progress));
}

QString Transaction::service() const
{
    return m_service;
}

QApt::DownloadProgress Transaction::downloadProgress()
{
    QMutexLocker lock(&m_dataMutex);

    return m_downloadProgress;
}

void Transaction::setDownloadProgress(const QApt::DownloadProgress &downloadProgress)
{
    QMutexLocker lock(&m_dataMutex);

    m_downloadProgress = downloadProgress;
    emit propertyChanged(QApt::DownloadProgressProperty,
                         QDBusVariant(QVariant::fromValue((downloadProgress))));
}

void Transaction::setService(const QString &service)
{
    m_service = service;
}

void Transaction::run()
{
    setDelayedReply(true);
    if (isForeignUser() || !authorizeRun()) {
        qDebug() << "auth error";
        sendErrorReply(QDBusError::AccessDenied);
        return;
    }

    QMutexLocker lock(&m_dataMutex);
    m_queue->enqueue(m_tid);
    setStatus(QApt::WaitingStatus);
}

int Transaction::dbusSenderUid() const
{
    return connection().interface()->serviceUid(message().service()).value();
}

bool Transaction::isForeignUser() const
{
    return dbusSenderUid() != m_uid;
}

bool Transaction::authorizeRun()
{
    m_dataMutex.lock();
    QString action = m_roleActionMap.value(m_role);
    m_dataMutex.unlock();

    // Some actions don't need authorizing, and are run in the worker
    // for the sake of asynchronicity.
    if (action.isEmpty())
        return true;

    setStatus(QApt::AuthenticationStatus);

    return QApt::Auth::authorize(action, m_service);
}

void Transaction::setProperty(int property, QDBusVariant value)
{
    if (isForeignUser()) {
        sendErrorReply(QDBusError::AccessDenied);
        return;
    }

    switch (property)
    {
    case QApt::TransactionIdProperty:
    case QApt::UserIdProperty:
        // Read-only
        sendErrorReply(QDBusError::Failed);
        break;
    case QApt::RoleProperty:
        setRole((QApt::TransactionProperty)value.variant().toInt());
        break;
    case QApt::StatusProperty:
        setStatus((QApt::TransactionStatus)value.variant().toInt());
        break;
    case QApt::LocaleProperty:
        setLocale(value.variant().toString());
        break;
    case QApt::ProxyProperty:
        setProxy(value.variant().toString());
        break;
    case QApt::DebconfPipeProperty:
        setDebconfPipe(value.variant().toString());
        break;
    case QApt::PackagesProperty:
        setPackages(value.variant().toMap());
        break;
    default:
        sendErrorReply(QDBusError::InvalidArgs);
        break;
    }
}

void Transaction::cancel()
{
    if (isForeignUser()) {
        if (!QApt::Auth::authorize(QLatin1String("org.kubuntu.qaptworker.foreigncancel"),
                                   QLatin1String("org.kubuntu.qaptworker"))) {
            sendErrorReply(QDBusError::AccessDenied);
            return;
        }
    }

    QMutexLocker lock(&m_dataMutex);
    // We can only cancel cancellable transactions, obviously
    if (!m_isCancellable) {
        sendErrorReply(QDBusError::Failed);
        return;
    }

    m_isCancelled = true;
    m_isPaused = false;
    emit propertyChanged(QApt::CancelledProperty, QDBusVariant(m_isCancelled));
}

void Transaction::provideMedium(const QString &medium)
{
    QMutexLocker lock(&m_dataMutex);

    if (isForeignUser()) {
        sendErrorReply(QDBusError::AccessDenied);
        return;
    }

    // An incorrect medium was provided, or no medium was requested
    if (medium != m_medium || m_medium.isEmpty()) {
        sendErrorReply(QDBusError::Failed);
        return;
    }

    // The medium has now been provided, and the installation should be able to continue
    m_isPaused = false;
}

void Transaction::emitIdleTimeout()
{
    emit idleTimeout(this);
}
