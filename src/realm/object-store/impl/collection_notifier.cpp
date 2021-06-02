////////////////////////////////////////////////////////////////////////////
//
// Copyright 2016 Realm Inc.
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

#include <realm/object-store/impl/collection_notifier.hpp>

#include <realm/object-store/impl/realm_coordinator.hpp>
#include <realm/object-store/shared_realm.hpp>

#include <realm/db.hpp>
#include <realm/list.hpp>

using namespace realm;
using namespace realm::_impl;

bool CollectionNotifier::any_related_table_was_modified(TransactionChangeInfo const& info) const noexcept
{
    // Check if any of the tables accessible from the root table were
    // actually modified. This can be false if there were only insertions, or
    // deletions which were not linked to by any row in the linking table
    auto table_modified = [&](auto& tbl) {
        auto it = info.tables.find(tbl.table_key.value);
        return it != info.tables.end() && (!it->second.modifications_empty() || !it->second.insertions_empty());
    };
    return any_of(begin(m_related_tables), end(m_related_tables), table_modified);
}

std::function<bool(ObjectChangeSet::ObjectKeyType)>
CollectionNotifier::get_modification_checker(TransactionChangeInfo const& info, ConstTableRef root_table)
{
    if (info.schema_changed)
        set_table(root_table);

    if (!any_related_table_was_modified(info)) {
        return [](ObjectChangeSet::ObjectKeyType) {
            return false;
        };
    }

    // If the table in question has no outgoing links it will be the only entry in `m_related_tables`.
    // In this case we do not need a `DeepChangeChecker` and check the modifications using the
    // `ObjectChangeSet` within the `TransactionChangeInfo` for this table directly.
    if (m_related_tables.size() == 1 && !all_callbacks_filtered()) {
        auto root_table_key = m_related_tables[0].table_key;
        auto& object_change_set = info.tables.find(root_table_key.value)->second;
        return [&](ObjectChangeSet::ObjectKeyType object_key) {
            return object_change_set.modifications_contains(object_key, {});
        };
    }

    if (all_callbacks_filtered()) {
        return KeyPathChangeChecker(info, *root_table, m_related_tables, m_key_path_arrays);
    }
    else if (any_callbacks_filtered()) {
        // In case we have some callbacks, we need to combine the unfiltered `DeepChangeChecker` with
        // the filtered `KeyPathChangeChecker` to make sure we send all expected notifications.
        KeyPathChangeChecker kpc(info, *root_table, m_related_tables, m_key_path_arrays);
        DeepChangeChecker dc(info, *root_table, m_related_tables, m_key_path_arrays);
        return [kpc = std::move(kpc), dc = std::move(dc)](ObjectChangeSet::ObjectKeyType object_key) mutable {
            return kpc(object_key) || dc(object_key);
        };
    }

    return DeepChangeChecker(info, *root_table, m_related_tables, m_key_path_arrays);
}

std::function<std::vector<int64_t>(ObjectChangeSet::ObjectKeyType)>
CollectionNotifier::get_object_modification_checker(TransactionChangeInfo const& info, ConstTableRef root_table)
{
    return ObjectChangeChecker(info, *root_table, m_related_tables, m_key_path_arrays);
}

void CollectionNotifier::recalculate_key_path_arrays()
{
    m_key_path_arrays = {};
    for (auto&& callback : m_callbacks) {
        m_key_path_arrays.push_back(callback.key_path_array);
    }
}

bool CollectionNotifier::any_callbacks_filtered() const noexcept
{
    return any_of(begin(m_callbacks), end(m_callbacks), [](const auto& callback) {
        return callback.key_path_array.size() > 0;
    });
}


bool CollectionNotifier::all_callbacks_filtered() const noexcept
{
    return all_of(begin(m_callbacks), end(m_callbacks), [](const auto& callback) {
        return callback.key_path_array.size() > 0;
    });
}

CollectionNotifier::CollectionNotifier(std::shared_ptr<Realm> realm)
    : m_realm(std::move(realm))
    , m_sg_version(Realm::Internal::get_transaction(*m_realm).get_version_of_current_transaction())
{
}

CollectionNotifier::~CollectionNotifier()
{
    // Need to do this explicitly to ensure m_realm is destroyed with the mutex
    // held to avoid potential double-deletion
    unregister();
}

void CollectionNotifier::release_data() noexcept
{
    m_sg = nullptr;
}

uint64_t CollectionNotifier::add_callback(CollectionChangeCallback callback, KeyPathArray key_path_array)
{
    m_realm->verify_thread();

    util::CheckedLockGuard lock(m_callback_mutex);
    auto token = m_next_token++;
    m_callbacks.push_back({std::move(callback), {}, {}, std::move(key_path_array), token, false, false});
    m_did_modify_callbacks = true;
    if (m_callback_index == npos) { // Don't need to wake up if we're already sending notifications
        Realm::Internal::get_coordinator(*m_realm).wake_up_notifier_worker();
        m_have_callbacks = true;
    }
    return token;
}

