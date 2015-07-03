#include <cstdio>
#include <algorithm>

#include <realm/array.hpp>
#include <realm/column_basic.hpp>
#include <realm/column_fwd.hpp>
#include <realm/query.hpp>
#include <realm/query_engine.hpp>
#include <realm/descriptor.hpp>

using namespace realm;

Query::Query() : m_view(nullptr)
{
    Create();
//    expression(static_cast<Expression*>(this));
}

Query::Query(Table& table, RowIndexes* tv) : m_table(table.get_table_ref()), m_view(tv)
{
    REALM_ASSERT_DEBUG(m_view == nullptr || m_view->cookie == m_view->cookie_expected);
    Create();
}

Query::Query(const Table& table, const LinkViewRef& lv):
    m_table((const_cast<Table&>(table)).get_table_ref()),
    m_view(lv.get()),
    m_source_link_view(lv)
{
    REALM_ASSERT_DEBUG(m_view == nullptr || m_view->cookie == m_view->cookie_expected);
    Create();
}

Query::Query(const Table& table, RowIndexes* tv) : m_table((const_cast<Table&>(table)).get_table_ref()), m_view(tv)
{
    REALM_ASSERT_DEBUG(m_view == nullptr ||m_view->cookie == m_view->cookie_expected);
    Create();
}

void Query::Create()
{
    // fixme, hack that prevents 'first' from relocating; this limits queries to 16 nested levels of group/end_group
    first.reserve(16);
    update.push_back(0);
    update_override.push_back(0);
    REALM_ASSERT_3(first.capacity(), >, first.size()); // see above fixme
    first.push_back(0);
    pending_not.push_back(false);
    do_delete = true;
    if (m_table)
        update_current_descriptor();
}

// FIXME: Try to remove this
Query::Query(const Query& copy)
{
    m_table = copy.m_table;
    all_nodes = copy.all_nodes;
    update = copy.update;
    update_override = copy.update_override;
    first = copy.first;
    pending_not = copy.pending_not;
    error_code = copy.error_code;
    m_view = copy.m_view;
    m_source_link_view = copy.m_source_link_view;
    copy.do_delete = false;
    do_delete = true;
    m_current_descriptor = copy.m_current_descriptor;
}


// todo, try and remove this constructor. It's currently required for copy-initialization only, and not
// copy-assignment anylonger (which is now just "=").
Query::Query(const Query& copy, const TCopyExpressionTag&) 
{
    // We can call the copyassignment operator even if this destination is uninitialized - the do_delete flag 
    // just needs to be false.
    do_delete = false;
    *this = copy;
}

Query& Query::operator = (const Query& source)
{
    REALM_ASSERT(source.do_delete);

    if (this != &source) {
        // free destination object
        delete_nodes();
        all_nodes.clear();
        first.clear();
        update.clear();
        pending_not.clear();
        update_override.clear();
        subtables.clear();

        m_table = source.m_table;
        m_view = source.m_view;
        m_source_link_view = source.m_source_link_view;

        Create();
        first = source.first;
        std::map<ParentNode*, ParentNode*> node_mapping;
        node_mapping[nullptr] = nullptr;
        std::vector<ParentNode*>::const_iterator i;
        for (i = source.all_nodes.begin(); i != source.all_nodes.end(); ++i) {
            ParentNode* new_node = (*i)->clone();
            all_nodes.push_back(new_node);
            node_mapping[*i] = new_node;
        }
        for (i = all_nodes.begin(); i != all_nodes.end(); ++i) {
            (*i)->translate_pointers(node_mapping);
        }
        for (size_t t = 0; t < first.size(); t++) {
            first[t] = node_mapping[first[t]];
        }



        if (first[0]) {
            ParentNode* node_to_update = first[0];
            while (node_to_update->m_child) {
                node_to_update = node_to_update->m_child;
            }
            update[0] = &node_to_update->m_child;
        }
    }
    return *this;
}

Query::~Query() REALM_NOEXCEPT
{
    delete_nodes();
}

void Query::delete_nodes() REALM_NOEXCEPT
{
    if (do_delete) {
        for (size_t t = 0; t < all_nodes.size(); t++) {
            delete all_nodes[t];
        }
    }
}

/*
// use and_query() instead!
Expression* Query::get_expression() {
    return (static_cast<ExpressionNode*>(first[first.size()-1]))->m_compare;
}
*/
Query& Query::expression(Expression* compare, bool auto_delete)
{
    ParentNode* const p = new ExpressionNode(compare, auto_delete);
    UpdatePointers(p, &p->m_child);
    return *this;
}

// Binary
Query& Query::equal(size_t column_ndx, BinaryData b)
{
    add_condition<Equal>(column_ndx, b);
    return *this;
}
Query& Query::not_equal(size_t column_ndx, BinaryData b)
{
    add_condition<NotEqual>(column_ndx, b);
    return *this;
}
Query& Query::begins_with(size_t column_ndx, BinaryData b)
{
    add_condition<BeginsWith>(column_ndx, b);
    return *this;
}
Query& Query::ends_with(size_t column_ndx, BinaryData b)
{
    add_condition<EndsWith>(column_ndx, b);
    return *this;
}
Query& Query::contains(size_t column_ndx, BinaryData b)
{
    add_condition<Contains>(column_ndx, b);
    return *this;
}


namespace {

struct MakeConditionNode {
    // make() for Node creates a Node* with either a value type
    // or null, and supports both nodes for values that are implicitly
    // nullable (like StringData and BinaryData) and others.
    // Regardless of nullability, it throws a LogicError if trying
    // to query for a value of type T on a column of a different type.

    template <class Node>
    static typename std::enable_if<Node::implicit_nullable, ParentNode*>::type
    make(size_t col_ndx, typename Node::TConditionValue value)
    {
        return new Node(std::move(value), col_ndx);
    }

