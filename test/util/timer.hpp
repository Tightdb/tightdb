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
#ifndef TIGHTDB_TEST_UTIL_TIMER_HPP
#define TIGHTDB_TEST_UTIL_TIMER_HPP

#include <stdint.h>
#include <cmath>
#include <ostream>

namespace tightdb {
namespace test_util {


class Timer {
public:
    void start() { m_start = get_timer_ticks(); }

    /// Returns elapsed time in seconds (counting only while the
    /// process is running).
    double get_elapsed_time() const
    {
        return calc_elapsed_seconds(get_timer_ticks() - m_start);
    }

    operator double() const { return get_elapsed_time(); }

    template<class Ch, class Tr>
    friend std::basic_ostream<Ch, Tr>& operator<<(std::basic_ostream<Ch, Tr>&, const Timer&);

private:
    uint_fast64_t m_start;

    static uint_fast64_t get_timer_ticks();
    static double calc_elapsed_seconds(uint_fast64_t ticks);
};





// Implementation:

template<class Ch, class Tr>
std::basic_ostream<Ch, Tr>& operator<<(std::basic_ostream<Ch, Tr>& out, const Timer& timer)
{
    double seconds_float = timer;
    uint_fast64_t rounded_minutes = std::floor(seconds_float/60 + 0.5);
    if (60 <= rounded_minutes) {
        // 1h0m -> inf
        uint_fast64_t hours             = rounded_minutes / 60;
        uint_fast64_t remaining_minutes = rounded_minutes - hours*60;
        out << hours             << "h";
        out << remaining_minutes << "m";
    }
    else {
        uint_fast64_t rounded_seconds = std::floor(seconds_float + 0.5);
        if (60 <= rounded_seconds) {
            // 1m0s -> 59m59s
            uint_fast64_t minutes           = rounded_seconds / 60;
            uint_fast64_t remaining_seconds = rounded_seconds - minutes*60;
            out << minutes           << "m";
            out << remaining_seconds << "s";
        }
        else {
            uint_fast64_t rounded_centies = std::floor(seconds_float*100 + 0.5);
            if (100 <= rounded_centies) {
                // 1s -> 59.99s
                uint_fast64_t seconds           = rounded_centies / 100;
                uint_fast64_t remaining_centies = rounded_centies - seconds*100;
                out << seconds;
                if (0 < remaining_centies) {
                    out << ".";
                    if (remaining_centies % 10 == 0) {
                        out << (remaining_centies / 10);
                    }
                    else {
                        out << remaining_centies;
                    }
                }
                out << "s";
            }
            else {
                // 0ms -> 999.9ms
                uint_fast64_t rounded_centi_centies = std::floor(seconds_float*10000 + 0.5);
                uint_fast64_t millis                  = rounded_centi_centies / 10;
                uint_fast64_t remaining_centi_centies = rounded_centi_centies - millis*10;
                out << millis;
                if (0 < remaining_centi_centies) {
                    out << "." << remaining_centi_centies;
                }
                out << "ms";
            }
        }
    }
    return out;
}


} // namespace test_util
} // namespace tightdb

#endif // TIGHTDB_TEST_UTIL_TIMER_HPP
