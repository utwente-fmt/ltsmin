#ifndef DIR_OPS_H
#define DIR_OPS_H

#include "config.h"

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
extern int CreateEmptyDir(char *name,int delete);

#define DELETE_NONE 0x00
#define DELETE_FILE 0x01
#define DELETE_DIR  0x02
#define DELETE_ALL  0x03

/** Check for existance of a directory.
  * @return 1 if #name# is a directory
  * @return 0 otherwise
  */
extern int IsADir(char *name);

typedef void* dir_enum_t;

extern dir_enum_t GetDirEnum(char *name);

extern char* GetNextDir(dir_enum_t e);

extern void DelDirEnum(dir_enum_t e);

//@}

#endif

