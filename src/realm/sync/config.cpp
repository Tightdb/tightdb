////////////////////////////////////////////////////////////////////////////
//
// Copyright 2015 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#include <realm/sync/config.hpp>
#include <realm/sync/client.hpp>
#include <realm/sync/protocol.hpp>
#include <realm/object-store/util/bson/bson.hpp>

namespace realm {

using ProtocolError = realm::sync::ProtocolError;

bool SyncError::is_client_error() const
{
    return error_code.category() == realm::sync::client_error_category();
}

/// The error is a protocol error, which may either be connection-level or session-level.
bool SyncError::is_connection_level_protocol_error() const
{
    if (error_code.category() != realm::sync::protocol_error_category()) {
        return false;
    }
    return !realm::sync::is_session_level_error(static_cast<ProtocolError>(error_code.value()));
}

/// The error is a connection-level protocol error.
bool SyncError::is_session_level_protocol_error() const
{
    if (error_code.category() != realm::sync::protocol_error_category()) {
        return false;
    }
    return realm::sync::is_session_level_error(static_cast<ProtocolError>(error_code.value()));
}

/// The error indicates a client reset situation.
bool SyncError::is_client_reset_requested() const
{
    if (error_code == make_error_code(sync::Client::Error::auto_client_reset_failure)) {
        return true;
    }
    if (error_code.category() != realm::sync::protocol_error_category()) {
        return false;
    }
    // clang-format off
    // keep this list in sync with SyncSession::handle_error()
    return (error_code == ProtocolError::bad_client_file ||
            error_code == ProtocolError::bad_client_file_ident ||
            error_code == ProtocolError::bad_origin_file_ident ||
            error_code == ProtocolError::bad_server_file_ident ||
            error_code == ProtocolError::bad_server_version ||
            error_code == ProtocolError::client_file_blacklisted ||
            error_code == ProtocolError::client_file_expired ||
            error_code == ProtocolError::diverging_histories ||
            error_code == ProtocolError::invalid_schema_change ||
            error_code == ProtocolError::server_file_deleted ||
            error_code == ProtocolError::user_blacklisted);
    // clang-format on
}

SyncConfig::SyncConfig(std::shared_ptr<SyncUser> user, bson::Bson partition)
    : user(std::move(user))
    , partition_value(partition.to_string())
{
}
SyncConfig::SyncConfig(std::shared_ptr<SyncUser> user, std::string partition)
    : user(std::move(user))
    , partition_value(std::move(partition))
{
}
SyncConfig::SyncConfig(std::shared_ptr<SyncUser> user, const char* partition)
    : user(std::move(user))
    , partition_value(partition)
{
}

} // namespace realm