    template <class Node, class T>
    static typename std::enable_if<
        Node::implicit_nullable
        && !std::is_same<T, typename Node::TConditionValue>::value
        && !std::is_same<T, null>::value
        , ParentNode*>::type
    make(size_t, T)
    {
        throw LogicError{LogicError::type_mismatch};
    }

    template <class Node, class T>
    static typename std::enable_if<
        !Node::implicit_nullable
        && std::is_same<T, null>::value
        , ParentNode*>::type
    make(size_t col_ndx, T value)
    {
        // value is null
        return new Node(value, col_ndx);
    }

    template <class Node, class T>
    static typename std::enable_if<
        !Node::implicit_nullable
        && std::is_same<T, typename Node::TConditionValue>::value
        , ParentNode*>::type
    make(size_t col_ndx, T value)
    {
        return new Node(value, col_ndx);
    }

    template <class Node, class T>
    static typename std::enable_if<
        !Node::implicit_nullable
        && !std::is_same<T, null>::value
        && !std::is_same<T, typename Node::TConditionValue>::value
        , ParentNode*>::type
    make(size_t, T)
    {
        throw LogicError{LogicError::type_mismatch};
    }
};

template <class Cond, class T>
ParentNode* make_condition_node(const Descriptor& descriptor, size_t column_ndx, T value)
{
    DataType type = descriptor.get_column_type(column_ndx);
    bool is_nullable = descriptor.is_nullable(column_ndx);
    switch (type) {
        case type_Int:
        case type_Bool:
        case type_DateTime: {
            if (is_nullable) {
                return MakeConditionNode::make<IntegerNode<ColumnIntNull, Cond>>(column_ndx, value);
            }
            else {
                return MakeConditionNode::make<IntegerNode<Column, Cond>>(column_ndx, value);
            }
        }
        case type_Float: {
            return MakeConditionNode::make<FloatDoubleNode<ColumnFloat, Cond>>(column_ndx, value);
        }
        case type_Double: {
            return MakeConditionNode::make<FloatDoubleNode<ColumnDouble, Cond>>(column_ndx, value);
        }
        case type_String: {
            return MakeConditionNode::make<StringNode<Cond>>(column_ndx, value);
        }
        case type_Binary: {
            return MakeConditionNode::make<BinaryNode<Cond>>(column_ndx, value);
        }
        default: {
            throw LogicError{LogicError::type_mismatch};
        }
    }
}

} // anonymous namespace

void Query::update_current_descriptor()
{
    ConstDescriptorRef desc = m_table->get_descriptor();
    for (size_t i = 0; i < m_subtable_path.size(); ++i) {
        desc = desc->get_subdescriptor(m_subtable_path[i]);
    }
    m_current_descriptor = desc;
}


template <typename TConditionFunction, class T>
Query& Query::add_condition(size_t column_ndx, T value)
{
    ParentNode* const parent = make_condition_node<TConditionFunction>(*m_current_descriptor, column_ndx, value);
    UpdatePointers(parent, &parent->m_child);
    return *this;
}


template <class TColumnType> Query& Query::equal(size_t column_ndx1, size_t column_ndx2)
{
    ParentNode* const p = new TwoColumnsNode<TColumnType, Equal>(column_ndx1, column_ndx2);
    UpdatePointers(p, &p->m_child);
    return *this;
}

// Two column methods, any type
template <class TColumnType> Query& Query::less(size_t column_ndx1, size_t column_ndx2)
{
    ParentNode* const p = new TwoColumnsNode<TColumnType, Less>(column_ndx1, column_ndx2);
    UpdatePointers(p, &p->m_child);
    return *this;
}
template <class TColumnType> Query& Query::less_equal(size_t column_ndx1, size_t column_ndx2)
{
    ParentNode* const p = new TwoColumnsNode<TColumnType, LessEqual>(column_ndx1, column_ndx2);
    UpdatePointers(p, &p->m_child);
    return *this;
}
template <class TColumnType> Query& Query::greater(size_t column_ndx1, size_t column_ndx2)
{
    ParentNode* const p = new TwoColumnsNode<TColumnType, Greater>(column_ndx1, column_ndx2);
    UpdatePointers(p, &p->m_child);
    return *this;
}
template <class TColumnType> Query& Query::greater_equal(size_t column_ndx1, size_t column_ndx2)
{
    ParentNode* const p = new TwoColumnsNode<TColumnType, GreaterEqual>(column_ndx1, column_ndx2);
    UpdatePointers(p, &p->m_child);
    return *this;
}
template <class TColumnType> Query& Query::not_equal(size_t column_ndx1, size_t column_ndx2)
{
    ParentNode* const p = new TwoColumnsNode<TColumnType, NotEqual>(column_ndx1, column_ndx2);
    UpdatePointers(p, &p->m_child);
    return *this;
}

// column vs column, integer
Query& Query::equal_int(size_t column_ndx1, size_t column_ndx2)
{
    return equal<Column>(column_ndx1, column_ndx2);
}

Query& Query::not_equal_int(size_t column_ndx1, size_t column_ndx2)
{
    return not_equal<Column>(column_ndx1, column_ndx2);
}

Query& Query::less_int(size_t column_ndx1, size_t column_ndx2)
{
    return less<Column>(column_ndx1, column_ndx2);
}

Query& Query::greater_equal_int(size_t column_ndx1, size_t column_ndx2)
{
    return greater_equal<Column>(column_ndx1, column_ndx2);
}

Query& Query::less_equal_int(size_t column_ndx1, size_t column_ndx2)
{
    return less_equal<Column>(column_ndx1, column_ndx2);
}

Query& Query::greater_int(size_t column_ndx1, size_t column_ndx2)
{
    return greater<Column>(column_ndx1, column_ndx2);
}


// column vs column, float
Query& Query::not_equal_float(size_t column_ndx1, size_t column_ndx2)
{
    return not_equal<BasicColumn<float>>(column_ndx1, column_ndx2);
}

