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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#include <realm/object-store/impl/deep_change_checker.hpp>
#include <realm/table.hpp>
#include <realm/list.hpp>

using namespace realm;
using namespace realm::_impl;

void DeepChangeChecker::find_all_related_tables(std::vector<RelatedTable>& out, Table const& table,
                                                std::vector<TableKey>& tables_in_filters,
                                                std::vector<KeyPathArray>& key_path_arrays)
{
    auto table_key = table.get_key();
    // If the currently looked at `table` is already part of the `std::vector<RelatedTable>` (possibly
    // due to another path involving it) we do not need to traverse further and can return.
    if (any_of(begin(out), end(out), [&](const auto& tbl) {
            return tbl.table_key == table_key;
        }))
        return;

    // If a filter is set and the table is not part of the filter, it can be skipped.
    if (tables_in_filters.size() != 0) {
        if (none_of(begin(tables_in_filters), end(tables_in_filters), [&](const auto& filtered_table_key) {
                return filtered_table_key == table_key;
            })) {
            return;
        }
    }

    // We need to add this table to `out` before recurring so that the check
    // above works, but we can't store a pointer to the thing being populated
    // because the recursive calls may resize `out`, so instead look it up by
    // index every time.
    size_t out_index = out.size();
    out.push_back({table_key, {}});

    for (auto col_key : table.get_column_keys()) {
        auto type = table.get_column_type(col_key);
        // If a column within the `table` does link to another table it needs to be added to `table`'s
        // links.
        if (type == type_Link || type == type_LinkList) {
            out[out_index].links.push_back({col_key.value, type == type_LinkList});
            // Finally this function needs to be called again to traverse all linked tables using the
            // just found link.
            find_all_related_tables(out, *table.get_link_target(col_key), tables_in_filters, key_path_arrays);
        }
    }
    if (tables_in_filters.size() != 0) {
        // Backlinks can only come into consideration when added via key paths.
        // If there are not `tables_in_filter` we can skip this part.
        table.for_each_backlink_column([&](ColKey column_key) {
            // If this backlink is included in any of the filters we follow the path further.
            out[out_index].links.push_back({column_key.value, false});
            const Table& origin_table = *table.get_link_target(column_key);

            for (auto& key_path_array : key_path_arrays) {
                for (auto& key_path : key_path_array) {
                    auto key_path_size = key_path.size();
                    if (key_path_size > 0) {
                        auto last_key_path_element = key_path[key_path_size - 1];
                        auto last_table_key = last_key_path_element.first;
                        auto last_column_key = last_key_path_element.second;
                        if (last_table_key == table_key && last_column_key == column_key) {
                            auto origin_table_key = origin_table.get_key();
                            auto origin_column_key = table.get_opposite_column(column_key);
                            tables_in_filters.push_back(origin_table_key);
                            std::pair new_pair(origin_table_key, origin_column_key);
                            key_path.push_back(new_pair);
                        }
                    }
                }
            }

            find_all_related_tables(out, origin_table, tables_in_filters, key_path_arrays);
            return false;
        });
    }
}

void DeepChangeChecker::find_filtered_related_tables(std::vector<RelatedTable>& out, Table const& table,
                                                     std::vector<KeyPathArray>& key_path_arrays)
{
    std::vector<TableKey> tables_in_filters = {};
    for (const auto& key_path_array : key_path_arrays) {
        for (const auto& key_path : key_path_array) {
            for (const auto& key_path_element : key_path) {
                tables_in_filters.push_back(key_path_element.first);
            }
        }
    }
    find_all_related_tables(out, table, tables_in_filters, key_path_arrays);
}

DeepChangeChecker::DeepChangeChecker(TransactionChangeInfo const& info, Table const& root_table,
                                     std::vector<RelatedTable> const& related_tables,
                                     const std::vector<KeyPathArray>& key_path_arrays)
    : m_info(info)
    , m_root_table(root_table)
    , m_key_path_arrays(std::move(key_path_arrays))
    , m_root_object_changes([&] {
        auto it = info.tables.find(root_table.get_key().value);
        return it != info.tables.end() ? &it->second : nullptr;
    }())
    , m_related_tables(related_tables)
{
    // If all callbacks do have a filter, every `KeyPathArray` will have entries.
    // In this case we need to check the `ColKey`s and pass the filtered columns
    // to the checker.
    // If at least one `NotificationCallback` does not have a filter we notify on any change.
    // This is signaled by leaving the `m_filtered_columns_in_root_table` and
    // `m_filtered_columns` empty.
    auto all_callbacks_filtered =
        all_of(begin(m_key_path_arrays), end(m_key_path_arrays), [](const auto& key_path_array) {
            return key_path_array.size() > 0;
        });
    if (all_callbacks_filtered) {
        for (const auto& key_path_array : m_key_path_arrays) {
            for (const auto& key_path : key_path_array) {
                if (key_path.size() != 0) {
                    m_filtered_columns_in_root_table.push_back(key_path[0].second);
                }
                for (const auto& key_path_element : key_path) {
                    m_filtered_columns.push_back(key_path_element.second);
                }
            }
        }
    }
}

