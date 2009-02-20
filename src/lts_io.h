#ifndef LTS_IO_H
#define LTS_IO_H

#include <stdint.h>
#include <amconfig.h>
#include <archive.h>
#include <options.h>
#include <greybox.h>
#include <lts_enum.h>
#include <lts_count.h>

/**
\file lts_io.h
\brief Producers and consumers for LTS files.
*/

/**
\defgroup ltsio Input and Output for labeled transition systems
*/
/*@{*/

/**
\brief Initialize the LTS IO library.
 */
extern void lts_io_init(int *argcp,char*argv[]);


typedef struct lts_output_struct *lts_output_t;

/**
\brief Create a handle for output to an archive.


\param outputname The name of the output file or directory.
\param model A grey box model, which is used to get static
information like state vector length, names of labels, etc.
The model is also used to access the chunk tables, which are
needed for writing the LTS.
\param segment_count The number of segments of the output file.
\param state_segment If less than then the segment count, this is
the number of the segment, whose state information will be written
through this output object. If equal to segment count then all
state information will be written through this object.
\param src_segment Select the source segment(s) of edges to be written.
\param dst_segment Select the destination segment(s) of edges to be written.
 */
extern lts_output_t lts_output_open(
	char *outputname,
	model_t model,
	int segment_count,
	int state_segment,
	int src_segment,
	int dst_segment
);

/**
\brief Get an enumeration consumer for an output.
 */
extern lts_enum_cb_t lts_output_enum(lts_output_t out);

/**
\brief Provide access to the state/transition counters.
 */
extern lts_count_t *lts_output_count(lts_output_t out);

/**
\brief Write header info and close the file or directory.

Note that in a parallel application, the state/transition counts must be synchonized
before calling this function.
 */
extern void lts_output_close(lts_output_t *out);

typedef struct lts_input_struct *lts_input_t;

/**
\brief Open one share of a file or directory that contains an LTS.

\param inputname The name of the file or directory to be opened.
\param model A grey box model that will be used to store the LTS type info and the values tables.
\param share The id of the share to be accessed through the handle.
\param share_count The total number of shares.

The my_id/worker pair determines the share of the LTS that belongs to this handle.
 */
extern lts_input_t lts_input_open(char*inputname,model_t model,int share,int share_count);

/**
\brief Get the number of segments in input.
 */
extern int lts_input_segments(lts_input_t input);

/**
\brief Provide access to the state/transition counters.
 */
extern lts_count_t *lts_input_count(lts_input_t in);

/**
\brief Enumerate one share of the states and/or edges in an LTS file.

\param input Handle for the share of the file that must be enumerated.
\param states If zero then states are not enumerated otherwise they are.
\param edges If zero then edges are not enumerated otherwise they are.
\param output Delivery point.
 */
extern void lts_input_enum(lts_input_t input,int states,int edges,lts_enum_cb_t output);

/**
\brief Destroy an input object.
 */
extern void lts_input_close(lts_input_t *input_p);

/*@}*/

#endif

