// -*- tab-width:4 ; indent-tabs-mode:nil -*-

/**
@file archive.h
@brief Library for dealing with archives.
*/

#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <hre-io/user.h>
#include <util-lib/string-map.h>

/// Test if archive is readable.
extern int arch_readable(archive_t archive);

/// Test if the archive contains the stream.
extern int arch_contains(archive_t archive,char *name);

/**
\brief Read a stream in the archive, without decompressing it.

The compression used for this stream is returned in the variable code.
*/
extern stream_t arch_read_raw(archive_t archive,char *name,char**code);

/// Read a stream in the archive, while decompressing it.
extern stream_t arch_read(archive_t archive,char *name);

/// Manually enable transparent compression;
extern void arch_set_transparent_compression(archive_t archive);

/// Test if an archive is writable.
extern int arch_writable(archive_t archive);

/// Write a stream to the archive, while applying the given compression.
extern stream_t arch_write_apply(archive_t archive,char *name,char*code);

/// Write a stream to the archive, which has already been compressed.
extern stream_t arch_write_raw(archive_t archive,char *name,char*code);

/// Write a stream to the archive, applying compression as specified by the policy.
extern stream_t arch_write(archive_t archive,char *name);

/// Get the compression policy.
extern string_map_t arch_get_write_policy(archive_t archive);

/// Set the compression policy.
extern void arch_set_write_policy(archive_t archive,string_map_t policy);

/// Archive enumerator type.
typedef struct arch_enum* arch_enum_t;

/// Get an enumerator for an archive.
extern arch_enum_t arch_enum(archive_t archive,char *regex);

/// Info item for a stream in an archive.
typedef struct archive_item_s {
    const char *name;
    const char *code;
    off_t length;
    off_t compressed;
} *archive_item_t;

/// Callbacks for enumerating an archive.
struct arch_enum_callbacks {
	int(*new_item)(void*arg,int no,const char*name);
	int(*end_item)(void*arg,int no);
	int(*data)(void*arg,int no,void*data,size_t len);
	int(*stat)(void*arg,int no,archive_item_t item);
};

/**
\brief Enumerate an archive.

Returns 0 if everything is completed. If one of the callbacks returns non-zero this
non-zero response is immediately returned.
 */
extern int arch_enumerate(arch_enum_t enumerator,struct arch_enum_callbacks *cb,void*arg);

/**
\brief Free an enumerator.
*/
extern void arch_enum_free(arch_enum_t* enumerator);

/// Close an archive.
extern void arch_close(archive_t *archive);

/// Create a directory and access it as an archive.
extern archive_t arch_dir_create(char*dirname,int buf,int del);

/// Open an existing directory as an archive.
extern archive_t arch_dir_open(char*dirname,int buf);

/// Open a new GCF archive for writing.
extern archive_t arch_gcf_create(raf_t raf,size_t block_size,size_t cluster_size,int worker,int worker_count);

/// Read an existing GCF archive.
extern archive_t arch_gcf_read(raf_t raf);

/**
\brief Read an existing ZIP archive.
 */
extern archive_t arch_zip_read(const char* name,int buf_size);

/**
\brief Create a new zip archive from an existing archive.

Note that it will never be possible to have a writable ZIP archive,
because ZIP archives cannot deal with multiple streams at once.
 */
extern void arch_zip_create(const char* name,int buf_size,string_map_t policy,archive_t contents);

/// Get the name of the archive.
extern const char* arch_name(archive_t archive);

#endif

