#ifndef CLIENT_SERVER_H
#define CLIENT_SERVER_H

#include <hre-io/user.h>

/**
\brief Opaque type for a server handle.
 */
typedef struct server_handle_s *server_handle_t;

/**
\brief Create a TCP server handle.
*/
extern server_handle_t CScreateTCP(int port);

/**
\brief Create a TCP client connection.
*/
extern stream_t CSconnectTCP(const char* name,int port);

/**
\brief Accept a connection on a server handle.
*/
extern stream_t CSaccept(server_handle_t handle);

#endif