Query& Query::less_float(size_t column_ndx1, size_t column_ndx2)
{
    return less<BasicColumn<float>>(column_ndx1, column_ndx2);
}

Query& Query::greater_float(size_t column_ndx1, size_t column_ndx2)
{
    return greater<BasicColumn<float>>(column_ndx1, column_ndx2);
}

Query& Query::greater_equal_float(size_t column_ndx1, size_t column_ndx2)
{
    return greater_equal<BasicColumn<float>>(column_ndx1, column_ndx2);
}

Query& Query::less_equal_float(size_t column_ndx1, size_t column_ndx2)
{
    return less_equal<BasicColumn<float>>(column_ndx1, column_ndx2);
}

Query& Query::equal_float(size_t column_ndx1, size_t column_ndx2)
{
    return equal<BasicColumn<float>>(column_ndx1, column_ndx2);
}

// column vs column, double
Query& Query::equal_double(size_t column_ndx1, size_t column_ndx2)
{
    return equal<BasicColumn<double>>(column_ndx1, column_ndx2);
}

Query& Query::less_equal_double(size_t column_ndx1, size_t column_ndx2)
{
    return less_equal<BasicColumn<double>>(column_ndx1, column_ndx2);
}

Query& Query::greater_equal_double(size_t column_ndx1, size_t column_ndx2)
{
    return greater_equal<BasicColumn<double>>(column_ndx1, column_ndx2);
}
Query& Query::greater_double(size_t column_ndx1, size_t column_ndx2)
{
    return greater<BasicColumn<double>>(column_ndx1, column_ndx2);
}
Query& Query::less_double(size_t column_ndx1, size_t column_ndx2)
{
    return less<BasicColumn<double>>(column_ndx1, column_ndx2);
}

Query& Query::not_equal_double(size_t column_ndx1, size_t column_ndx2)
{
    return not_equal<BasicColumn<double>>(column_ndx1, column_ndx2);
}

// null vs column
Query& Query::equal(size_t column_ndx, null)
{
    add_condition<Equal>(column_ndx, null{});
    return *this;
}

Query& Query::not_equal(size_t column_ndx, null n)
{
    add_condition<NotEqual>(column_ndx, n);
    return *this;
}

// int constant vs column (we need those because '1234' is ambiguous, can convert to float/double/int64_t)
Query& Query::equal(size_t column_ndx, int value)
{
    return equal(column_ndx, static_cast<int64_t>(value));
}
Query& Query::not_equal(size_t column_ndx, int value)
{
    return not_equal(column_ndx, static_cast<int64_t>(value));
}
Query& Query::greater(size_t column_ndx, int value)
{
    return greater(column_ndx, static_cast<int64_t>(value));
}
Query& Query::greater_equal(size_t column_ndx, int value)
{
    return greater_equal(column_ndx, static_cast<int64_t>(value));
}
Query& Query::less_equal(size_t column_ndx, int value)
{
    return less_equal(column_ndx, static_cast<int64_t>(value));
}
Query& Query::less(size_t column_ndx, int value)
{
    return less(column_ndx, static_cast<int64_t>(value));
}
Query& Query::between(size_t column_ndx, int from, int to)
{
    return between(column_ndx, static_cast<int64_t>(from), static_cast<int64_t>(to));
}

Query& Query::links_to(size_t origin_column, size_t target_row)
{
    ParentNode* const p = new LinksToNode(origin_column, target_row);
    UpdatePointers(p, &p->m_child);
    return *this;
}

// int64 constant vs column
Query& Query::equal(size_t column_ndx, int64_t value)
{
    add_condition<Equal>(column_ndx, value);
    return *this;
}
Query& Query::not_equal(size_t column_ndx, int64_t value)
{
    add_condition<NotEqual>(column_ndx, value);
    return *this;
}
Query& Query::greater(size_t column_ndx, int64_t value)
{
    add_condition<Greater>(column_ndx, value);
    return *this;
}
Query& Query::greater_equal(size_t column_ndx, int64_t value)
{
    if (value > LLONG_MIN) {
        add_condition<Greater>(column_ndx, value - 1);
    }
    // field >= LLONG_MIN has no effect
    return *this;
}
Query& Query::less_equal(size_t column_ndx, int64_t value)
{
    if (value < LLONG_MAX) {
        add_condition<Less>(column_ndx, value + 1);
    }
    // field <= LLONG_MAX has no effect
    return *this;
}
Query& Query::less(size_t column_ndx, int64_t value)
{
    add_condition<Less>(column_ndx, value);
    return *this;
}
Query& Query::between(size_t column_ndx, int64_t from, int64_t to)
{
    group();
    greater_equal(column_ndx, from);
    less_equal(column_ndx, to);
    end_group();
    return *this;
}
Query& Query::equal(size_t column_ndx, bool value)
{
    add_condition<Equal>(column_ndx, int64_t(value));
    return *this;
}

// ------------- float
Query& Query::equal(size_t column_ndx, float value)
{
    return add_condition<Equal>(column_ndx, value);
}
Query& Query::not_equal(size_t column_ndx, float value)
{
    return add_condition<NotEqual>(column_ndx, value);
}
Query& Query::greater(size_t column_ndx, float value)
{
    return add_condition<Greater>(column_ndx, value);
}
Query& Query::greater_equal(size_t column_ndx, float value)
{
    return add_condition<GreaterEqual>(column_ndx, value);
}
Query& Query::less_equal(size_t column_ndx, float value)
{
    return add_condition<LessEqual>(column_ndx, value);
}
Query& Query::less(size_t column_ndx, float value)
{
    return add_condition<Less>(column_ndx, value);
}
Query& Query::between(size_t column_ndx, float from, float to)
{
    group();
    greater_equal(column_ndx, from);
    less_equal(column_ndx, to);
    end_group();
    return *this;
}


