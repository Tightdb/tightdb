/*************************************************************************
 *
 * TIGHTDB CONFIDENTIAL
 * __________________
 *
 *  [2011] - [2012] TightDB Inc
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
 **************************************************************************/
#ifndef TIGHTDB_UTIL_FILE_MAPPER_HPP
#define TIGHTDB_UTIL_FILE_MAPPER_HPP

#include <tightdb/util/file.hpp>

namespace tightdb {
namespace util {

void *mmap(int fd, size_t size, File::AccessMode access, const char *encryption_key);
void munmap(void *addr, size_t size) TIGHTDB_NOEXCEPT;
void* mremap(int fd, void* old_addr, size_t old_size, File::AccessMode a, size_t new_size);
void msync(void *addr, size_t size);

File::SizeType encrypted_size_to_data_size(File::SizeType size) TIGHTDB_NOEXCEPT;
File::SizeType data_size_to_encrypted_size(File::SizeType size) TIGHTDB_NOEXCEPT;
size_t round_up_to_page_size(size_t size) TIGHTDB_NOEXCEPT;

}
}
#endif
