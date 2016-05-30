#include "testsettings.hpp"
#ifdef TEST_THREAD

#include <cstring>
#include <algorithm>
#include <queue>
#include <functional>
#include <mutex>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <realm/utilities.hpp>
#include <realm/util/features.h>
#include <realm/util/thread.hpp>
#ifndef _WIN32
#include <realm/util/interprocess_condvar.hpp>
#endif
#include <realm/util/interprocess_mutex.hpp>

#include <iostream>
#include "test.hpp"

using namespace realm;
using namespace realm::util;


// Test independence and thread-safety
// -----------------------------------
//
// All tests must be thread safe and independent of each other. This
// is required because it allows for both shuffling of the execution
// order and for parallelized testing.
//
// In particular, avoid using std::rand() since it is not guaranteed
// to be thread safe. Instead use the API offered in
// `test/util/random.hpp`.
//
// All files created in tests must use the TEST_PATH macro (or one of
// its friends) to obtain a suitable file system path. See
// `test/util/test_path.hpp`.
//
//
// Debugging and the ONLY() macro
// ------------------------------
//
// A simple way of disabling all tests except one called `Foo`, is to
// replace TEST(Foo) with ONLY(Foo) and then recompile and rerun the
// test suite. Note that you can also use filtering by setting the
// environment varible `UNITTEST_FILTER`. See `README.md` for more on
// this.
//
// Another way to debug a particular test, is to copy that test into
// `experiments/testcase.cpp` and then run `sh build.sh
// check-testcase` (or one of its friends) from the command line.


namespace {

void increment(int* i)
{
    ++*i;
}

struct Shared {
    Mutex m_mutex;
    int m_value;

    // 10000 takes less than 0.1 sec
    void increment_10000_times()
    {
        for (int i=0; i<10000; ++i) {
            LockGuard lock(m_mutex);
            ++m_value;
        }
    }

    void increment_10000_times2()
    {
        for (int i=0; i<10000; ++i) {
            LockGuard lock(m_mutex);
            // Create a time window where thread interference can take place. Problem with ++m_value is that it
            // could assemble into 'inc [addr]' which has very tiny gap
            double f = m_value;
            f += 1.;
            m_value = int(f);
        }
    }

};

struct SharedWithEmulated {
    InterprocessMutex m_mutex;
    InterprocessMutex::SharedPart m_shared_part;
    int m_value;

    SharedWithEmulated(std::string name) { m_mutex.set_shared_part(m_shared_part, name, "0"); }
    ~SharedWithEmulated() { m_mutex.release_shared_part(); }

    // 10000 takes less than 0.1 sec
    void increment_10000_times()
    {
        for (int i=0; i<10000; ++i) {
            std::lock_guard<InterprocessMutex> lock(m_mutex);
            ++m_value;
        }
    }

    void increment_10000_times2()
    {
        for (int i=0; i<10000; ++i) {
            std::lock_guard<InterprocessMutex> lock(m_mutex);
            // Create a time window where thread interference can take place. Problem with ++m_value is that it
            // could assemble into 'inc [addr]' which has very tiny gap
            double f = m_value;
            f += 1.;
            m_value = int(f);
        }
    }

};

struct Robust {
    RobustMutex m_mutex;
    bool m_recover_called;

    void simulate_death()
    {
        m_mutex.lock(std::bind(&Robust::recover, this));
        // Do not unlock
    }

    void simulate_death_during_recovery()
    {
        bool no_thread_has_died = m_mutex.low_level_lock();
        if (!no_thread_has_died)
            m_recover_called = true;
        // Do not unlock
    }

    void recover()
    {
        m_recover_called = true;
    }

    void recover_throw()
    {
        m_recover_called = true;
        throw RobustMutex::NotRecoverable();
    }
};


class QueueMonitor {
public:
    QueueMonitor(): m_closed(false)
    {
    }

    bool get(int& value)
    {
        LockGuard lock(m_mutex);
        for (;;) {
            if (!m_queue.empty())
                break;
            if (m_closed)
                return false;
            m_nonempty_or_closed.wait(lock); // Wait for producer
        }
        bool was_full = m_queue.size() == max_queue_size;
        value = m_queue.front();
        m_queue.pop();
        if (was_full)
            m_nonfull.notify_all(); // Resume a waiting producer
        return true;
    }