// ------------- double
Query& Query::equal(size_t column_ndx, double value)
{
    return add_condition<Equal>(column_ndx, value);
}
Query& Query::not_equal(size_t column_ndx, double value)
{
    return add_condition<NotEqual>(column_ndx, value);
}
Query& Query::greater(size_t column_ndx, double value)
{
    return add_condition<Greater>(column_ndx, value);
}
Query& Query::greater_equal(size_t column_ndx, double value)
{
    return add_condition<GreaterEqual>(column_ndx, value);
}
Query& Query::less_equal(size_t column_ndx, double value)
{
    return add_condition<LessEqual>(column_ndx, value);
}
Query& Query::less(size_t column_ndx, double value)
{
    return add_condition<Less>(column_ndx, value);
}
Query& Query::between(size_t column_ndx, double from, double to)
{
    group();
    greater_equal(column_ndx, from);
    less_equal(column_ndx, to);
    end_group();
    return *this;
}


// Strings, StringData()

Query& Query::equal(size_t column_ndx, StringData value, bool case_sensitive)
{
    if (case_sensitive)
        add_condition<Equal>(column_ndx, value);
    else
        add_condition<EqualIns>(column_ndx, value);
    return *this;
}
Query& Query::begins_with(size_t column_ndx, StringData value, bool case_sensitive)
{
    if (case_sensitive)
        add_condition<BeginsWith>(column_ndx, value);
    else
        add_condition<BeginsWithIns>(column_ndx, value);
    return *this;
}
Query& Query::ends_with(size_t column_ndx, StringData value, bool case_sensitive)
{
    if (case_sensitive)
        add_condition<EndsWith>(column_ndx, value);
    else
        add_condition<EndsWithIns>(column_ndx, value);
    return *this;
}
Query& Query::contains(size_t column_ndx, StringData value, bool case_sensitive)
{
    if (case_sensitive)
        add_condition<Contains>(column_ndx, value);
    else
        add_condition<ContainsIns>(column_ndx, value);
    return *this;
}
Query& Query::not_equal(size_t column_ndx, StringData value, bool case_sensitive)
{
    if (case_sensitive)
        add_condition<NotEqual>(column_ndx, value);
    else
        add_condition<NotEqualIns>(column_ndx, value);
    return *this;
}


// Aggregates =================================================================================

size_t Query::peek_tableview(size_t tv_index) const
{
    REALM_ASSERT(m_view);
    REALM_ASSERT_DEBUG(m_view->cookie == m_view->cookie_expected);
    REALM_ASSERT_3(tv_index, <, m_view->size());

    size_t tablerow = m_view->m_row_indexes.get(tv_index);

    size_t r;
    if (first.size() > 0 && first[0])
        r = first[0]->find_first(tablerow, tablerow + 1);
    else
        r = tablerow;

    return r;
}

template <Action action, typename T, typename R, class ColType>
    R Query::aggregate(R(ColType::*aggregateMethod)(size_t start, size_t end, size_t limit,
                                                    size_t* return_ndx) const,
                       size_t column_ndx, size_t* resultcount, size_t start, size_t end, size_t limit, 
                       size_t* return_ndx) const
{
    if(limit == 0 || m_table->is_degenerate()) {
        if (resultcount)
            *resultcount = 0;
        return static_cast<R>(0);
    }

    if (end == size_t(-1))
        end = m_view ? m_view->size() : m_table->size();

    const ColType& column =
        m_table->get_column<ColType, ColumnType(ColumnTypeTraits<T, ColType::nullable>::id)>(column_ndx);

    if ((first.size() == 0 || first[0] == 0) && !m_view) {

        // No criteria, so call aggregate METHODS directly on columns
        // - this bypasses the query system and is faster
        // User created query with no criteria; aggregate range
        if (resultcount) {
            *resultcount = limit < (end - start) ? limit : (end - start);
        }
        // direct aggregate on the column
        return (column.*aggregateMethod)(start, end, limit, return_ndx);
    }
    else {

        // Aggregate with criteria - goes through the nodes in the query system
        Init(*m_table);
        QueryState<R> st;
        st.init(action, nullptr, limit);

        SequentialGetter<ColType> source_column(*m_table, column_ndx);

        if (!m_view) {
            aggregate_internal(action, ColumnTypeTraits<T, ColType::nullable>::id, ColType::nullable, first[0], &st, start, end, &source_column);
        }
        else {
            for (size_t t = start; t < end && st.m_match_count < limit; t++) {
                size_t r = peek_tableview(t);
                if (r != not_found)
                    st.template match<action, false>(r, 0, source_column.get_next(m_view->m_row_indexes.get(t)));
            }
        }

        if (resultcount) {
            *resultcount = st.m_match_count;
        }

        if (return_ndx) {
            *return_ndx = st.m_minmax_index;
        }

        return st.m_state;
    }
}

    /**************************************************************************************************************
    *                                                                                                             *
    * Main entry point of a query. Schedules calls to aggregate_local                                             *
    * Return value is the result of the query, or Array pointer for FindAll.                                      *
    *                                                                                                             *
    **************************************************************************************************************/

    void Query::aggregate_internal(Action TAction, DataType TSourceColumn, bool nullable,
                                   ParentNode* pn, QueryStateBase* st,
                                   size_t start, size_t end, SequentialGetterBase* source_column) const
    {
        if (end == not_found)
            end = m_table->size();

        for (size_t c = 0; c < pn->m_children.size(); c++)
            pn->m_children[c]->aggregate_local_prepare(TAction, TSourceColumn, nullable);

        size_t td;

        while (start < end) {
            size_t best = std::distance(pn->m_children.begin(),
                                        std::min_element(pn->m_children.begin(), pn->m_children.end(),
                                                         ParentNode::score_compare()));

            // Find a large amount of local matches in best condition
            td = pn->m_children[best]->m_dT == 0.0 ? end : (start + 1000 > end ? end : start + 1000);

            // Executes start...end range of a query and will stay inside the condition loop of the node it was called
            // on. Can be called on any node; yields same result, but different performance. Returns prematurely if
            // condition of called node has evaluated to true local_matches number of times.
            // Return value is the next row for resuming aggregating (next row that caller must call aggregate_local on)
            start = pn->m_children[best]->aggregate_local(st, start, td, findlocals, source_column);

            // Make remaining conditions compute their m_dD (statistics)
            for (size_t c = 0; c < pn->m_children.size() && start < end; c++) {
                if (c == best)
                    continue;

                // Skip test if there is no way its cost can ever be better than best node's
                double cost = pn->m_children[c]->cost();
                if (pn->m_children[c]->m_dT < cost) {

                    // Limit to bestdist in order not to skip too large parts of index nodes
                    size_t maxD = pn->m_children[c]->m_dT == 0.0 ? end - start : bestdist;
                    td = pn->m_children[c]->m_dT == 0.0 ? end : (start + maxD > end ? end : start + maxD);
                    start = pn->m_children[c]->aggregate_local(st, start, td, probe_matches, source_column);
                }
            }
        }
    }


