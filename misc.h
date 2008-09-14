#ifndef MISC_H
#define MISC_H

#include "config.h"
#include <pthread.h>

/** Check for existence of a directory.
  * @return 1 if @c name is a directory
  * @return 0 otherwise
  */
extern int IsADir(const char *name);

/** Check for existence of a regular file.
  * @return 1 if @c name is a regular file
  * @return 0 otherwise
  */
extern int IsReg(const char *name);

/** Creates empty directory
* @return 0 success 
* @return <0 error
*/
extern int CreateDir(const char *name);

/** Calls for each found file in @c dir callback function @c eachFile.
@param dir which contains the files
@param eachFile callback function which must return @c 0 if OK, and has one 
argument (sort @c char*) @c path to file. */

typedef int (*EachFile)(char *path);
extern int ForEachFileInDir(const char *dir, EachFile eachFile);
extern int ForEachDirInDir(const char *path, EachFile eachDir);

int serversocket(int port);
void tcp_listen(pthread_t *thr,int port,void(*setup)(int sd,void*arg),void*arg);
int clientsocket(char *hostname,int port);


#endif

