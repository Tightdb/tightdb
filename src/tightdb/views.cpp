#include <tightdb/views.hpp>

using namespace tightdb;

// Re-sort view according to last used criterias
void RowIndexes::sort(Sorter& sorting_predicate)
{
    std::vector<size_t> v;
    // always put any detached refs at the end of the sort
    // FIXME: reconsider if this is the right thing to do
    // FIXME: consider specialized implementations in derived classes
    // (handling detached refs is not required in linkviews)
    size_t detached_ref_count = 0;
    for (size_t t = 0; t < size(); t++) {
        size_t ndx = m_row_indexes.get(t);
        if (ndx != -1ULL) {
            v.push_back(ndx);
        }
        else
            ++detached_ref_count;
    }
    sorting_predicate.m_row_indexes_class = this;
    std::stable_sort(v.begin(), v.end(), sorting_predicate);
    m_row_indexes.clear();
    for (size_t t = 0; t < v.size(); t++)
        m_row_indexes.add(v[t]);
    for (size_t t = 0; t < detached_ref_count; ++t)
        m_row_indexes.add(-1);
}