// Sum

int64_t Query::sum_int(size_t column_ndx, size_t* resultcount, size_t start, size_t end, size_t limit) const
{
    if (m_table->is_nullable(column_ndx)) {
        return aggregate<act_Sum, int64_t>(&ColumnIntNull::sum, column_ndx, resultcount, start, end, limit);
    }
    return aggregate<act_Sum, int64_t>(&Column::sum, column_ndx, resultcount, start, end, limit);
}
double Query::sum_float(size_t column_ndx, size_t* resultcount, size_t start, size_t end, size_t limit) const
{
    return aggregate<act_Sum, float>(&ColumnFloat::sum, column_ndx, resultcount, start, end, limit);
}
double Query::sum_double(size_t column_ndx, size_t* resultcount, size_t start, size_t end, size_t limit) const
{
    return aggregate<act_Sum, double>(&ColumnDouble::sum, column_ndx, resultcount, start, end, limit);
}

// Maximum

int64_t Query::maximum_int(size_t column_ndx, size_t* resultcount, size_t start, size_t end, size_t limit, 
                           size_t* return_ndx) const
{
    if (m_table->is_nullable(column_ndx)) {
        return aggregate<act_Max, int64_t>(&ColumnIntNull::maximum, column_ndx, resultcount, start, end, limit, return_ndx);
    }
    return aggregate<act_Max, int64_t>(&Column::maximum, column_ndx, resultcount, start, end, limit, return_ndx);
}

DateTime Query::maximum_datetime(size_t column_ndx, size_t* resultcount, size_t start, size_t end, 
                                 size_t limit, size_t* return_ndx) const
{
    if (m_table->is_nullable(column_ndx)) {
        return aggregate<act_Max, int64_t>(&ColumnIntNull::maximum, column_ndx, resultcount, start, end, limit, return_ndx);
    }
    return aggregate<act_Max, int64_t>(&Column::maximum, column_ndx, resultcount, start, end, limit, return_ndx);
}

float Query::maximum_float(size_t column_ndx, size_t* resultcount, size_t start, size_t end, 
                           size_t limit, size_t* return_ndx) const
{
    return aggregate<act_Max, float>(&ColumnFloat::maximum, column_ndx, resultcount, start, end, limit, return_ndx);
}
double Query::maximum_double(size_t column_ndx, size_t* resultcount, size_t start, size_t end,
                             size_t limit, size_t* return_ndx) const
{
    return aggregate<act_Max, double>(&ColumnDouble::maximum, column_ndx, resultcount, start, end, limit,
                                      return_ndx);
}


// Minimum

int64_t Query::minimum_int(size_t column_ndx, size_t* resultcount, size_t start, size_t end, 
                           size_t limit, size_t* return_ndx) const
{
    if (m_table->is_nullable(column_ndx)) {
        return aggregate<act_Min, int64_t>(&ColumnIntNull::minimum, column_ndx, resultcount, start, end, limit, return_ndx);
    }
    return aggregate<act_Min, int64_t>(&Column::minimum, column_ndx, resultcount, start, end, limit, return_ndx);
}
float Query::minimum_float(size_t column_ndx, size_t* resultcount, size_t start, size_t end, size_t limit,
                           size_t* return_ndx) const
{
    return aggregate<act_Min, float>(&ColumnFloat::minimum, column_ndx, resultcount, start, end, limit, return_ndx);
}
double Query::minimum_double(size_t column_ndx, size_t* resultcount, size_t start, size_t end, size_t limit, 
                             size_t* return_ndx) const
{
    return aggregate<act_Min, double>(&ColumnDouble::minimum, column_ndx, resultcount, start, end, limit, 
                                      return_ndx);
}

DateTime Query::minimum_datetime(size_t column_ndx, size_t* resultcount, size_t start, size_t end, size_t limit,
                                 size_t* return_ndx) const
{
    if (m_table->is_nullable(column_ndx)) {
        return aggregate<act_Min, int64_t>(&ColumnIntNull::minimum, column_ndx, resultcount, start, end, limit, return_ndx);
    }
    return aggregate<act_Min, int64_t>(&Column::minimum, column_ndx, resultcount, start, end, limit, return_ndx);
}


// Average