void CollectionNotifier::remove_callback(uint64_t token)
{
    // the callback needs to be destroyed after releasing the lock as destroying
    // it could cause user code to be called
    NotificationCallback old;
    {
        util::CheckedLockGuard lock(m_callback_mutex);
        auto it = find_callback(token);
        if (it == end(m_callbacks)) {
            return;
        }

        size_t idx = distance(begin(m_callbacks), it);
        if (m_callback_index != npos) {
            if (m_callback_index >= idx)
                --m_callback_index;
        }
        --m_callback_count;

        old = std::move(*it);
        m_callbacks.erase(it);
        m_did_modify_callbacks = true;

        m_have_callbacks = !m_callbacks.empty();
    }
}

void CollectionNotifier::suppress_next_notification(uint64_t token)
{
    {
        std::lock_guard<std::mutex> lock(m_realm_mutex);
        REALM_ASSERT(m_realm);
        m_realm->verify_thread();
        m_realm->verify_in_write();
    }

    util::CheckedLockGuard lock(m_callback_mutex);
    auto it = find_callback(token);
    if (it != end(m_callbacks)) {
        // We're inside a write on this collection's Realm, so the callback
        // should have already been called and there are no versions after
        // this one yet
        REALM_ASSERT(it->changes_to_deliver.empty());
        REALM_ASSERT(it->accumulated_changes.empty());
        it->skip_next = true;
    }
}

std::vector<NotificationCallback>::iterator CollectionNotifier::find_callback(uint64_t token)
{
    REALM_ASSERT(m_error || m_callbacks.size() > 0);

    auto it = std::find_if(begin(m_callbacks), end(m_callbacks), [=](const auto& c) {
        return c.token == token;
    });
    // We should only fail to find the callback if it was removed due to an error
    REALM_ASSERT(m_error || it != end(m_callbacks));
    return it;
}

void CollectionNotifier::unregister() noexcept
{
    std::lock_guard<std::mutex> lock(m_realm_mutex);
    m_realm = nullptr;
}

bool CollectionNotifier::is_alive() const noexcept
{
    std::lock_guard<std::mutex> lock(m_realm_mutex);
    return m_realm != nullptr;
}

std::unique_lock<std::mutex> CollectionNotifier::lock_target()
{
    return std::unique_lock<std::mutex>{m_realm_mutex};
}

void CollectionNotifier::set_table(ConstTableRef table)
{
    m_related_tables.clear();
    util::CheckedLockGuard lock(m_callback_mutex);
    recalculate_key_path_arrays();
    DeepChangeChecker::find_filtered_related_tables(m_related_tables, *table, m_key_path_arrays);
}

void CollectionNotifier::add_required_change_info(TransactionChangeInfo& info)
{
    if (!do_add_required_change_info(info) || m_related_tables.empty()) {
        return;
    }

    // Create an entry in the `TransactionChangeInfo` for every table in `m_related_tables`.
    info.tables.reserve(m_related_tables.size());
    for (auto& tbl : m_related_tables)
        info.tables[tbl.table_key.value];
}

void CollectionNotifier::update_related_tables(Table const& table)
{
    m_related_tables.clear();
    recalculate_key_path_arrays();
    DeepChangeChecker::find_filtered_related_tables(m_related_tables, table, m_key_path_arrays);
    // We deactivate the `m_did_modify_callbacks` toggle to make sure the recalculation is only done when
    // necessary.
    m_did_modify_callbacks = false;
}

void CollectionNotifier::prepare_handover()
{
    REALM_ASSERT(m_sg);
    m_sg_version = m_sg->get_version_of_current_transaction();
    do_prepare_handover(*m_sg);
    add_changes(std::move(m_change));
    m_change = {};
    REALM_ASSERT(m_change.empty());
    m_has_run = true;

#ifdef REALM_DEBUG
    util::CheckedLockGuard lock(m_callback_mutex);
    for (auto& callback : m_callbacks)
        REALM_ASSERT(!callback.skip_next);
    m_did_modify_callbacks = true;
#endif
}

void CollectionNotifier::before_advance()
{
    for_each_callback([&](auto& lock, auto& callback) {
        if (callback.changes_to_deliver.empty()) {
            return;
        }

        auto changes = callback.changes_to_deliver;
        // acquire a local reference to the callback so that removing the
        // callback from within it can't result in a dangling pointer
        auto cb = callback.fn;
        lock.unlock_unchecked();
        cb.before(changes);
    });
}

void CollectionNotifier::after_advance()
{
    for_each_callback([&](auto& lock, auto& callback) {
        if (callback.initial_delivered && callback.changes_to_deliver.empty()) {
            return;
        }
        callback.initial_delivered = true;

        auto changes = std::move(callback.changes_to_deliver).finalize();
        callback.changes_to_deliver = {};
        // acquire a local reference to the callback so that removing the
        // callback from within it can't result in a dangling pointer
        auto cb = callback.fn;
        lock.unlock_unchecked();
        cb.after(changes);
    });
}