    void put(int value)
    {
        LockGuard lock(m_mutex);
        while (m_queue.size() == max_queue_size)
            m_nonfull.wait(lock); // Wait for consumer
        bool was_empty = m_queue.empty();
        m_queue.push(value);
        if (was_empty)
            m_nonempty_or_closed.notify_all(); // Resume a waiting consumer
    }

    void close()
    {
        LockGuard lock(m_mutex);
        m_closed = true;
        m_nonempty_or_closed.notify_all(); // Resume all waiting consumers
    }

private:
    Mutex m_mutex;
    CondVar m_nonempty_or_closed, m_nonfull;
    std::queue<int> m_queue;
    bool m_closed;

    static const unsigned max_queue_size = 8;
};

void producer_thread(QueueMonitor* queue, int value)
{
    for (int i=0; i<1000; ++i) {
        queue->put(value);
    }
}

void consumer_thread(QueueMonitor* queue, int* consumed_counts)
{
    for (;;) {
        int value = 0;
        bool closed = !queue->get(value);
        if (closed)
            return;
        ++consumed_counts[value];
    }
}


class bowl_of_stones_semaphore {
public:
    bowl_of_stones_semaphore(int initial_number_of_stones = 0):
        m_num_stones(initial_number_of_stones)
    {
    }
    void get_stone(int num_to_get)
    {
        LockGuard lock(m_mutex);
        while (m_num_stones < num_to_get)
            m_cond_var.wait(lock);
        m_num_stones -= num_to_get;
    }
    void add_stone()
    {
        LockGuard lock(m_mutex);
        ++m_num_stones;
        m_cond_var.notify_all();
    }
private:
    Mutex m_mutex;
    int m_num_stones;
    CondVar m_cond_var;
};



} // anonymous namespace



TEST(Thread_Join)
{
    int i = 0;
    Thread thread(std::bind(&increment, &i));
    CHECK(thread.joinable());
    thread.join();
    CHECK(!thread.joinable());
    CHECK_EQUAL(1, i);
}


TEST(Thread_Start)
{
    int i = 0;
    Thread thread;
    CHECK(!thread.joinable());
    thread.start(std::bind(&increment, &i));
    CHECK(thread.joinable());
    thread.join();
    CHECK(!thread.joinable());
    CHECK_EQUAL(1, i);
}


TEST(Thread_MutexLock)
{
    Mutex mutex;
    {
        LockGuard lock(mutex);
    }
    {
        LockGuard lock(mutex);
    }
}


TEST(Thread_ProcessSharedMutex)
{
    Mutex mutex((Mutex::process_shared_tag()));
    {
        LockGuard lock(mutex);
    }
    {
        LockGuard lock(mutex);
    }
}


TEST(Thread_CriticalSection)
{
    Shared shared;
    shared.m_value = 0;
    Thread threads[10];
    for (int i = 0; i < 10; ++i)
        threads[i].start(std::bind(&Shared::increment_10000_times, &shared));
    for (int i = 0; i < 10; ++i)
        threads[i].join();
    CHECK_EQUAL(100000, shared.m_value);
}


TEST(Thread_EmulatedMutex_CriticalSection)
{
    TEST_PATH(path);
    SharedWithEmulated shared(path);
    shared.m_value = 0;
    Thread threads[10];
    for (int i = 0; i < 10; ++i)
        threads[i].start(std::bind(&SharedWithEmulated::increment_10000_times, &shared));
    for (int i = 0; i < 10; ++i)
        threads[i].join();
    CHECK_EQUAL(100000, shared.m_value);
}


TEST(Thread_CriticalSection2)
{
    Shared shared;
    shared.m_value = 0;
    Thread threads[10];
    for (int i = 0; i < 10; ++i)
        threads[i].start(std::bind(&Shared::increment_10000_times2, &shared));
    for (int i = 0; i < 10; ++i)
        threads[i].join();
    CHECK_EQUAL(100000, shared.m_value);
}


