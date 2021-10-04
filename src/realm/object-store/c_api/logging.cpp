////////////////////////////////////////////////////////////////////////////
//
// Copyright 2021 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or utilied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#include <realm/object-store/c_api/types.hpp>
#include <realm/object-store/c_api/util.hpp>

namespace realm::c_api {
namespace {
using Logger = realm::util::Logger;
static inline realm_log_level_e to_capi(Logger::Level level)
{
    switch (level) {
        case Logger::Level::all:
            return RLM_LOG_LEVEL_ALL;
        case Logger::Level::trace:
            return RLM_LOG_LEVEL_TRACE;
        case Logger::Level::debug:
            return RLM_LOG_LEVEL_DEBUG;
        case Logger::Level::detail:
            return RLM_LOG_LEVEL_DETAIL;
        case Logger::Level::info:
            return RLM_LOG_LEVEL_INFO;
        case Logger::Level::warn:
            return RLM_LOG_LEVEL_WARNING;
        case Logger::Level::error:
            return RLM_LOG_LEVEL_ERROR;
        case Logger::Level::fatal:
            return RLM_LOG_LEVEL_FATAL;
        case Logger::Level::off:
            return RLM_LOG_LEVEL_OFF;
    }
    REALM_TERMINATE("Invalid log level."); // LCOV_EXCL_LINE
}

static inline Logger::Level from_capi(realm_log_level_e level)
{
    switch (level) {
        case RLM_LOG_LEVEL_ALL:
            return Logger::Level::all;
        case RLM_LOG_LEVEL_TRACE:
            return Logger::Level::trace;
        case RLM_LOG_LEVEL_DEBUG:
            return Logger::Level::debug;
        case RLM_LOG_LEVEL_DETAIL:
            return Logger::Level::detail;
        case RLM_LOG_LEVEL_INFO:
            return Logger::Level::info;
        case RLM_LOG_LEVEL_WARNING:
            return Logger::Level::warn;
        case RLM_LOG_LEVEL_ERROR:
            return Logger::Level::error;
        case RLM_LOG_LEVEL_FATAL:
            return Logger::Level::fatal;
        case RLM_LOG_LEVEL_OFF:
            return Logger::Level::off;
    }
    REALM_TERMINATE("Invalid log level."); // LCOV_EXCL_LINE
}

class CLogger : public Logger::LevelThreshold, public Logger {
public:
    CLogger(UserdataPtr userdata, realm_logger_log_func_t log_callback,
            realm_logger_get_threshold_func_t get_threshold)
        : Logger::LevelThreshold()
        , Logger(static_cast<Logger::LevelThreshold&>(*this))
        , m_userdata(std::move(userdata))
        , m_log_callback(log_callback)
        , m_get_threshold(get_threshold)
    {
    }

protected:
    void do_log(Logger::Level level, const std::string& message) final
    {
        m_log_callback(m_userdata.get(), to_capi(level), message.c_str());
    }

    Logger::Level get() const noexcept final
    {
        return from_capi(m_get_threshold(m_userdata.get()));
    }

private:
    UserdataPtr m_userdata;
    realm_logger_log_func_t m_log_callback;
    realm_logger_get_threshold_func_t m_get_threshold;
};
} // namespace
} // namespace realm::c_api

using namespace realm::c_api;

RLM_API realm_logger_t* realm_logger_new(realm_logger_log_func_t log_func,
                                         realm_logger_get_threshold_func_t threshold_func, void* userdata,
                                         realm_free_userdata_func_t free_func)
{
    return new realm_logger_t(new CLogger(UserdataPtr{userdata, free_func}, log_func, threshold_func));
}
