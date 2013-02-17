#include "stdbool.h"
#include "inttypes.h"

#ifndef __rsva11001adapter_rsva11001connection
#define __rsva11001adapter_rsva11001connection

struct rsva11001_connection_t;

typedef struct rsva11001_connection_t rsva11001_connection;

rsva11001_connection * rsva11001_connection_malloc();

void rsva11001_connection_destroy(rsva11001_connection * const this);

bool rsva11001_connection_configure(rsva11001_connection * const this,
const char * hostname,
const char * username,
const char * password,
unsigned int channel);

bool rsva11001_connection_open(rsva11001_connection * const this);

bool rsva11001_connection_close(rsva11001_connection * const this);

int rsva11001_connection_getFd(rsva11001_connection const * const this);

bool rsva11001_connection_process(rsva11001_connection * const this);

bool rsva11001_connection_getLatestImage(rsva11001_connection * const this,uint8_t * const buffer, uint_fast32_t * size);

const char * rsva11001_connection_getLastError(rsva11001_connection const * const this);

#endif