// Todo. Not supported on Windows in particular? Keywords: winbug
TEST_IF(Thread_RobustMutex, TEST_THREAD_ROBUSTNESS)
{
    // Abort if robust mutexes are not supported on the current
    // platform. Otherwise we would probably get into a dead-lock.
    if (!RobustMutex::is_robust_on_this_platform())
        return;

    Robust robust;

    // Check that lock/unlock cycle works and does not involve recovery
    robust.m_recover_called = false;
    robust.m_mutex.lock(std::bind(&Robust::recover, &robust));
    CHECK(!robust.m_recover_called);
    robust.m_mutex.unlock();
    robust.m_recover_called = false;
    robust.m_mutex.lock(std::bind(&Robust::recover, &robust));
    CHECK(!robust.m_recover_called);
    robust.m_mutex.unlock();

    // Check recovery by simulating a death
    robust.m_recover_called = false;
    {
        Thread thread(std::bind(&Robust::simulate_death, &robust));
        thread.join();
    }
    CHECK(!robust.m_recover_called);
    robust.m_recover_called = false;
    robust.m_mutex.lock(std::bind(&Robust::recover, &robust));
    CHECK(robust.m_recover_called);
    robust.m_mutex.unlock();

    // One more round of recovery
    robust.m_recover_called = false;
    {
        Thread thread(std::bind(&Robust::simulate_death, &robust));
        thread.join();
    }
    CHECK(!robust.m_recover_called);
    robust.m_recover_called = false;
    robust.m_mutex.lock(std::bind(&Robust::recover, &robust));
    CHECK(robust.m_recover_called);
    robust.m_mutex.unlock();

    // Simulate a case where recovery fails or is impossible
    robust.m_recover_called = false;
    {
        Thread thread(std::bind(&Robust::simulate_death, &robust));
        thread.join();
    }
    CHECK(!robust.m_recover_called);
    robust.m_recover_called = false;
    CHECK_THROW(robust.m_mutex.lock(std::bind(&Robust::recover_throw, &robust)),
                RobustMutex::NotRecoverable);
    CHECK(robust.m_recover_called);

    // Check that successive attempts at locking will throw
    robust.m_recover_called = false;
    CHECK_THROW(robust.m_mutex.lock(std::bind(&Robust::recover, &robust)),
                RobustMutex::NotRecoverable);
    CHECK(!robust.m_recover_called);
    robust.m_recover_called = false;
    CHECK_THROW(robust.m_mutex.lock(std::bind(&Robust::recover, &robust)),
                RobustMutex::NotRecoverable);
    CHECK(!robust.m_recover_called);
}


TEST_IF(Thread_DeathDuringRecovery, TEST_THREAD_ROBUSTNESS)
{
    // Abort if robust mutexes are not supported on the current
    // platform. Otherwise we would probably get into a dead-lock.
    if (!RobustMutex::is_robust_on_this_platform())
        return;

    // This test checks that death during recovery causes a robust
    // mutex to stay in the 'inconsistent' state.

    Robust robust;

    // Bring the mutex into the 'inconsistent' state
    robust.m_recover_called = false;
    {
        Thread thread(std::bind(&Robust::simulate_death, &robust));
        thread.join();
    }
    CHECK(!robust.m_recover_called);

    // Die while recovering
    robust.m_recover_called = false;
    {
        Thread thread(std::bind(&Robust::simulate_death_during_recovery, &robust));
        thread.join();
    }
    CHECK(robust.m_recover_called);

    // The mutex is still in the 'inconsistent' state if another
    // attempt at locking it calls the recovery function
    robust.m_recover_called = false;
    robust.m_mutex.lock(std::bind(&Robust::recover, &robust));
    CHECK(robust.m_recover_called);
    robust.m_mutex.unlock();

    // Now that the mutex is fully recovered, we should be able to
    // carry out a regular round of lock/unlock
    robust.m_recover_called = false;
    robust.m_mutex.lock(std::bind(&Robust::recover, &robust));
    CHECK(!robust.m_recover_called);
    robust.m_mutex.unlock();

    // Try a double death during recovery
    robust.m_recover_called = false;
    {
        Thread thread(std::bind(&Robust::simulate_death, &robust));
        thread.join();
    }
    CHECK(!robust.m_recover_called);
    robust.m_recover_called = false;
    {
        Thread thread(std::bind(&Robust::simulate_death_during_recovery, &robust));
        thread.join();
    }
    CHECK(robust.m_recover_called);
    robust.m_recover_called = false;
    {
        Thread thread(std::bind(&Robust::simulate_death_during_recovery, &robust));
        thread.join();
    }
    CHECK(robust.m_recover_called);
    robust.m_recover_called = false;
    robust.m_mutex.lock(std::bind(&Robust::recover, &robust));
    CHECK(robust.m_recover_called);
    robust.m_mutex.unlock();
    robust.m_recover_called = false;
    robust.m_mutex.lock(std::bind(&Robust::recover, &robust));
    CHECK(!robust.m_recover_called);
    robust.m_mutex.unlock();
}


