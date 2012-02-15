// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef DIR_OPS_H
#define DIR_OPS_H

/** @name Directory creation.
  */
//@{

/** Create an empty Directory. If no directory exists then it is created
  * with access permission 777 & #umask#. Otherwise the behaviour is
  * controlled by setting #delete# to a bitwise or of some of the following
  * constants.
  * \par\noindent\begin{tabular}{ll}
  * #DELETE_NONE# & Fail if #name# exists.
  * \\
  * #DELETE_FILE# & if a regular file #name# exists it will be deleted.
  * \\
  * #DELETE_DIR# & if a directory #name# exists then every entry in that
  *            \\& directory will be unlinked. The mode of the directory
  *            \\& will {\em not} be changed.
  * \\
  * #DELETE_ALL# & Bitwise or of all of the above.
  * \end{tabular}
  * @param name Name for the new directory.
  * @param delete Influence handling of existing files/directories.
  * @return 0 on success.
  * @return -1 on error. In addition #errno# is set appropriately.
  */
extern int create_empty_dir(const char *name,int delete);

#define DELETE_NONE 0x00
#define DELETE_FILE 0x01
#define DELETE_DIR  0x02
#define DELETE_ALL  0x03

/** Check for existance of a directory.
  * @return 1 if #name# is a directory
  * @return 0 otherwise
  */
extern int is_a_dir(const char *name);

/** Check for existance of a file.
  * @return 1 if #name# is a regular file
  * @return 0 otherwise
  */
extern int is_a_file(const char *name);

typedef void* dir_enum_t;

extern dir_enum_t get_dir_enum(const char *name);

extern char* get_next_dir(dir_enum_t e);

extern void del_dir_enum(dir_enum_t e);

extern void recursive_erase(const char *name);

//@}

#endif

