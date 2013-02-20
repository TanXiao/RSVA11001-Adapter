#include "request.h"
#include "iso646.h"
#include "stdlib.h"
#include "http-parser.h"

struct rsva11001_request_t{
	int sockfd;
	uint8_t url[64];
	
	uint8_t * responseHeaders;
	uint_fast32_t responseHeadersAmountWritten;
	uint_fast32_t responseHeaderLength;
	
	uint8_t * responseBody;
	uint_fast32_t responseBodyAmountWritten;
	uint_fast32_t responseBodyLength;
	
	http_parser parser;
};

static http_parser_settings settings;
settings.on_header_value = NULL;
settings.on_header_field = NULL;
settings.on_message_begin = NULL;
settings.on_status_complete = NULL;


static int onUrl(http_parser const * const parser, uint8_t * data, size_t length)
{
	return 0;
}

settings.on_url = (void*)onUrl;
settings.on_status_complete = ;
settings.on_body = ;
settings.on_message_end = ;

rsva11001_request * rsva11001_request_malloc(){
	rsva11001_request * const this = malloc(sizeof(rsva11001_request));
	
	if(this)
	{
		this->sockfd = 0;
		this->responseBody = NULL;
		this->responseHeaders = NULL;
	}
	return this;
}

void rsva11001_request_associate(rsva11001_request * const this, int sockfd)
{
	this->sockfd = sockfd;
}

bool rsva11001_request_send(rsva11001_request * const this)
{
	assert(this!=NULL);
	
	if(this->responseHeaders)
	{
		
	}
	
	if(this->responseBody)
	{
		
	}
	
	return true;
}
bool rsva11001_request_recv(rsva11001_request * const this)
{
	assert(this!=NULL);
	
	while(true)
	{
		static const char BUFFER_SIZE = 1024
		uint8_t buffer[BUFFER_SIZE];
		
		const ssize_t amountReceived = recv(this->sockfd,buffer,sizeof(buffer),0);
		
		if(amountReceived == -1)
		{
			const int error = errno;
			
			switch(error)
			{
				case EAGAIN:
					return true;
				default:
					return false;
			}
		}
		
	}
	return true;
}

bool rsva11001_request_query(rsva11001_request * const this,unsigned int * channel)
{
	return true;
}

bool rsva11001_request_fill(rsva11001_request * const this,uint8_t const * const headers,uint_fast32_t headersLength, uint8_t const * const body , uint_fast32_t bodyLength)
{
	
	return true;
}
bool rsva11001_request_unknown(rsva11001_request * const this)
{
	return true;
}
bool rsva11001_request_isDone(rsva11001_request * const this)
{
	return this->responseBody == NULL and this->responseHeaders == NULL;
}

