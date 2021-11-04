/*************************************************************************
 *
 * Copyright 2021 Realm, Inc.
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

#include "realm/sync/subscriptions.hpp"

#include "realm/group.hpp"
#include "realm/keys.hpp"
#include "realm/list.hpp"
#include "realm/sort_descriptor.hpp"
#include "realm/table.hpp"
#include "realm/table_view.hpp"

namespace realm::sync {
namespace {

constexpr static std::string_view c_flx_metadata_table("flx_metadata");
constexpr static std::string_view c_flx_subscription_sets_table("flx_subscription_sets");
constexpr static std::string_view c_flx_subscriptions_table("flx_subscriptions");

constexpr static std::string_view c_flx_meta_schema_version_field("schema_version");
constexpr static std::string_view c_flx_sub_sets_state_field("state");
constexpr static std::string_view c_flx_sub_sets_version_field("version");
constexpr static std::string_view c_flx_sub_sets_error_str_field("error");
constexpr static std::string_view c_flx_sub_sets_subscriptions_field("subscriptions");

constexpr static std::string_view c_flx_sub_created_at_field("created_at");
constexpr static std::string_view c_flx_sub_updated_at_field("updated_at");
constexpr static std::string_view c_flx_sub_name_field("name");
constexpr static std::string_view c_flx_sub_object_class_field("object_class");
constexpr static std::string_view c_flx_sub_query_str_field("query");

inline std::string_view to_string_view(StringData in) noexcept
{
    return std::string_view(in.data(), in.size());
}

} // namespace

Subscription::Subscription(const SubscriptionSet* parent, Obj obj)
    : m_parent(parent)
    , m_obj(std::move(obj))
{
}

const SubscriptionStore* Subscription::store() const
{
    return m_parent->m_mgr;
}

Timestamp Subscription::created_at() const
{
    return m_obj.get<Timestamp>(store()->m_sub_keys->created_at);
}

Timestamp Subscription::updated_at() const
{
    return m_obj.get<Timestamp>(store()->m_sub_keys->updated_at);
}

std::string_view Subscription::name() const
{
    return to_string_view(m_obj.get<StringData>(store()->m_sub_keys->name));
}

std::string_view Subscription::object_class_name() const
{
    return to_string_view(m_obj.get<StringData>(store()->m_sub_keys->object_class_name));
}

std::string_view Subscription::query_string() const
{
    return to_string_view(m_obj.get<StringData>(store()->m_sub_keys->query_str));
}

SubscriptionSet::iterator::iterator(const SubscriptionSet* parent, LnkLst::iterator it)
    : m_parent(parent)
    , m_sub_it(std::move(it))
    , m_cur_sub(m_parent->subscription_from_iterator(m_sub_it))
{
}

SubscriptionSet::iterator& SubscriptionSet::iterator::operator++()
{
    ++m_sub_it;
    m_cur_sub = m_parent->subscription_from_iterator(m_sub_it);
    return *this;
}

SubscriptionSet::iterator SubscriptionSet::iterator::operator++(int)
{
    auto ret = *this;
    ++(*this);
    return ret;
}

SubscriptionSet::SubscriptionSet(const SubscriptionStore* mgr, TransactionRef tr, Obj obj)
    : m_mgr(mgr)
    , m_tr(std::move(tr))
    , m_obj(std::move(obj))
    , m_sub_list{}
{
    if (m_obj.is_valid()) {
        m_sub_list = LnkLst(m_obj, m_mgr->m_sub_set_keys->subscriptions);
    }
}

int64_t SubscriptionSet::version() const
{
    if (!m_obj.is_valid()) {
        return 0;
    }
    return m_obj.get_primary_key().get_int();
}

SubscriptionSet::State SubscriptionSet::state() const
{
    if (!m_obj.is_valid()) {
        return State::Uncommitted;
    }
    return static_cast<State>(m_obj.get<int64_t>(m_mgr->m_sub_set_keys->state));
}

StringData SubscriptionSet::error_str() const
{
    if (!m_obj.is_valid()) {
        return StringData{};
    }
    return m_obj.get<StringData>(m_mgr->m_sub_set_keys->error_str);
}

size_t SubscriptionSet::size() const
{
    if (!m_obj.is_valid()) {
        return 0;
    }
    return m_sub_list.size();
}

SubscriptionSet::const_iterator SubscriptionSet::begin() const
{
    return iterator(this, m_sub_list.begin());
}

SubscriptionSet::const_iterator SubscriptionSet::end() const
{
    return iterator(this, m_sub_list.end());
}

SubscriptionSet::const_iterator SubscriptionSet::find(StringData name) const
{
    return std::find_if(begin(), end(), [&](const Subscription& sub) {
        return sub.name() == name;
    });
}

SubscriptionSet::const_iterator SubscriptionSet::find(const Query& query) const
{
    const auto query_desc = query.get_description();
    const auto table_name = Group::table_name_to_class_name(query.get_table()->get_name());
    return std::find_if(begin(), end(), [&](const Subscription& sub) {
        return sub.object_class_name() == table_name && sub.query_string() == query_desc;
    });
}

SubscriptionSet::const_iterator SubscriptionSet::erase(const_iterator it)
{
    m_sub_list.remove_target_row(it.m_sub_it.index());
    return it;
}

void SubscriptionSet::clear()
{
    m_sub_list.remove_all_target_rows();
}

void SubscriptionSet::insert_sub_impl(Timestamp created_at, Timestamp updated_at, StringData name,
                                      StringData object_class_name, StringData query_str)
{
    auto new_sub = m_sub_list.create_and_insert_linked_object(m_sub_list.is_empty() ? 0 : m_sub_list.size() - 1);
    new_sub.set(m_mgr->m_sub_keys->created_at, created_at);
    new_sub.set(m_mgr->m_sub_keys->updated_at, updated_at);
    new_sub.set(m_mgr->m_sub_keys->name, name);
    new_sub.set(m_mgr->m_sub_keys->object_class_name, object_class_name);
    new_sub.set(m_mgr->m_sub_keys->query_str, query_str);
}

Subscription SubscriptionSet::subscription_from_iterator(LnkLst::iterator it) const
{
    if (it == m_sub_list.end()) {
        return Subscription(this, Obj{});
    }
    return Subscription(this, m_sub_list.get_object(it.index()));
}

std::pair<SubscriptionSet::iterator, bool> SubscriptionSet::insert_or_assign_impl(iterator it, StringData name,
                                                                                  StringData object_class_name,
                                                                                  StringData query_str)
{
    auto now = Timestamp{std::chrono::system_clock::now()};
    if (it != end()) {
        it->m_obj.set(m_mgr->m_sub_keys->object_class_name, object_class_name);
        it->m_obj.set(m_mgr->m_sub_keys->query_str, query_str);
        it->m_obj.set(m_mgr->m_sub_keys->updated_at, now);

        return {it, false};
    }

    insert_sub_impl(now, now, name, object_class_name, query_str);

    return {iterator(this, LnkLst::iterator(&m_sub_list, m_sub_list.size() - 1)), true};
}

std::pair<SubscriptionSet::iterator, bool> SubscriptionSet::insert_or_assign(std::string_view name,
                                                                             const Query& query)
{
    auto table_name = Group::table_name_to_class_name(query.get_table()->get_name());
    auto query_str = query.get_description();
    auto it = std::find_if(begin(), end(), [&](const Subscription& sub) {
        return (sub.name() == name);
    });

    return insert_or_assign_impl(it, name, table_name, query_str);
}

std::pair<SubscriptionSet::iterator, bool> SubscriptionSet::insert_or_assign(const Query& query)
{
    auto table_name = Group::table_name_to_class_name(query.get_table()->get_name());
    auto query_str = query.get_description();
    auto it = std::find_if(begin(), end(), [&](const Subscription& sub) {
        return (sub.object_class_name() == table_name && sub.query_string() == query_str);
    });

    std::string_view name;
    return insert_or_assign_impl(it, name, table_name, query_str);
}

void SubscriptionSet::update_state(State new_state, util::Optional<std::string> error_str)
{
    auto old_state = state();
    switch (new_state) {
        case State::Uncommitted:
            throw std::logic_error("cannot set subscription set state to uncommitted");

        case State::Error:
            if (old_state != State::Bootstrapping) {
                throw std::logic_error("subscription set must be in Bootstrapping to update state to error");
            }
            if (!error_str) {
                throw std::logic_error("Must supply an error message when setting a subscription to the error state");
            }

            m_obj.set(m_mgr->m_sub_set_keys->state, static_cast<int64_t>(new_state));
            m_obj.set(m_mgr->m_sub_set_keys->error_str, *error_str);
            break;
        case State::Bootstrapping:
        case State::Pending:
            if (error_str) {
                throw std::logic_error(
                    "Cannot supply an error message for a subscription set when state is not Error");
            }
            m_obj.set(m_mgr->m_sub_set_keys->state, static_cast<int64_t>(new_state));
            break;
        case State::Complete:
            if (error_str) {
                throw std::logic_error(
                    "Cannot supply an error message for a subscription set when state is not Error");
            }
            m_obj.set(m_mgr->m_sub_set_keys->state, static_cast<int64_t>(new_state));
            m_mgr->supercede_prior_to(m_tr, version());
            break;
    }
}

SubscriptionSet SubscriptionSet::make_mutable_copy() const
{
    auto new_tr = m_tr->duplicate();
    if (!new_tr->promote_to_write()) {
        throw std::runtime_error("could not promote flexible sync metadata transaction to writable");
    }

    auto sub_sets = new_tr->get_table(m_mgr->m_sub_set_keys->table);
    auto new_pk = sub_sets->maximum_int(sub_sets->get_primary_key_column()) + 1;

    SubscriptionSet new_set_obj(m_mgr, std::move(new_tr), sub_sets->create_object_with_primary_key(Mixed{new_pk}));
    for (const auto& sub : *this) {
        new_set_obj.insert_sub_impl(sub.created_at(), sub.updated_at(), sub.name(), sub.object_class_name(),
                                    sub.query_string());
    }

    return new_set_obj;
}

void SubscriptionSet::commit()
{
    if (m_tr->get_transact_stage() != DB::transact_Writing) {
        throw std::logic_error("SubscriptionSet is not in a commitable state");
    }
    if (state() == State::Uncommitted) {
        update_state(State::Pending, util::none);
    }
    m_tr->commit_and_continue_as_read();
}

SubscriptionStore::SubscriptionStore(DBRef db)
    : m_db(std::move(db))
    , m_sub_set_keys(std::make_unique<SubscriptionSetKeys>())
    , m_sub_keys(std::make_unique<SubscriptionKeys>())
{
    auto tr = m_db->start_read();

    auto schema_metadata_key = tr->find_table(c_flx_metadata_table);
    auto create_schema_if_needed = [&] {
        tr->promote_to_write();

        if (tr->find_table(c_flx_metadata_table)) {
            return false;
        }

        auto schema_metadata = tr->add_table(c_flx_metadata_table);
        auto version_col = schema_metadata->add_column(type_Int, c_flx_meta_schema_version_field);
        schema_metadata->create_object().set(version_col, int64_t(1));

        auto sub_sets_table =
            tr->add_table_with_primary_key(c_flx_subscription_sets_table, type_Int, c_flx_sub_sets_version_field);
        auto subs_table = tr->add_embedded_table(c_flx_subscriptions_table);
        m_sub_keys->table = subs_table->get_key();
        m_sub_keys->created_at = subs_table->add_column(type_Timestamp, c_flx_sub_created_at_field);
        m_sub_keys->updated_at = subs_table->add_column(type_Timestamp, c_flx_sub_updated_at_field);
        m_sub_keys->name = subs_table->add_column(type_String, c_flx_sub_name_field, true);
        m_sub_keys->object_class_name = subs_table->add_column(type_String, c_flx_sub_object_class_field);
        m_sub_keys->query_str = subs_table->add_column(type_String, c_flx_sub_query_str_field);

        m_sub_set_keys->table = sub_sets_table->get_key();
        m_sub_set_keys->state = sub_sets_table->add_column(type_Int, c_flx_sub_sets_state_field);
        m_sub_set_keys->error_str = sub_sets_table->add_column(type_String, c_flx_sub_sets_error_str_field, true);
        m_sub_set_keys->subscriptions =
            sub_sets_table->add_column_list(*subs_table, c_flx_sub_sets_subscriptions_field);
        tr->commit();
        return true;
    };

    if (schema_metadata_key || !create_schema_if_needed()) {
        auto lookup_and_validate_column = [&](TableRef& table, StringData col_name, DataType col_type) -> ColKey {
            auto ret = table->get_column_key(col_name);
            if (!ret) {
                throw std::runtime_error(util::format("Flexible Sync metadata missing %1 column in %2 table",
                                                      col_name, table->get_name()));
            }
            auto found_col_type = table->get_column_type(ret);
            if (found_col_type != col_type) {
                throw std::runtime_error(util::format(
                    "column %1 in Flexible Sync metadata table %2 is the wrong type", col_name, table->get_name()));
            }
            return ret;
        };

        auto schema_metadata = tr->get_table(schema_metadata_key);
        auto version_obj = schema_metadata->get_object(0);
        auto version = version_obj.get<int64_t>(
            lookup_and_validate_column(schema_metadata, c_flx_meta_schema_version_field, type_Int));
        if (version != 1) {
            throw std::runtime_error("Invalid schema version for flexible sync metadata");
        }

        m_sub_set_keys->table = tr->find_table(c_flx_subscription_sets_table);
        auto sub_sets = tr->get_table(m_sub_set_keys->table);
        m_sub_set_keys->state = lookup_and_validate_column(sub_sets, c_flx_sub_sets_state_field, type_Int);
        m_sub_set_keys->error_str = lookup_and_validate_column(sub_sets, c_flx_sub_sets_error_str_field, type_String);
        m_sub_set_keys->subscriptions =
            lookup_and_validate_column(sub_sets, c_flx_sub_sets_subscriptions_field, type_LinkList);
        if (!m_sub_set_keys->subscriptions) {
            throw std::runtime_error("Flexible Sync metadata missing subscriptions table");
        }

        auto subs = sub_sets->get_opposite_table(m_sub_set_keys->subscriptions);
        if (!subs->is_embedded()) {
            throw std::runtime_error("Flexible Sync subscriptions table should be an embedded object");
        }
        m_sub_keys->table = subs->get_key();
        m_sub_keys->created_at = lookup_and_validate_column(subs, c_flx_sub_created_at_field, type_Timestamp);
        m_sub_keys->updated_at = lookup_and_validate_column(subs, c_flx_sub_updated_at_field, type_Timestamp);
        m_sub_keys->query_str = lookup_and_validate_column(subs, c_flx_sub_query_str_field, type_String);
        m_sub_keys->object_class_name = lookup_and_validate_column(subs, c_flx_sub_object_class_field, type_String);
        m_sub_keys->name = lookup_and_validate_column(subs, c_flx_sub_name_field, type_String);
    }
}

const SubscriptionSet SubscriptionStore::get_latest() const
{
    auto tr = m_db->start_read();
    auto sub_sets = tr->get_table(m_sub_set_keys->table);
    if (sub_sets->is_empty()) {
        return SubscriptionSet(this, std::move(tr), Obj{});
    }
    auto latest_id = sub_sets->maximum_int(sub_sets->get_primary_key_column());
    auto latest_obj = sub_sets->get_object_with_primary_key(Mixed{latest_id});
    return SubscriptionSet(this, std::move(tr), std::move(latest_obj));
}

const SubscriptionSet SubscriptionStore::get_active() const
{
    auto tr = m_db->start_read();
    auto sub_sets = tr->get_table(m_sub_set_keys->table);
    if (sub_sets->is_empty()) {
        return SubscriptionSet(this, std::move(tr), Obj{});
    }

    DescriptorOrdering descriptor_ordering;
    descriptor_ordering.append_sort(SortDescriptor{{{sub_sets->get_primary_key_column()}}, {false}});
    descriptor_ordering.append_limit(LimitDescriptor{1});
    auto res = sub_sets->where()
                   .equal(m_sub_set_keys->state, static_cast<int64_t>(SubscriptionSet::State::Complete))
                   .find_all(descriptor_ordering);

    if (res.is_empty()) {
        tr->close();
        return get_latest();
    }
    return SubscriptionSet(this, std::move(tr), res.get(0));
}

SubscriptionSet SubscriptionStore::get_mutable_by_version(int64_t version_id)
{
    auto tr = m_db->start_write();
    auto sub_sets = tr->get_table(m_sub_set_keys->table);
    auto obj_key = sub_sets->find_primary_key(Mixed{version_id});
    if (obj_key.is_unresolved()) {
        throw std::out_of_range("No subscription set exists for specified version");
    }

    return SubscriptionSet(this, std::move(tr), sub_sets->get_object(obj_key));
}

void SubscriptionStore::supercede_prior_to(TransactionRef tr, int64_t version_id) const
{
    auto sub_sets = tr->get_table(m_sub_set_keys->table);
    Query remove_query(sub_sets);
    remove_query.less(sub_sets->get_primary_key_column(), version_id);
    remove_query.remove();
}

} // namespace realm::sync
