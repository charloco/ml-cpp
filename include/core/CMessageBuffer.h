/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
#ifndef INCLUDED_ml_learn_CMessageBuffer_h
#define INCLUDED_ml_learn_CMessageBuffer_h

#include <core/CCondition.h>
#include <core/CLogger.h>
#include <core/CMutex.h>
#include <core/CScopedLock.h>
#include <core/CThread.h>
#include <core/CoreTypes.h>

#include <limits>

namespace ml {
namespace core {

//! \brief
//! A thread safe message buffer.
//!
//! DESCRIPTION:\n
//! A thread safe message buffer.
//!
//! IMPLEMENTATION DECISIONS:\n
//! A generic interface for a buffer where data
//! can be added in one thread (addMessage) and
//! flushed and processed in another (flush) and (process)
//!
template<typename MESSAGE, typename BUFFER>
class CMessageBuffer {
public:
    CMessageBuffer(BUFFER& buffer)
        : m_Thread(*this), m_Condition(m_Mutex), m_Buffer(buffer) {}

    virtual ~CMessageBuffer() {}

    //! Initialise - create the receiving thread
    bool start() {
        CScopedLock lock(m_Mutex);

        if (m_Thread.start() == false) {
            LOG_ERROR(<< "Unable to initialise thread");
            return false;
        }

        m_Condition.wait();

        return true;
    }

    //! Shutdown - kill thread
    bool stop() {
        m_Thread.stop();

        return true;
    }

    void addMessage(const MESSAGE& msg) {
        CScopedLock lock(m_Mutex);

        m_Buffer.addMessage(msg);
    }

private:
    class CMessageBufferThread : public CThread {
    public:
        CMessageBufferThread(CMessageBuffer<MESSAGE, BUFFER>& messageBuffer)
            : m_MessageBuffer(messageBuffer), m_Shutdown(false), m_IsRunning(false) {}

        //! The queue must have the mutex for this to be called
        bool isRunning() const {
            // Assumes lock
            return m_IsRunning;
        }

    protected:
        void run() {
            using TMessageVec = std::vector<MESSAGE>;

            m_MessageBuffer.m_Mutex.lock();

            m_IsRunning = true;

            m_MessageBuffer.m_Condition.signal();

            while (!m_Shutdown) {
                m_MessageBuffer.m_Condition.wait(m_MessageBuffer.m_Buffer.flushInterval());

                TMessageVec data;

                core_t::TTime flushedTime(m_MessageBuffer.m_Buffer.flushMessages(data));
                m_MessageBuffer.m_Mutex.unlock();
                m_MessageBuffer.m_Buffer.processMessages(data, flushedTime);
                m_MessageBuffer.m_Mutex.lock();
            }

            // Flush outstanding messages (maintain lock)
            TMessageVec data;

            m_MessageBuffer.m_Buffer.flushAllMessages(data);
            m_MessageBuffer.m_Buffer.processMessages(
                data, std::numeric_limits<core_t::TTime>::max());

            m_IsRunning = false;

            m_MessageBuffer.m_Mutex.unlock();
        }

        void shutdown() {
            CScopedLock lock(m_MessageBuffer.m_Mutex);

            m_Shutdown = true;
            m_MessageBuffer.m_Condition.signal();
        }

    private:
        CMessageBuffer<MESSAGE, BUFFER>& m_MessageBuffer;
        bool m_Shutdown;
        bool m_IsRunning;
    };

    CMessageBufferThread m_Thread;
    CMutex m_Mutex;
    CCondition m_Condition;
    BUFFER& m_Buffer;

    friend class CMessageBufferThread;
};
}
}

#endif // INCLUDED_ml_learn_CMessageBuffer_h