template <typename T, bool Nullable>
double Query::average(size_t column_ndx, size_t* resultcount, size_t start, size_t end, size_t limit) const
{
    if(limit == 0 || m_table->is_degenerate()) {
        if (resultcount)
            *resultcount = 0;
        return 0.;
    }

    size_t resultcount2 = 0;
    typedef typename ColumnTypeTraits<T, Nullable>::column_type ColType;
    typedef typename ColumnTypeTraits<T, Nullable>::sum_type SumType;
    const SumType sum1 = aggregate<act_Sum, T>(&ColType::sum, column_ndx, &resultcount2, start, end, limit);
    double avg1 = 0;
    if (resultcount2 != 0)
        avg1 = static_cast<double>(sum1) / resultcount2;
    if (resultcount)
        *resultcount = resultcount2;
    return avg1;
}

double Query::average_int(size_t column_ndx, size_t* resultcount, size_t start, size_t end, size_t limit) const
{
    if (m_table->is_nullable(column_ndx)) {
        return average<int64_t, true>(column_ndx, resultcount, start, end, limit);
    }
    return average<int64_t, false>(column_ndx, resultcount, start, end, limit);
}
double Query::average_float(size_t column_ndx, size_t* resultcount, size_t start, size_t end, size_t limit) const
{
    if (m_table->is_nullable(column_ndx)) {
        return average<float, true>(column_ndx, resultcount, start, end, limit);
    }
    return average<float, false>(column_ndx, resultcount, start, end, limit);
}
double Query::average_double(size_t column_ndx, size_t* resultcount, size_t start, size_t end, size_t limit) const
{
    if (m_table->is_nullable(column_ndx)) {
        return average<double, true>(column_ndx, resultcount, start, end, limit);
    }
    return average<double, false>(column_ndx, resultcount, start, end, limit);
}


// Grouping
Query& Query::group()
{
    update.push_back(0);
    update_override.push_back(0);
    REALM_ASSERT_3(first.capacity(), >, first.size()); // see fixme in ::Create()
    first.push_back(0);
    pending_not.push_back(false);
    return *this;
}
Query& Query::end_group()
{
    if (first.size() < 2) {
        error_code = "Unbalanced group";
        return *this;
    }

    // append first node in current group to surrounding group. If an Or node was met,
    // it will have manipulated first, so that it (the Or node) is the first node in the
    // current group.
    if (update[update.size()-2] != 0)
        *update[update.size()-2] = first[first.size()-1];

    // similarly, if the surrounding group is empty, simply make first node of current group,
    // the first node of the surrounding group.
    if (first[first.size()-2] == 0)
        first[first.size()-2] = first[first.size()-1];

    // the update back link for the surrounding group must be updated to support
    // the linking in of nodes that follows. If the node we are adding to the surrounding
    // context has taken control of the nodes in the inner group, then we set up an
    // update to a field inside it - if not, then we just copy the last update in the
    // current group into the surrounding group. So: the update override is used to override
    // the normal sequential linking in of nodes, producing e.g. the structure used for
    // OrNodes and NotNodes.
    if (update_override[update_override.size()-1] != 0)
        update[update.size() - 2] = update_override[update_override.size()-1];
    else if (update[update.size()-1] != 0)
        update[update.size() - 2] = update[update.size()-1];

    first.pop_back();
    pending_not.pop_back();
    update.pop_back();
    update_override.pop_back();
    HandlePendingNot();
    return *this;
}

// Not creates an implicit group to capture the term that we want to negate.
Query& Query::Not()
{
    NotNode* const p = new NotNode;
    all_nodes.push_back(p);
    if (first[first.size()-1] == nullptr) {
        first[first.size()-1] = p;
    }
    if (update[update.size()-1] != nullptr) {
        *update[update.size()-1] = p;
    }
    group();
    pending_not[pending_not.size()-1] = true;
    // value for update for sub-condition
    update[update.size()-2] = nullptr;
    update[update.size()-1] = &p->m_cond;
    // pending value for update, once the sub-condition ends:
    update_override[update_override.size()-1] = &p->m_child;
    return *this;
}

// And-terms must end by calling HandlePendingNot. This will check if a negation is pending,
// and if so, it will end the implicit group created to hold the term to negate. Note that
// end_group itself will recurse into HandlePendingNot if multiple implicit groups are nested
// within each other.
void Query::HandlePendingNot()
{
    if (pending_not.size() > 1 && pending_not[pending_not.size()-1]) {
        // we are inside group(s) implicitly created to handle a not, so pop it/them:
        // but first, prevent the pop from linking the current node into the surrounding
        // context - the current node is instead hanging of from the previously added NotNode's
        // m_cond field.
        // update[update.size()-1] = 0;
        end_group();
    }
}

Query& Query::Or()
{
    OrNode* o = dynamic_cast<OrNode*>(first.back());
    if (o) {
        if (o->m_cond.back())
            o->m_cond.push_back(0);
    }
    else {
        o = new OrNode(first.back());
        o->m_cond.push_back(0);
        all_nodes.push_back(o);
    }

    first.back() = o;
    update.back() = &o->m_cond.back();
    update_override.back() = &o->m_child;
    return *this;
}

Query& Query::subtable(size_t column)
{
    SubtableNode* const p = new SubtableNode(column);
    UpdatePointers(p, &p->m_child);
    // once subtable conditions have been evaluated, resume evaluation from m_child2
    subtables.push_back(&p->m_child2);
    m_subtable_path.push_back(column);
    update_current_descriptor();
    group();
    return *this;
}

Query& Query::end_subtable()
{
    if (subtables.size() == 0) {
        error_code = "Unbalanced subtable";
        return *this;
    }

    end_group();

    if (update[update.size()-1] != 0)
        update[update.size()-1] = subtables[subtables.size()-1];

    subtables.pop_back();
    m_subtable_path.pop_back();
    update_current_descriptor();
    return *this;
}

