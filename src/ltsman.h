#ifndef LTS_MANAGER_H
#define LTS_MANAGER_H

/**
@file ltsman.h
@brief Library for managing labeled transition systems.


*/

#include "config.h"
#include "stringindex.h"
#include "stream.h"
#include "packet_stream.h"

typedef struct lts_s *lts_t;
typedef uint32_t lts_seg_t;
typedef uint32_t lts_ofs_t;
typedef uint32_t lts_count_t;
typedef uint64_t lts_global_t;

/// Is the root known;
extern int lts_has_root(lts_t lts);
/// What is the root segment?
extern lts_seg_t lts_get_root_seg(lts_t lts);
/// What is the root offset?
extern lts_ofs_t lts_get_root_ofs(lts_t lts);
/// Set the root state.
extern void lts_set_root(lts_t lts,lts_seg_t seg,lts_ofs_t ofs);

/// Is the segment count known?
extern int lts_has_segments(lts_t lts);
/// Get the segment count.
extern lts_seg_t lts_get_segments(lts_t lts);
/// Set the segment count.
extern void lts_set_segments(lts_t lts,lts_seg_t segments);

/// Is the state count of the given segment known?
extern int lts_has_states(lts_t lts,lts_seg_t seg);
/// Get the segment count of a segment.
extern lts_ofs_t lts_get_states(lts_t lts,lts_seg_t seg);
/// Set the segment count of a segment.
extern void lts_set_states(lts_t lts,lts_seg_t seg,lts_ofs_t count);

/// Is the state count of all segments known?
extern int lts_has_all_states(lts_t lts);
/// Get the total number of states.
extern lts_global_t lts_get_all_states(lts_t lts);

/// Add a number of states to a segment.
extern void lts_add_states(lts_t lts,lts_seg_t seg,lts_count_t count);
/// Get the number of states added to a segment.
extern lts_count_t lts_added_states(lts_t lts,lts_seg_t seg);

/// Is the number of transitions between the given segments known?
extern int lts_has_trans(lts_t lts,lts_seg_t src,lts_seg_t dst);
/// Set the number of transitions between two segments.
extern lts_count_t lts_get_trans(lts_t lts,lts_seg_t src,lts_seg_t dst);
/// Get the number of transitions between two segments.
extern void lts_set_trans(lts_t lts,lts_seg_t src,lts_seg_t dst,lts_count_t count);

/// Is the number of transitions between all segments known?
extern int lts_has_all_trans(lts_t lts);
/// Get the total number of transitions.
extern lts_global_t lts_get_all_trans(lts_t lts);

/// Add transitions between two segments.
extern void lts_add_trans(lts_t lts,lts_seg_t src,lts_seg_t dst,lts_count_t count);
/// Get the number of added transitions between two segments.
extern lts_count_t lts_added_trans(lts_t lts,lts_seg_t src,lts_seg_t dst);
/// Get the total number of transitions added.
extern lts_global_t lts_added_all_trans(lts_t lts);

/// Check if all counts which were both added and set are consistent.
extern int lts_count_check(lts_t lts);

/// Add a new comment.
extern void lts_add_comment(lts_t lts,char*comment);
/// Reset the builtin comment enumerator.
extern void lts_reset_comment(lts_t lts);
/// Get the next comment.
extern char* lts_next_comment(lts_t lts);

/**
Temporary edge label solution until a proper state/edge label system can de defined. 
*/
extern string_index_t lts_get_string_index(lts_t lts);

/// Create an empty LTS info object.
extern lts_t lts_new();
/// The various types of info serialisations.
typedef enum {LTS_INFO_DIR,LTS_INFO_PACKET} info_fmt_t;
/// Deserialize an info object and detect the format.
extern lts_t lts_read_info(stream_t ds,int*header_found);
/// Serialize an info object in the specified format.
extern void lts_write_info(lts_t lts,stream_t ds,info_fmt_t format);

/** Set a packet stream that tracks new additions to this object.

Only new information wil be written to this stream. To write the
currently known information use #lts_write_info in LTS_INFO_PACKET mode.
*/
extern void lts_set_packet_stream(lts_t lts,stream_t stream);
/// Packet callback for adding data to an lts.
extern void lts_info_add(lts_t lts,uint16_t len,uint8_t* data);

#endif

