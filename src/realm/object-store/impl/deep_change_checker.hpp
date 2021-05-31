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

#ifndef DEEP_CHANGE_CHECKER_HPP
#define DEEP_CHANGE_CHECKER_HPP

#include <realm/object-store/object_changeset.hpp>
#include <realm/object-store/impl/collection_change_builder.hpp>

#include <array>

namespace realm {
class Realm;
class Table;
class Transaction;

using KeyPath = std::vector<std::pair<TableKey, ColKey>>;
using KeyPathArray = std::vector<KeyPath>;

namespace _impl {
class RealmCoordinator;

struct ListChangeInfo {
    TableKey table_key;
    int64_t row_key;
    int64_t col_key;
    CollectionChangeBuilder* changes;
};

// FIXME: this should be in core
using TableKeyType = decltype(TableKey::value);
using ObjKeyType = decltype(ObjKey::value);

// A collection of all changes to all tables which we use to check against when the `DeepChangeChecker`.
struct TransactionChangeInfo {
    std::vector<ListChangeInfo> lists;
    std::unordered_map<TableKeyType, ObjectChangeSet> tables;
    bool track_all;
    bool schema_changed;
};

/**
 * The `DeepChangeChecker` serves two purposes:
 * - Given an initial `Table` and an optional `KeyPathArray` it find all tables related to that initial table.
 *   A `RelatedTable` is a `Table` that can be reached via a link from another `Table`.
 * - The `DeepChangeChecker` also offers a way to check if a specific `ObjKey` was changed.
 */
class DeepChangeChecker {
public:
    struct OutgoingLink {
        int64_t col_key;
        bool is_list;
    };

    /**
     * `RelatedTable` is used to describe the connections of a `Table` to other tables.
     * Tables count as related if they can be reached via a forward link.
     * A table counts as being related to itself.
     */
    struct RelatedTable {
        // The key of the table for which this struct holds all outgoing links.
        TableKey table_key;
        // All outgoing links to the table specified by `table_key`.
        std::vector<OutgoingLink> links;
    };

    DeepChangeChecker(TransactionChangeInfo const& info, Table const& root_table,
                      std::vector<RelatedTable> const& related_tables,
                      const std::vector<KeyPathArray>& key_path_arrays);

    /**
     * Check if the object identified by `object_key` was changed.
     *
     * @param object_key The `ObjKey::value` for the object that is supposed to be checked.
     *
     * @return True if the object was changed, false otherwise.
     */
    bool operator()(int64_t object_key);

    /**
     * Search for related tables within the specified `table`.
     * Related tables are all tables that can be reached via links from the `table`.
     * A table is always related to itself.
     *
     * Example schema:
     * {
     *   {"root_table",
     *       {
     *           {"link", PropertyType::Object | PropertyType::Nullable, "linked_table"},
     *       }
     *   },
     *   {"linked_table",
     *       {
     *           {"value", PropertyType::Int}
     *       }
     *   },
     * }
     *
     * Asking for related tables for `root_table` based on this schema will result in a `std::vector<RelatedTable>`
     * with two entries, one for `root_table` and one for `linked_table`. The function would be called once for
     * each table involved until there are no further links.
     *
     * Likewise a search for related tables starting with `linked_table` would only return this table.
     *
     * Filter:
     * Using a `key_path_array` that only consists of the table key for `root_table` would result
     * in `out` just having this one entry.
     *
     * @param out Return value containing all tables that can be reached from the given `table` including
     *            some additional information about those tables (see `OutgoingLink` in `RelatedTable`).
     * @param table The table that the related tables will be searched for.
     * @param key_path_arrays A collection of all `KeyPathArray`s passed to the `NotificationCallback`s for this
     *                        `CollectionNotifier`.
     */
    static void find_filtered_related_tables(std::vector<RelatedTable>& out, Table const& table,
                                             std::vector<KeyPathArray>& key_path_arrays);

    // This function is only used by `find_filtered_related_tables` internally.
    // It is however used in some tests and therefore exposed here.
    static void find_all_related_tables(std::vector<RelatedTable>& out, Table const& table,
                                        std::vector<TableKey>& tables_in_filters,
                                        std::vector<KeyPathArray>& key_path_arrays);

protected:
    TransactionChangeInfo const& m_info;

    // The `Table` this `DeepChangeChecker` is based on.
    Table const& m_root_table;

    // The `m_key_path_array` contains all columns filtered for. We need this when checking for
    // changes in `operator()` to make sure only columns actually filtered for send notifications.
    std::vector<KeyPathArray> m_key_path_arrays;