// todo, add size_t end? could be useful
size_t Query::find(size_t begin)
{
    if (m_table->is_degenerate())
        return not_found;

    REALM_ASSERT_3(begin, <=, m_table->size());

    Init(*m_table);

    // User created query with no criteria; return first
    if (first.size() == 0 || first[0] == nullptr) {
        if (m_view)
            return m_view->size() == 0 ? not_found : begin;
        else
            return m_table->size() == 0 ? not_found : begin;
    }

    if (m_view) {
        size_t end = m_view->size();
        for (; begin < end; begin++) {
            size_t res = peek_tableview(begin);
            if (res != not_found)
                return begin;
        }
        return not_found;
    }
    else {
        size_t end = m_table->size();
        size_t res = first[0]->find_first(begin, end);
        return (res == end) ? not_found : res;
    }
}

void Query::find_all(TableViewBase& ret, size_t start, size_t end, size_t limit) const
{
    if (limit == 0 || m_table->is_degenerate())
        return;

    REALM_ASSERT_3(start, <=, m_table->size());

    Init(*m_table);

    if (end == size_t(-1))
        end = m_view ? m_view->size() : m_table->size();

    // User created query with no criteria; return everything
    if (first.size() == 0 || first[0] == 0) {
        Column& refs = ret.m_row_indexes;
        size_t end_pos = (limit != size_t(-1)) ? std::min(end, start + limit) : end;

        if (m_view) {
            for (size_t i = start; i < end_pos; ++i)
                refs.add(m_view->m_row_indexes.get(i));
        }
        else {
            for (size_t i = start; i < end_pos; ++i)
                refs.add(i);
        }
        return;
    }

    if (m_view) {
        for (size_t begin = start; begin < end && ret.size() < limit; begin++) {
            size_t res = peek_tableview(begin);
            if (res != not_found)
                ret.m_row_indexes.add(res);
        }
    }
    else {
        QueryState<int64_t> st;
        st.init(act_FindAll, &ret.m_row_indexes, limit);
        aggregate_internal(act_FindAll, ColumnTypeTraits<int64_t, false>::id, false, first[0], &st, start, end, nullptr);
    }
}

TableView Query::find_all(size_t start, size_t end, size_t limit)
{
    TableView ret(*m_table, *this, start, end, limit);
    find_all(ret, start, end, limit);
    return ret;
}


size_t Query::count(size_t start, size_t end, size_t limit) const
{
    if(limit == 0 || m_table->is_degenerate())
        return 0;

    if (end == size_t(-1))
        end = m_view ? m_view->size() : m_table->size();

    if (first.size() == 0 || first[0] == 0) {
        // User created query with no criteria; count all
        return (limit < end - start ? limit : end - start);
    }

    Init(*m_table);
    size_t cnt = 0;

    if (m_view) {
        for (size_t begin = start; begin < end && cnt < limit; begin++) {
            size_t res = peek_tableview(begin);
            if (res != not_found)
                cnt++;
        }
    }
    else {
        QueryState<int64_t> st;
        st.init(act_Count, nullptr, limit);
        aggregate_internal(act_Count, ColumnTypeTraits<int64_t, false>::id, false, first[0], &st, start, end, nullptr);
        cnt = size_t(st.m_state);
    }

    return cnt;
}


// todo, not sure if start, end and limit could be useful for delete.
size_t Query::remove(size_t start, size_t end, size_t limit)
{
    if(limit == 0 || m_table->is_degenerate())
        return 0;

    if (end == not_found)
        end = m_view ? m_view->size() : m_table->size();

    size_t results = 0;

    if (m_view) {
        for (;;) {
            if (start + results == end || results == limit)
                return results;

            Init(*m_table);
            size_t r = peek_tableview(start + results);
            if (r != not_found) {
                m_table->remove(r);
                m_view->m_row_indexes.adjust_ge(m_view->m_row_indexes.get(start + results), -1);
                results++;
            }
            else {
                return results;
            }
        }
    }
    else {
        size_t r = start;
        for (;;) {
            // Every remove invalidates the array cache in the nodes
            // so we have to re-initialize it before searching
            Init(*m_table);

            r = FindInternal(r, end - results);
            if (r == not_found || r == m_table->size() || results == limit)
                break;
            ++results;
            m_table->remove(r);
        }
        return results;
    }
}

#if REALM_MULTITHREAD_QUERY
TableView Query::find_all_multi(size_t start, size_t end)
{
    (void)start;
    (void)end;

    // Initialization
    Init(*m_table);
    ts.next_job = start;
    ts.end_job = end;
    ts.done_job = 0;
    ts.count = 0;
    ts.table = &table;
    ts.node = first[0];

    // Signal all threads to start
    pthread_mutex_unlock(&ts.jobs_mutex);
    pthread_cond_broadcast(&ts.jobs_cond);

    // Wait until all threads have completed
    pthread_mutex_lock(&ts.completed_mutex);
    while (ts.done_job < ts.end_job)
        pthread_cond_wait(&ts.completed_cond, &ts.completed_mutex);
    pthread_mutex_lock(&ts.jobs_mutex);
    pthread_mutex_unlock(&ts.completed_mutex);

    TableView tv(*m_table);

    // Sort search results because user expects ascending order
    sort(ts.chunks.begin(), ts.chunks.end(), &Query::comp);
    for (size_t i = 0; i < ts.chunks.size(); ++i) {
        const size_t from = ts.chunks[i].first;
        const size_t upto = (i == ts.chunks.size() - 1) ? size_t(-1) : ts.chunks[i + 1].first;
        size_t first = ts.chunks[i].second;

        while (first < ts.results.size() && ts.results[first] < upto && ts.results[first] >= from) {
            tv.get_ref_column().add(ts.results[first]);
            ++first;
        }
    }

    return move(tv);
}