TEST(Thread_CondVar)
{
    QueueMonitor queue;
    const int num_producers = 32;
    const int num_consumers = 32;
    Thread producers[num_producers], consumers[num_consumers];
    int consumed_counts[num_consumers][num_producers];
    memset(consumed_counts, 0, sizeof consumed_counts);

    for (int i = 0; i < num_producers; ++i)
        producers[i].start(std::bind(&producer_thread, &queue, i));
    for (int i = 0; i < num_consumers; ++i)
        consumers[i].start(std::bind(&consumer_thread, &queue, &consumed_counts[i][0]));
    for (int i = 0; i < num_producers; ++i)
        producers[i].join();
    queue.close(); // Stop consumers when queue is empty
    for (int i = 0; i < num_consumers; ++i)
        consumers[i].join();

    for (int i = 0; i < num_producers; ++i) {
        int n = 0;
        for (int j = 0; j < num_consumers; ++j)
            n += consumed_counts[j][i];
        CHECK_EQUAL(1000, n);
    }
}

#ifndef _WIN32 // interprocess condvars not suported in Windows yet

// Detect and flag trivial implementations of condvars.
namespace {

void signaller(int* signals, InterprocessMutex* mutex, InterprocessCondVar* cv)
{
    millisleep(1000);
    *signals = 1;
    {
        std::lock_guard<InterprocessMutex> l(*mutex);
        // wakeup any waiters
        cv->notify_all();
    }
    // exit scope to allow waiters to get lock
    millisleep(1000);
    *signals = 2;
    {
        std::lock_guard<InterprocessMutex> l(*mutex);
        // wakeup any waiters, 2nd time
        cv->notify_all();
    }
    millisleep(1000);
    *signals = 3;
    {
        std::lock_guard<InterprocessMutex> l(*mutex);
        // wakeup any waiters, 2nd time
        cv->notify_all();
    }
    millisleep(1000);
    *signals = 4;
}

void wakeup_signaller(int* signal_state, InterprocessMutex* mutex, InterprocessCondVar* cv)
{
    millisleep(1000);
    *signal_state = 2;
    std::lock_guard<InterprocessMutex> l(*mutex);
    cv->notify_all();
}


void waiter_with_count(bowl_of_stones_semaphore* feedback, int* wait_counter, 
                       InterprocessMutex* mutex, InterprocessCondVar* cv)
{
    std::lock_guard<InterprocessMutex> l(*mutex);
    ++ *wait_counter;
    feedback->add_stone();
    cv->wait(*mutex, nullptr);
    -- *wait_counter;
    feedback->add_stone();
}



void waiter(InterprocessMutex* mutex, InterprocessCondVar* cv)
{
    std::lock_guard<InterprocessMutex> l(*mutex);
    cv->wait(*mutex, nullptr);
}

}

// Verify, that a wait on a condition variable actually waits
// - this test relies on assumptions about scheduling, which
//   may not hold on a heavily loaded system.
NONCONCURRENT_TEST(Thread_CondvarWaits)
{
    int signals = 0;
    InterprocessMutex mutex;
    InterprocessMutex::SharedPart mutex_part;
    InterprocessCondVar changed;
    InterprocessCondVar::SharedPart condvar_part;
    TEST_PATH(path);
    mutex.set_shared_part(mutex_part, path, "");
    changed.set_shared_part(condvar_part, path, "");
    changed.init_shared_part(condvar_part);
    Thread signal_thread;
    signals = 0;
    signal_thread.start(std::bind(signaller, &signals, &mutex, &changed));
    {
        std::lock_guard<InterprocessMutex> l(mutex);
        changed.wait(mutex, nullptr);
        CHECK_EQUAL(signals, 1);
        changed.wait(mutex, nullptr);
        CHECK_EQUAL(signals, 2);
        changed.wait(mutex, nullptr);
        CHECK_EQUAL(signals, 3);
    }
    signal_thread.join();
    changed.release_shared_part();
    mutex.release_shared_part();
}

