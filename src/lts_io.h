#ifndef LTS_IO_H
#define LTS_IO_H

#include <popt.h>
#include <stdint.h>
#include <amconfig.h>
#include <archive.h>
#include <greybox.h>
#include <lts_enum.h>
#include <lts_count.h>
#include <tables.h>
#include <lts-type.h>

/**
\file lts_io.h
\brief Producers and consumers for LTS files.
*/

/**
\defgroup ltsio Input and Output for labeled transition systems
*/
/*@{*/

/**
\brief Options for the LTS I/O library.

Parsing these options has the side effect of initializing the library.
 */
extern struct poptOption lts_io_options[];

typedef struct lts_output_struct *lts_output_t;

/**
\brief Open an LTS file for writing in a certain mode.

\param requested_mode A string of length three that describes the requested write mode.
\param actual_mode A pointer to a string, where the actual mode can be returned.
If this is NULL then the requested mode is mandatory. Otherwise the mode might be changed
and the chosen mode is returned here.
 */
extern lts_output_t lts_output_open(
	char *outputname,
	model_t model,
	int segment_count,
	int share,
	int share_count,
	const char *requested_mode,
	char **actual_mode
);

/**
\brief Set the root vector.
 */
extern void lts_output_set_root_vec(lts_output_t output,uint32_t * root);

/**
\brief Set the root segment/offset.
 */
extern void lts_output_set_root_idx(lts_output_t output,uint32_t root_seg,uint32_t root_ofs);

/**
\brief Get an enumeration consumer for an output.
 */
extern lts_enum_cb_t lts_output_begin(lts_output_t out,int which_state,int which_src,int which_dst);

/**
\brief Close and destroy output handle.
 */
extern void lts_output_end(lts_output_t out,lts_enum_cb_t e);


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
\param requested_mode A string of length three that describes the requested write mode.
\param actual_mode A pointer to a string, where the actual mode can be returned.
If this is NULL then the requested mode is mandatory. Otherwise the mode might be changed
and the chosen mode is returned here.

The my_id/worker pair determines the share of the LTS that belongs to this handle.
 */
extern lts_input_t lts_input_open(
    char*inputname,
    model_t model,
    int share,
    int share_count,
	const char *requested_mode,
	char **actual_mode
);

/**
\brief Get the type of the LTS.
*/
extern lts_type_t lts_input_ltstype(lts_input_t input);

/**
\brief Get the number of segments in input.
 */
extern int lts_input_segments(lts_input_t input);

/**
\brief Provide the location for loading a value table.
*/
extern void lts_input_vt_set(lts_input_t input,int table,value_table_t vt);

/**
\brief Provide the location for loading the state information of a segment.
*/
extern void lts_input_state_table_set(lts_input_t input,int segment,matrix_table_t mt);

/**
\brief Provide the location for loading the edges of a segment.
*/
extern void lts_input_edge_table_set(lts_input_t input,int segment,matrix_table_t mt);

/**
\brief Load an LTS into the provided tables.
*/
extern void lts_input_load(lts_input_t input);

/**
\brief Try changing the input mode.

For example, a DIR file with N segments can be read as
as part -si or as an N part -is.
*/
extern int lts_input_chmod(lts_input_t input,const char *mode);

/**
\brief Get the mode, which was used to store the input.
*/
extern char* lts_input_mode(lts_input_t input);

/**
\brief Get the root vector
*/
extern uint32_t* lts_input_root(lts_input_t input);

/**
\brief Get the root segment.
 */
extern uint32_t lts_root_segment(lts_input_t input);

/**
\brief Get the root offset.
 */
extern uint32_t lts_root_offset(lts_input_t input);

/**
\brief Provide access to the state/transition counters.
 */
extern lts_count_t *lts_input_count(lts_input_t in);



/**
\brief Enumerate the states and/or edges in a LTS file.

\param input Handle for the share of the file that must be enumerated.
\param flags bitwise or of one or more LTS_ENUM_xxx macros
\param output Delivery point.

Callback order has to be defined in case of both states and edges!
 */
extern void lts_input_enum_all(lts_input_t input,int flags,lts_enum_cb_t output);

/**
\brief Enumerate the states and/or edges of one partition in a LTS file.

\param input Handle for the share of the file that must be enumerated.
\param part The number of the partition to be enumerated.
\param flags bitwise or of one or more LTS_ENUM_xxx macros
\param output Delivery point.

Callback order has to be defined in case of both states and edges!
 */
extern void lts_input_enum_part(lts_input_t input,int part,int flags,lts_enum_cb_t output);

/**
\brief Destroy an input object.
 */
extern void lts_input_close(lts_input_t *input_p);

/*@}*/

#endif

