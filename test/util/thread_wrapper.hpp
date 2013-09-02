/*************************************************************************
 *
 * TIGHTDB CONFIDENTIAL
 * __________________
 *
 *  [2011] - [2012] TightDB Inc
 *  All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains
 * the property of TightDB Incorporated and its suppliers,
 * if any.  The intellectual and technical concepts contained
 * herein are proprietary to TightDB Incorporated
 * and its suppliers and may be covered by U.S. and Foreign Patents,
 * patents in process, and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from TightDB Incorporated.
 *
 **************************************************************************/
#ifndef TIGHTDB_TEST_UTIL_THREAD_WRAPPER_HPP
#define TIGHTDB_TEST_UTIL_THREAD_WRAPPER_HPP

#include <exception>
#include <string>
#include <iostream>

#include <tightdb/thread.hpp>
#include <tightdb/bind.hpp>

namespace tightdb {
namespace test_util {


/// Catch exceptions thrown in threads and make the exception message
/// available to the thread that calls ThreadWrapper::join().
class ThreadWrapper {
public:
    template<class F> void start(const F& func)
    {
        m_except = false;
        m_thread.start(util::bind(&Runner<F>::run, func, this));
    }

    /// Returns 'true' if thread has thrown an exception. In that case
    /// the exception message will also be writte to std::cerr.
    bool join()
    {
        std::string except_msg;
        if (join(except_msg)) {
            std::cerr << "Exception thrown in thread: "<<except_msg<<"\n";
            return true;
        }
        return false;
    }

    /// Returns 'true' if thread has thrown an exception. In that
    /// case the exception message will have been assigned to \a
    /// except_msg.
    bool join(std::string& except_msg)
    {
        m_thread.join();
        if (m_except) {
            except_msg = m_except_msg;
            return true;
        }
        return false;
    }

private:
    Thread m_thread;
    bool m_except;
    std::string m_except_msg;

    template<class F> struct Runner {
        static void run(F func, ThreadWrapper* tw)
        {
            try {
                func();
            }
            catch (std::exception& e) {
                tw->m_except = true;
                tw->m_except_msg = e.what();
            }
            catch (...) {
                tw->m_except = true;
                tw->m_except_msg = "Unknown error";
            }
        }
    };
};


} // namespace test_util
} // namespace tightdb

#endif // TIGHTDB_TEST_UTIL_THREAD_WRAPPER_HPP
