#include "http_parser.h"
#include "stdbool.h"
#include "stdint.h"

#ifndef __rsva11001adapter_request
#define __rsva11001adapter_request

struct rsva11001_request_t;
typedef struct rsva11001_request_t rsva11001_request;

rsva11001_request * rsva11001_request_malloc();
void rsva11001_request_associate(rsva11001_request * const this, int sockfd);
bool rsva11001_request_send(rsva11001_request * const this);

enum{
	REQUEST_WOULDBLOCK,
	REQUEST_FILL,
	REQUEST_ERROR,
	REQUEST_DONE
};
bool rsva11001_request_recv(rsva11001_request * const this);

bool rsva11001_request_query(rsva11001_request * const this,unsigned int * channel);

bool rsva11001_request_fill(rsva11001_request * const this,uint8_t const * const headers,uint_fast32_t headersLength, uint8_t const * const body , uint_fast32_t bodyLength);
bool rsva11001_request_unknown(rsva11001_request * const this);
bool rsva11001_request_isDone(rsva11001_request * const this);


#endif
