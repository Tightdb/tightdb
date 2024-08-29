/*************************************************************************
 *
 * Copyright 2024 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#include <realm/exceptions.hpp>

#include <realm/util/demangle.hpp>

#include <Foundation/Foundation.h>

namespace realm {
Status exception_to_status() noexcept
{
    try {
        throw;
    }
    catch (NSException* e) {
        return Status(ErrorCodes::UnknownError, e.reason.UTF8String);
    }
    catch (const Exception& e) {
        return e.to_status();
    }
    catch (const std::exception& e) {
        return Status(ErrorCodes::UnknownError,
                      util::format("Caught std::exception of type %1: %2", util::get_type_name(e), e.what()));
    }
    catch (...) {
        REALM_UNREACHABLE();
    }
}
} // namespace realm