    // The `ObjectChangeSet` for `root_table` if it is contained in `m_info`.
    ObjectChangeSet const* const m_root_object_changes;

    // Contains all `ColKey`s that we filter for in the root table.
    std::vector<ColKey> m_filtered_columns_in_root_table;
    std::vector<ColKey> m_filtered_columns;

private:
    std::vector<RelatedTable> const& m_related_tables;

    std::unordered_map<TableKeyType, std::unordered_set<ObjKeyType>> m_not_modified;

    struct Path {
        int64_t object_key;
        int64_t col_key;
        bool depth_exceeded;
    };
    std::array<Path, 4> m_current_path;

    /**
     * Checks if a specific object, identified by it's `ObjKeyType` in a given `Table` was changed.
     *
     * @param table The `Table` that contains the `ObjKeyType` that will be checked.
     * @param object_key The `ObjKeyType` identifying the object to be checked for changes.
     * @param filtered_columns A `std::vector` of all `ColKey`s filtered in any of the `NotificationCallbacks`.
     * @param depth Determines how deep the search will be continued if the change could not be found
     *              on the first level.
     *
     * @return True if the object was changed, false otherwise.
     */
    bool check_row(Table const& table, ObjKeyType object_key, const std::vector<ColKey>& filtered_columns,
                   size_t depth = 0);

    /**
     * Check the `table` within `m_related_tables` for changes in it's outgoing links.
     *
     * @param table The table to check for changed links.
     * @param object_key The key for the object to look for.
     * @param depth The maximum depth that should be considered for this search.
     *
     * @return True if the specified `table` does have linked objects that have been changed.
     *         False if the `table` is not contained in `m_related_tables` or the `table` does not have any
     *         outgoing links at all or the `table` does not have linked objects with changes.
     */
    bool check_outgoing_links(Table const& table, int64_t object_key, const std::vector<ColKey>& filtered_columns,
                              size_t depth = 0);
};

/**
 * The `KeyPathChangeChecker` is a specialised version of `DeepChangeChecker` that offers a checks by traversing and
 * only traversing the given KeyPathArray. With this it supports any depth (as opposed to the maxium depth of 4 on the
 * `DeepChangeChecker`) and backlinks.
 */
class KeyPathChangeChecker : DeepChangeChecker {
public:
    KeyPathChangeChecker(TransactionChangeInfo const& info, Table const& root_table,
                         std::vector<RelatedTable> const& related_tables,
                         const std::vector<KeyPathArray>& key_path_arrays);

    /**
     * Check if the object identified by `object_key` was changed and it is included in the `KeyPathArray` provided
     * when construction this `KeyPathChangeChecker`.
     *
     * @param object_key The `ObjKey::value` for the object that is supposed to be checked.
     *
     * @return True if the object was changed, false otherwise.
     */
    bool operator()(int64_t object_key);
};

/**
 * The `ObjectChangeChecker` is a specialised version of `DeepChangeChecker` that offers a deep change check for
 * objects which is different from the checks done for `Collection`. Like `KeyPathChecker` it is only traversing the
 * given KeyPathArray and has no depth limit.
 *
 * This difference is mainly seen in the fact that for objects we notify about the specific columns that have been
 * changed
 */
class ObjectChangeChecker : DeepChangeChecker {
public:
    ObjectChangeChecker(TransactionChangeInfo const& info, Table const& root_table,
                        std::vector<RelatedTable> const& related_tables,
                        const std::vector<KeyPathArray>& key_path_arrays);

    /**
     * Check if the object identified by `object_key` was changed and it is included in the `KeyPathArray` provided
     * when construction this `KeyPathChangeChecker`.
     *
     * @param object_key The `ObjKey::value` for the object that is supposed to be checked.
     *
     * @return A list of columns changed in the root object.
     */
    std::vector<int64_t> operator()(int64_t object_key);

private:
    /**
     * Traverses down a given `KeyPath` and checks the objects along the way for changes.
     *
     * @param changed_columns The list of `ColKeyType`s that was changed in the root object.
     *                        A key will be added to this list if it turns out to be changed.
     * @param key_path The `KeyPath` used to traverse the given object with.
     * @param depth The current depth in the key_path.
     * @param table The `TableKey` for the current depth.
     * @param object_key_value The `ObjKeyType` that is to be checked for changes.
     */
    void check_key_path(std::vector<int64_t>& changed_columns, const KeyPath& key_path, size_t depth,
                        const Table& table, const ObjKeyType& object_key_value);
};


} // namespace _impl
} // namespace realm

#endif /* DEEP_CHANGE_CHECKER_HPP */
