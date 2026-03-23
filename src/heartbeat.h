#ifndef HEARTBEATTHREAD_H
#define HEARTBEATTHREAD_H
#include "iDescriptor.h"
#include <QDebug>
#include <QThread>
#include <atomic>

using namespace IdeviceFFI;

class HeartbeatThread : public QThread
{
    Q_OBJECT
public:
    HeartbeatThread(HeartbeatClientHandle *heartbeat,
                    iDescriptor::Uniq macAddress, QObject *parent = nullptr)
        : QThread(parent), m_hb(Heartbeat::adopt(heartbeat)),
          m_macAddress(macAddress)
    {
    }

    void run() override
    {
        qDebug() << "Heartbeat thread started";
        try {
            u_int64_t interval = 15;

            while (!isInterruptionRequested()) {
                Result result = m_hb.get_marco(interval);
                if (result.is_err() && !isInterruptionRequested()) {
                    qDebug()
                        << "Failed to get marco:"
                        << QString::fromStdString(result.unwrap_err().message);
                    m_tries++;
                    emit heartbeatFailed(m_macAddress, m_tries);
                    if (m_tries >= HEARTBEAT_RETRY_LIMIT) {
                        qDebug()
                            << "Maximum heartbeat retries reached, exiting for "
                               "device"
                            << m_macAddress;
                        m_exited = true;
                        emit heartbeatThreadExited(m_macAddress);
                        break;
                    }
                    continue;
                }

                interval = result.unwrap();
                qDebug() << "Received marco, new interval:" << interval;

                if (isInterruptionRequested()) {
                    break;
                }
                Result polo_result = m_hb.send_polo();
                if (polo_result.is_err() && !isInterruptionRequested()) {
                    qDebug() << "Failed to send polo:"
                             << QString::fromStdString(
                                    polo_result.unwrap_err().message);
                    m_tries++;
                    emit heartbeatFailed(m_macAddress, m_tries);
                    if (m_tries >= HEARTBEAT_RETRY_LIMIT) {
                        qDebug() << "Maximum heartbeat retries reached, "
                                    "exiting for "
                                    "device"
                                 << m_macAddress;
                        m_exited = true;
                        emit heartbeatThreadExited(m_macAddress);
                        break;
                    }
                    continue;
                }

                qDebug() << "Sent polo successfully";
                interval += 5;
                m_initialCompleted = true;
            }
        } catch (const std::exception &e) {
            qDebug() << "Heartbeat error:" << e.what();

            emit heartbeatThreadExited(m_macAddress);
        }
    }

    bool initialCompleted() const { return m_initialCompleted; }
    bool exited() const { return m_exited.load(); }

private:
    Heartbeat m_hb;
    bool m_initialCompleted = false;
    iDescriptor::Uniq m_macAddress;
    unsigned int m_tries = 0;
    std::atomic<bool> m_exited{false};

signals:
    void heartbeatFailed(const QString &macAddress, unsigned int tries = 0);
    void heartbeatThreadExited(const iDescriptor::Uniq &uniq);
};
#endif // HEARTBEATTHREAD_H