bool DeepChangeChecker::check_outgoing_links(Table const& table, int64_t object_key,
                                             const std::vector<ColKey>& filtered_columns, size_t depth)
{
    auto table_key = table.get_key();

    // First we create an iterator pointing at the table identified by `table_key` within the `m_related_tables`.
    auto it = std::find_if(begin(m_related_tables), end(m_related_tables), [&](const auto& tbl) {
        return tbl.table_key == table_key;
    });
    // If no iterator could be found the table is not contained in `m_related_tables` and we cannot check any
    // outgoing links.
    if (it == m_related_tables.end())
        return false;
    // Likewise if the table could be found but does not have any (outgoing) links.
    if (it->links.empty())
        return false;

    // Check if we're already checking if the destination of the link is
    // modified, and if not add it to the stack
    auto already_checking = [&](int64_t col) {
        auto end = m_current_path.begin() + depth;
        auto match = std::find_if(m_current_path.begin(), end, [&](const auto& p) {
            return p.object_key == object_key && p.col_key == col;
        });
        if (match != end) {
            for (; match < end; ++match) {
                match->depth_exceeded = true;
            }
            return true;
        }
        m_current_path[depth] = {object_key, col, false};
        return false;
    };

    const Obj obj = table.get_object(ObjKey(object_key));
    auto linked_object_changed = [&](const OutgoingLink& link) {
        if (already_checking(link.col_key))
            return false;

        if (ColKey(link.col_key).get_type() == col_type_BackLink) {
            // Related tables can include tables that are only reachable via backlinks.
            // These tables do not need to be considered when executing this check and
            // therefore be ignored.
            return false;
        }

        if (!link.is_list) {
            ObjKey dst_key = obj.get<ObjKey>(ColKey(link.col_key));
            if (!dst_key) // do not descend into a null or unresolved link
                return false;
            return check_row(*table.get_link_target(ColKey(link.col_key)), dst_key.value, filtered_columns,
                             depth + 1);
        }

        auto& target = *table.get_link_target(ColKey(link.col_key));
        auto lvr = obj.get_linklist(ColKey(link.col_key));
        return std::any_of(lvr.begin(), lvr.end(), [&](const auto& key) {
            return this->check_row(target, key.value, filtered_columns, depth + 1);
        });
    };

    // Check the `links` of all `m_related_tables` and return true if any of them has a `linked_object_changed`.
    return std::any_of(begin(it->links), end(it->links), linked_object_changed);
}

bool DeepChangeChecker::check_row(Table const& table, ObjKeyType object_key,
                                  const std::vector<ColKey>& filtered_columns, size_t depth)
{
    // Arbitrary upper limit on the maximum depth to search
    if (depth >= m_current_path.size()) {
        // Don't mark any of the intermediate rows checked along the path as
        // not modified, as a search starting from them might hit a modification
        for (size_t i = 0; i < m_current_path.size(); ++i)
            m_current_path[i].depth_exceeded = true;
        return false;
    }

    TableKey table_key = table.get_key();

    // If the pair (table_key.value, key) can be found in `m_info.tables` we can
    // end the search and return here.
    if (depth > 0) {
        auto it = m_info.tables.find(table_key.value);
        if (it != m_info.tables.end() && it->second.modifications_contains(object_key, filtered_columns))
            return true;
    }

    // Look up the unmodified objects for the `table_key.value` and check if the
    // `key` can be found within them. If so, we can return without checking the
    // outgoing links.
    auto& not_modified = m_not_modified[table_key.value];
    auto it = not_modified.find(object_key);
    if (it != not_modified.end())
        return false;

    // If both of the above short cuts don't lead to a result we need to check the
    // outgoing links.
    bool ret = check_outgoing_links(table, object_key, filtered_columns, depth);
    if (!ret && (depth == 0 || !m_current_path[depth - 1].depth_exceeded))
        not_modified.insert(object_key);
    return ret;
}

bool DeepChangeChecker::operator()(ObjKeyType key)
{
    // If the root object changed we do not need to iterate over every row since a notification needs to be sent
    // anyway.
    if (m_root_object_changes &&
        m_root_object_changes->modifications_contains(key, m_filtered_columns_in_root_table)) {
        return true;
    }

    return check_row(m_root_table, key, m_filtered_columns, 0);
}