void CollectionNotifier::deliver_error(std::exception_ptr error)
{
    // Don't complain about double-unregistering callbacks if we sent an error
    // because we're going to remove all the callbacks immediately.
    m_error = true;

    m_callback_count = m_callbacks.size();
    for_each_callback([this, &error](auto& lock, auto& callback) {
        // acquire a local reference to the callback so that removing the
        // callback from within it can't result in a dangling pointer
        auto cb = std::move(callback.fn);
        auto token = callback.token;
        lock.unlock_unchecked();
        cb.error(error);

        // We never want to call the callback again after this, so just remove it
        this->remove_callback(token);
    });
}

bool CollectionNotifier::is_for_realm(Realm& realm) const noexcept
{
    std::lock_guard<std::mutex> lock(m_realm_mutex);
    return m_realm.get() == &realm;
}

bool CollectionNotifier::package_for_delivery()
{
    if (!prepare_to_deliver())
        return false;
    util::CheckedLockGuard lock(m_callback_mutex);
    for (auto& callback : m_callbacks) {
        // changes_to_deliver will normally be empty here. If it's non-empty
        // then that means package_for_delivery() was called multiple times
        // without the notification actually being delivered, which can happen
        // if the Realm was refreshed from within a notification callback.
        callback.changes_to_deliver.merge(std::move(callback.accumulated_changes));
        callback.accumulated_changes = {};
    }
    m_callback_count = m_callbacks.size();
    return true;
}

template <typename Fn>
void CollectionNotifier::for_each_callback(Fn&& fn)
{
    util::CheckedUniqueLock callback_lock(m_callback_mutex);
    REALM_ASSERT_DEBUG(m_callback_count <= m_callbacks.size());
    for (m_callback_index = 0; m_callback_index < m_callback_count; ++m_callback_index) {
        fn(callback_lock, m_callbacks[m_callback_index]);
        if (!callback_lock.owns_lock())
            callback_lock.lock_unchecked();
    }

    m_callback_index = npos;
}

void CollectionNotifier::attach_to(std::shared_ptr<Transaction> sg)
{
    do_attach_to(*sg);
    m_sg = std::move(sg);
}

Transaction& CollectionNotifier::source_shared_group()
{
    return Realm::Internal::get_transaction(*m_realm);
}

void CollectionNotifier::report_collection_root_is_deleted()
{
    if (!m_has_delivered_root_deletion_event) {
        m_change.collection_root_was_deleted = true;
        m_has_delivered_root_deletion_event = true;
    }
}

void CollectionNotifier::add_changes(CollectionChangeBuilder change)
{
    util::CheckedLockGuard lock(m_callback_mutex);
    for (auto& callback : m_callbacks) {
        if (callback.skip_next) {
            // Only the first commit in a batched set of transactions can be
            // skipped, so if we already have some changes something went wrong.
            REALM_ASSERT_DEBUG(callback.accumulated_changes.empty());
            callback.skip_next = false;
        }
        else {
            // Only copy the changeset if there's more callbacks that need it
            if (&callback == &m_callbacks.back())
                callback.accumulated_changes.merge(std::move(change));
            else
                callback.accumulated_changes.merge(CollectionChangeBuilder(change));
        }
    }
}

NotifierPackage::NotifierPackage(std::exception_ptr error, std::vector<std::shared_ptr<CollectionNotifier>> notifiers,
                                 RealmCoordinator* coordinator)
    : m_notifiers(std::move(notifiers))
    , m_coordinator(coordinator)
    , m_error(std::move(error))
{
}

// Clang TSE seems to not like returning a unique_lock from a function
void NotifierPackage::package_and_wait(util::Optional<VersionID::version_type> target_version)
    NO_THREAD_SAFETY_ANALYSIS
{
    if (!m_coordinator || m_error || !*this)
        return;

    auto lock = m_coordinator->wait_for_notifiers([&] {
        if (!target_version)
            return true;
        return std::all_of(begin(m_notifiers), end(m_notifiers), [&](auto const& n) {
            return !n->have_callbacks() || (n->has_run() && n->version().version >= *target_version);
        });
    });

    // Package the notifiers for delivery and remove any which don't have anything to deliver
    auto package = [&](auto& notifier) {
        if (notifier->has_run() && notifier->package_for_delivery()) {
            m_version = notifier->version();
            return false;
        }
        return true;
    };
    m_notifiers.erase(std::remove_if(begin(m_notifiers), end(m_notifiers), package), end(m_notifiers));
    if (m_version && target_version && m_version->version < *target_version) {
        m_notifiers.clear();
        m_version = util::none;
    }
    REALM_ASSERT(m_version || m_notifiers.empty());

    m_coordinator = nullptr;
}

void NotifierPackage::before_advance()
{
    if (m_error)
        return;
    for (auto& notifier : m_notifiers)
        notifier->before_advance();
}

void NotifierPackage::after_advance()
{
    if (m_error) {
        for (auto& notifier : m_notifiers)
            notifier->deliver_error(m_error);
        return;
    }
    for (auto& notifier : m_notifiers)
        notifier->after_advance();
}