// Verify that a condition variable looses its signal if no one
// is waiting on it
NONCONCURRENT_TEST(Thread_CondvarIsStateless)
{
    int signal_state = 0;
    InterprocessMutex mutex;
    InterprocessMutex::SharedPart mutex_part;
    InterprocessCondVar changed;
    InterprocessCondVar::SharedPart condvar_part;
    InterprocessCondVar::init_shared_part(condvar_part);
    TEST_PATH(path);
    mutex.set_shared_part(mutex_part, path, "");
    changed.set_shared_part(condvar_part, path, "");
    Thread signal_thread;
    signal_state = 1;
    // send some signals:
    {
        std::lock_guard<InterprocessMutex> l(mutex);
        for (int i=0; i<10; ++i)
            changed.notify_all();
    }
    // spawn a thread which will later do one more signal in order
    // to wake us up.
    signal_thread.start(std::bind(wakeup_signaller, &signal_state, &mutex, &changed));
    // Wait for a signal - the signals sent above should be lost, so
    // that this wait will actually wait for the thread to signal.
    {
        std::lock_guard<InterprocessMutex> l(mutex);
        changed.wait(mutex,0);
        CHECK_EQUAL(signal_state, 2);
    }
    signal_thread.join();
    changed.release_shared_part();
    mutex.release_shared_part();
}


// this test hangs, if timeout doesn't work.
NONCONCURRENT_TEST(Thread_CondvarTimeout)
{
    InterprocessMutex mutex;
    InterprocessMutex::SharedPart mutex_part;
    InterprocessCondVar changed;
    InterprocessCondVar::SharedPart condvar_part;
    InterprocessCondVar::init_shared_part(condvar_part);
    TEST_PATH(path);
    mutex.set_shared_part(mutex_part, path, "");
    changed.set_shared_part(condvar_part, path, "");
    struct timespec time;
    time.tv_sec = 0;
    time.tv_nsec = 100000000; // 0.1 sec
    {
        std::lock_guard<InterprocessMutex> l(mutex);
        for (int i=0; i<5; ++i)
            changed.wait(mutex, &time);
    }
    changed.release_shared_part();
    mutex.release_shared_part();
}


// test that notify_all will wake up all waiting threads, if there
// are many waiters:
NONCONCURRENT_TEST(Thread_CondvarNotifyAllWakeup)
{
    InterprocessMutex mutex;
    InterprocessMutex::SharedPart mutex_part;
    InterprocessCondVar changed;
    InterprocessCondVar::SharedPart condvar_part;
    InterprocessCondVar::init_shared_part(condvar_part);
    TEST_PATH(path);
    mutex.set_shared_part(mutex_part, path, "");
    changed.set_shared_part(condvar_part, path, "");
    const int num_waiters = 10;
    Thread waiters[num_waiters];
    for (int i=0; i<num_waiters; ++i) {
        waiters[i].start(std::bind(waiter, &mutex, &changed));
    }
    millisleep(1000); // allow time for all waiters to wait
    changed.notify_all();
    for (int i=0; i<num_waiters; ++i) {
        waiters[i].join();
    }
    changed.release_shared_part();
    mutex.release_shared_part();
}


// FIXME: Disabled because of sporadic failures on Android, which makes CI results non-deterministic
// Reenabled again but marked as non-concurrent

// test that notify will wake up only a single thread, even if there
// are many waiters:
NONCONCURRENT_TEST(Thread_CondvarNotifyWakeup)
{
    int wait_counter = 0;
    InterprocessMutex mutex;
    InterprocessMutex::SharedPart mutex_part;
    InterprocessCondVar changed;
    InterprocessCondVar::SharedPart condvar_part;
    InterprocessCondVar::init_shared_part(condvar_part);
    bowl_of_stones_semaphore feedback(0);
    SHARED_GROUP_TEST_PATH(path);
    mutex.set_shared_part(mutex_part, path, "");
    changed.set_shared_part(condvar_part, path, "");
    const int num_waiters = 10;
    Thread waiters[num_waiters];
    for (int i=0; i<num_waiters; ++i) {
        waiters[i].start(std::bind(waiter_with_count, &feedback, &wait_counter, &mutex, &changed));
    }
    feedback.get_stone(num_waiters);
    CHECK_EQUAL(wait_counter, num_waiters);
    changed.notify();
    feedback.get_stone(1);
    CHECK_EQUAL(wait_counter, num_waiters-1);
    changed.notify();
    feedback.get_stone(1);
    CHECK_EQUAL(wait_counter, num_waiters-2);
    changed.notify_all();
    for (int i=0; i<num_waiters; ++i) {
        waiters[i].join();
    }
    changed.release_shared_part();
    mutex.release_shared_part();
}


#endif // _WIN32

#endif // TEST_THREAD