int Query::set_threads(unsigned int threadcount)
{
#if defined(_WIN32) || defined(__WIN32__) || defined(_WIN64)
    pthread_win32_process_attach_np ();
#endif
    pthread_mutex_init(&ts.result_mutex, nullptr);
    pthread_cond_init(&ts.completed_cond, nullptr);
    pthread_mutex_init(&ts.jobs_mutex, nullptr);
    pthread_mutex_init(&ts.completed_mutex, nullptr);
    pthread_cond_init(&ts.jobs_cond, nullptr);

    pthread_mutex_lock(&ts.jobs_mutex);

    for (size_t i = 0; i < m_threadcount; ++i)
        pthread_detach(threads[i]);

    for (size_t i = 0; i < threadcount; ++i) {
        int r = pthread_create(&threads[i], nullptr, query_thread, (void*)&ts);
        if (r != 0)
            REALM_ASSERT(false); //todo
    }
    m_threadcount = threadcount;
    return 0;
}


void* Query::query_thread(void* arg)
{
    static_cast<void>(arg);
    thread_state* ts = static_cast<thread_state*>(arg);

    std::vector<size_t> res;
    std::vector<pair<size_t, size_t>> chunks;

    for (;;) {
        // Main waiting loop that waits for a query to start
        pthread_mutex_lock(&ts->jobs_mutex);
        while (ts->next_job == ts->end_job)
            pthread_cond_wait(&ts->jobs_cond, &ts->jobs_mutex);
        pthread_mutex_unlock(&ts->jobs_mutex);

        for (;;) {
            // Pick a job
            pthread_mutex_lock(&ts->jobs_mutex);
            if (ts->next_job == ts->end_job)
                break;
            const size_t chunk = min(ts->end_job - ts->next_job, thread_chunk_size);
            const size_t mine = ts->next_job;
            ts->next_job += chunk;
            size_t r = mine - 1;
            const size_t end = mine + chunk;

            pthread_mutex_unlock(&ts->jobs_mutex);

            // Execute job
            for (;;) {
                r = ts->node->find_first(r + 1, end);
                if (r == end)
                    break;
                res.push_back(r);
            }

            // Append result in common queue shared by all threads.
            pthread_mutex_lock(&ts->result_mutex);
            ts->done_job += chunk;
            if (res.size() > 0) {
                ts->chunks.push_back(std::pair<size_t, size_t>(mine, ts->results.size()));
                ts->count += res.size();
                for (size_t i = 0; i < res.size(); i++) {
                    ts->results.push_back(res[i]);
                }
                res.clear();
            }
            pthread_mutex_unlock(&ts->result_mutex);

            // Signal main thread that we might have compleeted
            pthread_mutex_lock(&ts->completed_mutex);
            pthread_cond_signal(&ts->completed_cond);
            pthread_mutex_unlock(&ts->completed_mutex);
        }
    }
    return 0;
}

#endif // REALM_MULTITHREADQUERY

std::string Query::validate()
{
    if (first.size() == 0)
        return "";

    if (error_code != "") // errors detected by QueryInterface
        return error_code;

    if (first[0] == 0)
        return "Syntax error";

    return first[0]->validate(); // errors detected by QueryEngine
}

void Query::Init(const Table& table) const
{
    if (first[0] != nullptr) {
        ParentNode* top = first[0];
        top->init(table);
        std::vector<ParentNode*> v;
        top->gather_children(v);
    }
}

bool Query::is_initialized() const
{
    const ParentNode* top = first[0];
    if (top != nullptr) {
        return top->is_initialized();
    }
    return true;
}

size_t Query::FindInternal(size_t start, size_t end) const
{
    if (end == size_t(-1))
        end = m_table->size();
    if (start == end)
        return not_found;

    size_t r;
    if (first[0] != 0)
        r = first[0]->find_first(start, end);
    else
        r = start; // user built an empty query; return any first

    if (r == m_table->size())
        return not_found;
    else
        return r;
}

bool Query::comp(const std::pair<size_t, size_t>& a, const std::pair<size_t, size_t>& b)
{
    return a.first < b.first;
}

void Query::UpdatePointers(ParentNode* p, ParentNode** newnode)
{
    all_nodes.push_back(p);
    if (first[first.size()-1] == 0) {
        first[first.size()-1] = p;
    }

    if (update[update.size()-1] != 0) {
        *update[update.size()-1] = p;
    }
    update[update.size()-1] = newnode;

    HandlePendingNot();
}

/* ********************************************************************************************************************
*
*  Stuff related to next-generation query syntax
*
******************************************************************************************************************** */

Query& Query::and_query(Query q)
{
    // This transfers ownership of the nodes from q to this, so both q and this
    // must currently own their nodes
    REALM_ASSERT(do_delete && q.do_delete);

    ParentNode* const p = q.first[0];
    UpdatePointers(p, &p->m_child);

    // q.first[0] was added by UpdatePointers, but it'll be added again below
    // so remove it
    all_nodes.pop_back();

    // The query on which AddQuery() was called is now responsible for destruction of query given as argument. do_delete
    // indicates not to do cleanup in deconstructor, and all_nodes contains a list of all objects to be deleted. So
    // take all objects of argument and copy to this node's all_nodes list.
    q.do_delete = false;
    all_nodes.insert( all_nodes.end(), q.all_nodes.begin(), q.all_nodes.end() );

    if (q.m_source_link_view) {
        REALM_ASSERT(!m_source_link_view || m_source_link_view == q.m_source_link_view);
        m_source_link_view = q.m_source_link_view;
    }

    return *this;
}


Query Query::operator||(Query q)
{
    Query q2(*this->m_table);
    q2.and_query(*this);
    q2.Or();
    q2.and_query(q);

    return q2;
}


Query Query::operator&&(Query q)
{
    if(first[0] == nullptr)
        return q;

    if(q.first[0] == nullptr)
        return (*this);

    Query q2(*this->m_table);
    q2.and_query(*this);
    q2.and_query(q);

    return q2;
}


Query Query::operator!()
{
    if (first[0] == nullptr)
        throw std::runtime_error("negation of empty query is not supported");
    Query q(*this->m_table);
    q.Not();
    q.and_query(*this);
    return q;
}