KeyPathChangeChecker::KeyPathChangeChecker(TransactionChangeInfo const& info, Table const& root_table,
                                           std::vector<RelatedTable> const& related_tables,
                                           const std::vector<KeyPathArray>& key_path_arrays)
    : DeepChangeChecker(info, root_table, related_tables, key_path_arrays)
{
}

bool KeyPathChangeChecker::operator()(ObjKeyType object_key)
{
    // If the root object changed we do not need to iterate over every row since a notification needs to be sent
    // anyway.
    if (m_root_object_changes &&
        m_root_object_changes->modifications_contains(object_key, m_filtered_columns_in_root_table)) {
        return true;
    }

    // The `KeyPathChangeChecker` traverses along the given key path arrays and only those to check for changes
    // along them.
    for (const auto& key_path_array : m_key_path_arrays) {
        for (const auto& key_path : key_path_array) {
            auto next_object_key_to_check = object_key;
            for (size_t i = 0; i < key_path.size(); i++) {
                // Check for a change on the current depth.
                auto table_key = key_path[i].first;
                auto column_key = key_path[i].second;
                auto iterator = m_info.tables.find(table_key.value);
                if (iterator != m_info.tables.end() &&
                    (iterator->second.modifications_contains(next_object_key_to_check, {column_key}) ||
                     iterator->second.insertions_contains(next_object_key_to_check))) {
                    return true;
                }

                // Advance one level deeper into the key path if possible.
                if (i < key_path.size() - 1) {
                    auto column_type = column_key.get_type();
                    if (column_type == col_type_Link) {
                        const Obj obj = m_root_table.get_object(ObjKey(next_object_key_to_check));
                        next_object_key_to_check = obj.get<ObjKey>(ColKey(column_key)).value;
                    }
                }
            }
        }
    }

    return false;
}

ObjectChangeChecker::ObjectChangeChecker(TransactionChangeInfo const& info, Table const& root_table,
                                         std::vector<RelatedTable> const& related_tables,
                                         const std::vector<KeyPathArray>& key_path_arrays)
    : DeepChangeChecker(info, root_table, related_tables, key_path_arrays)
{
}

std::vector<int64_t> ObjectChangeChecker::operator()(ObjKeyType object_key)
{
    std::vector<int64_t> changed_columns = {};

    for (const auto& key_path_array : m_key_path_arrays) {
        for (const auto& key_path : key_path_array) {
            check_key_path(changed_columns, key_path, 0, m_root_table, object_key);
        }
    }

    return changed_columns;
}

void ObjectChangeChecker::check_key_path(std::vector<int64_t>& changed_columns, const KeyPath& key_path, size_t depth,
                                         const Table& table, const ObjKeyType& object_key_value)
{
    if (depth >= key_path.size()) {
        // We've reached the end of the key path.
        return;
    }

    auto table_key = key_path[depth].first;
    auto column_key = key_path[depth].second;

    // Check for a change on the current depth level.
    auto iterator = m_info.tables.find(table_key.value);
    if (iterator != m_info.tables.end() && (iterator->second.modifications_contains(object_key_value, {column_key}) ||
                                            iterator->second.insertions_contains(object_key_value))) {
        // If an object linked to the root object was changed we only mark the
        // property of the root objects as changed.
        auto root_column_key = key_path[0].second;
        changed_columns.push_back(root_column_key.value);
    }

    // Advance one level deeper into the key path.
    if (column_key.is_list()) {
        auto target_table = table.get_link_target(column_key);
        auto object = table.get_object(ObjKey(object_key_value));
        auto lvr = object.get_linklist(ColKey(column_key));
        auto sz = lvr.size();
        for (size_t i = 0; i < sz; i++) {
            check_key_path(changed_columns, key_path, depth + 1, *target_table, lvr.get(i).value);
        }
    }
    else {
        auto column_type = column_key.get_type();
        if (column_type == col_type_Link) {
            // A forward link will only have one target object.
            auto target_table = table.get_link_target(column_key);
            auto object = table.get_object(ObjKey(object_key_value));
            auto target_object_key_value = object.get<ObjKey>(ColKey(column_key)).value;
            check_key_path(changed_columns, key_path, depth + 1, *target_table, target_object_key_value);
        }
        else if (column_type == col_type_BackLink) {
            // A backlink can have multiple origin objects. We need to iterate over all of them.
            auto origin_table = table.get_opposite_table(column_key);
            auto origin_column_key = table.get_opposite_column(column_key);
            auto object = table.get_object(ObjKey(object_key_value));
            size_t backlink_count = object.get_backlink_count(*origin_table, origin_column_key);
            for (size_t i = 0; i < backlink_count; i++) {
                auto origin_object_key = object.get_backlink(*origin_table, origin_column_key, i);
                check_key_path(changed_columns, key_path, depth + 1, *origin_table, origin_object_key.value);
            }
        }
    }
}
