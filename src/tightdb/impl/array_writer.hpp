/*************************************************************************
 *
 * TIGHTDB CONFIDENTIAL
 * __________________
 *
 *  [2011] - [2014] TightDB Inc
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
***************************************************************************/

#ifndef TIGHTDB_ARRAY_WRITER_HPP
#define TIGHTDB_ARRAY_WRITER_HPP

namespace tightdb {
namespace _impl {

class ArrayWriterBase {
public:
    virtual ~ArrayWriterBase() {}

    /// Write the specified array data and its checksum into free
    /// space.
    ///
    /// Returns the position in the file where the first byte was
    /// written.
    virtual size_t write_array(const char* data, size_t size, uint_fast32_t checksum) = 0;
};

} // namespace impl_
} // namespace tightdb

#endif // TIGHTDB_ARRAY_WRITER_HPP
