#define _GNU_SOURCE
#include "rsva11001.h"
#include "assert.h"
#include "iso646.h"
#include "errno.h"
#include "stdlib.h"
#include "string.h"
#include "sys/socket.h"
#include "netinet/in.h"
#include "netinet/tcp.h"
#include <sys/param.h>
#include "http_parser.h"
#include "arpa/inet.h"
#include "stdio.h"
#include "unistd.h"
#include "netdb.h"
#include "poll.h"

#define setLastError(_this) do{\
const int error = errno;\
char buf[sizeof(_this->lastError)];\
const char * const errStr = strerror_r(error,buf,sizeof(buf));\
snprintf(_this->lastError,sizeof(_this->lastError),"%s:%i:%s %s",__FILE__,__LINE__,__func__,errStr);\
_this->lastError[sizeof(_this->lastError)-1];\
}while(0);

struct rsva11001_connection_t
{
	char lastError[256];
	
	
	int sockfd;
	char username[64];
	char password[64];
	char hostname[64];
	unsigned int channel ;
	
	uint8_t * outputBuffer;
	uint_fast32_t outputLength;
	uint_fast32_t bytesWritten;

	bool ping; 
	uint8_t * pingReceiveBuffer;
	uint_fast32_t pingReceiveBufferSize;
	
	uint8_t * pongReceiveBuffer;
	uint_fast32_t pongReceiveBufferSize;
	
	uint8_t ** activeReceiveBuffer;
	uint_fast32_t * activeReceiveBufferSize;
	uint_fast32_t activeReceiveBufferBytesReceived;
	
	http_parser_settings settings;
	http_parser parser;
	
	bool connected;
};

const char * rsva11001_connection_getLastError(rsva11001_connection const * const this)
{
	return this->lastError;
}


static const uint16_t HTTP_PORT = 80;
static const char * const HTTP_PATH = "/snapshot.html";

static const unsigned int HEADER_SIZE = 8;
static const char * const HEADER = "JPGS";
static const unsigned int FOOTER_SIZE = 4;
static const char * const FOOTER = "JPGE";

static const unsigned int MAX_CHAN = 9;
static const unsigned int MIN_CHAN = 1;

static void rsva11001_connection_swapBuffers(rsva11001_connection * const this)
{
	if(this->ping)
	{
		this->activeReceiveBuffer = &  this->pongReceiveBuffer;
		this->activeReceiveBufferSize = & this->pongReceiveBufferSize;
	}
	else
	{
		this->activeReceiveBuffer = & this->pingReceiveBuffer;
		this->activeReceiveBufferSize = & this->pingReceiveBufferSize;
	}
	
	this->ping = !(this->ping);
}

int rsva11001_connection_recv(http_parser const * const parser, const char * data, size_t length)
{
	rsva11001_connection * const this = parser->data;
	
	assert(this!=NULL);
	assert(this->activeReceiveBuffer!=NULL);
	assert(*this->activeReceiveBuffer!=NULL);
	assert(this->activeReceiveBufferSize!=NULL);
	
	uint_fast32_t const spaceRemaining = *(this->activeReceiveBufferSize) - this->activeReceiveBufferBytesReceived;
	
	assert(spaceRemaining!=0);
	
	uint8_t * const destination = * this->activeReceiveBuffer + this->activeReceiveBufferBytesReceived;
	
	uint_fast32_t const oldSize = this->activeReceiveBufferBytesReceived;
	
	//Copy the new data into the buffer
	{
		const size_t amountToCopy = MIN(length,spaceRemaining);
		memcpy(destination, data,  amountToCopy);
		
		this->activeReceiveBufferBytesReceived += amountToCopy;
	}
	
	//The first 8 bytes are
	// 'JPGS' <big endian 32-bit unsigned integer>	
	//If the header still hasn't been received, nothing can be done yet
	if(this->activeReceiveBufferBytesReceived < HEADER_SIZE)
	{
		return 0;
	}
	
	//Make sure the magic value is in the header
	if(0 != memcmp(HEADER,*(this->activeReceiveBuffer),4) )
	{
		return 1;
	}
	
	//Extract the length of the JPEG image 
	uint32_t jpegLength;
	
	memcpy((void*)&jpegLength,*(this->activeReceiveBuffer) + 4, sizeof(uint32_t));
	
	//Swap to native byte order
	jpegLength = ntohl(jpegLength);
	
	//Check to see if the whole msg has been received
	const uint_fast32_t totalLength = HEADER_SIZE + jpegLength + FOOTER_SIZE;
	
	if(totalLength <= this->activeReceiveBufferBytesReceived)
	{
		//Make sure the footer is there
		if(0!= memcmp(FOOTER,*(this->activeReceiveBuffer) + HEADER_SIZE + jpegLength,FOOTER_SIZE))
		{
			return 1;
		}
		
		//Figure out what bytes are part of the new message		
		uint_fast32_t const bytesReceivedThisCallForMessageThatIsNowComplete = (totalLength - oldSize);
		
		uint_fast32_t const overflow = length - bytesReceivedThisCallForMessageThatIsNowComplete;
		
		//Swap the buffers
		rsva11001_connection_swapBuffers(this);
		
		//Copy the overflow data into the new one
		memcpy(*(this->activeReceiveBuffer), data + bytesReceivedThisCallForMessageThatIsNowComplete, overflow);
		
		//Update the length of the new buffer
		this->activeReceiveBufferBytesReceived = overflow;
		
	}
	
	return 0;
	
	
}

bool rsva11001_connection_getLatestImage(rsva11001_connection * const this,uint8_t * const out, uint_fast32_t * size)
{
	uint8_t const * buffer;	
	if(this->ping)
	{
		buffer = this->pongReceiveBuffer;
	}
	else
	{
		buffer = this->pingReceiveBuffer;
	}
	
	//Make sure the magic value is in the header
	if(0 != memcmp(HEADER,buffer,4) )
	{
		*size = 0;
		return true;
	}
	
	//Extract the length of the JPEG image 
	uint32_t jpegLength;
	
	memcpy((void*)&jpegLength,buffer + 4, sizeof(uint32_t));
	
	//Swap to native byte order
	jpegLength = ntohl(jpegLength);
	
	//Make sure the magic value is in the footer
	if(0!= memcmp(FOOTER,buffer + HEADER_SIZE + jpegLength,FOOTER_SIZE))
	{
		*size = 0;
		return true;
	}
	
	//Make sure there is enough space for the image
	
	if(*size < jpegLength)
	{
		*size = 0;
		return true;
	}
	
	//Copy over the image
	
	memcpy(out,buffer+HEADER_SIZE,jpegLength);
	
	*size= jpegLength;
	
	return true;
	
	
}

rsva11001_connection * rsva11001_connection_malloc(){
	rsva11001_connection * this = malloc(sizeof(rsva11001_connection));
	
	if(this)
	{
		
		this->sockfd = -1;
		this->channel = 0;
		this->username[0] = '\0';
		this->password[0] = '\0';
		this->hostname[0] = '\0';
		
		this->outputBuffer = NULL;
		this->outputLength = 0;
		
		
		this->pingReceiveBuffer = NULL;
		this->pingReceiveBufferSize = 0;
		
		this->pongReceiveBufferSize = 0;
		this->pongReceiveBuffer = NULL;
		
		this->activeReceiveBuffer = NULL;
		
		bzero(&(this->settings),sizeof(this->settings));
		
		this->settings.on_body = (void *)rsva11001_connection_recv;
		
		bzero(&(this->parser),sizeof(this->parser));
		this->parser.data = this;
		
		this->ping = true;
		
		this->lastError[0]='\0';
		this->connected = false;
	}
	
	
	
	return this;
}

bool rsva11001_connection_configure(rsva11001_connection * const this,
const char * hostname,
const char * username,
const char * password,
unsigned int channel)
{
	assert(this!=NULL);
	assert(hostname!=NULL);
	assert(username!=NULL);
	assert(password!=NULL);
	
	//don't reconfigure while open
	if(this->sockfd != -1)
	{
		return false;
	}
	
	if(
	strlen(hostname) >= sizeof(this->hostname) or
	strlen(username) >= sizeof(this->username) or
	strlen(password) >= sizeof(this->password) )
	{
		return false;
	}
	
	if(channel > MAX_CHAN or channel < MIN_CHAN)
	{
		return false;
	}
	
	strcpy(this->hostname,hostname);
	strcpy(this->username,username);
	strcpy(this->password,password);
	this->channel = channel;
	
	return true;
}

bool rsva11001_connection_open(rsva11001_connection * const this)
{
	//If already open, just return
	if(this->sockfd!=-1)
	{
		return true;
	}
	
	
	//Open the socket
	this->sockfd = socket(AF_INET,SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,0);
	
	if(this->sockfd == -1 )
	{
		setLastError(this);
		return false;
	}
	
	//Connect to the server
	
	struct sockaddr_in addr;
	
	{
		static const char * SERVICE = "http";
		
		struct addrinfo * res;
		struct addrinfo hints;
		bzero(&hints,sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		
		if(0!=getaddrinfo(this->hostname,SERVICE,&hints,&res))
		{
			setLastError(this);
			close(this->sockfd);
			this->sockfd = -1;
			return false;
		}
		
		memcpy(&addr,res->ai_addr,sizeof(addr));
		
		freeaddrinfo(res);
	}
	
	if(0!=connect(this->sockfd,&addr,sizeof(addr)))
	{
		const int error = errno;
		
		switch(error)
		{
			default:
			{
				setLastError(this);
				close(this->sockfd);
				this->sockfd = -1;
				return false;
			}
			
			case EINPROGRESS:
				break;

		}
	}
	
	this->connected = false;
	
	
	//Build the request
	static const unsigned int outputBufferSize = 512;
	if(not this->outputBuffer)
	{
		this->outputBuffer = malloc(outputBufferSize* sizeof(uint8_t));
		
		if(not this->outputBuffer)
		{
			setLastError(this);
			close(this->sockfd);
			this->sockfd = -1;
			return false;
		}
	}
	
	const int requestSize = snprintf((char *)this->outputBuffer, outputBufferSize,
	"GET %s?%s&%s&PTZCONTROL=CH%u\r\n"
	"User-Agent: %s\r\n"
	"Accept: */*\r\n"
	"Host: %s\r\n"
	"Connection: Close\r\n"
	"\r\n",
	HTTP_PATH,
	this->username,
	this->password,
	this->channel,
	__FILE__,
	this->hostname);
	
	if(requestSize == outputBufferSize)
	{
		return false;
	}
	
	this->outputLength = (uint_fast32_t)requestSize;
	this->bytesWritten = 0;
	
	static const unsigned int BASE_BUFFER_SIZE = 16383*2;

	if(not this->pongReceiveBuffer)
	{
		this->pongReceiveBuffer = malloc(BASE_BUFFER_SIZE);
		this->pongReceiveBufferSize = BASE_BUFFER_SIZE;
	}
	bzero(this->pongReceiveBuffer,this->pongReceiveBufferSize);
	
	if(not this->pingReceiveBuffer)
	{
		this->pingReceiveBuffer = malloc(BASE_BUFFER_SIZE);
		this->pingReceiveBufferSize = BASE_BUFFER_SIZE;
	}
	bzero(this->pingReceiveBuffer,this->pingReceiveBufferSize);
	
	this->ping = true;
	this->activeReceiveBuffer = & this->pingReceiveBuffer;
	this->activeReceiveBufferSize = & this->pingReceiveBufferSize;
	this->activeReceiveBufferBytesReceived = 0;
	
	http_parser_init(& this->parser,HTTP_RESPONSE);
	
	return true;
}

void rsva11001_connection_destroy(rsva11001_connection * const this)
{
	
	if(this->sockfd!=-1)
	{
		close(this->sockfd);
	}
	free(this->outputBuffer);
	free(this->pingReceiveBuffer);
	free(this->pongReceiveBuffer);
	
	return;
}

bool rsva11001_connection_process(rsva11001_connection * const this)
{
	assert(this!=NULL);
	assert(this->sockfd!=-1);
	
	if(not this->connected)
	{
		struct pollfd pfd;
		pfd.fd = this->sockfd;
		pfd.events = POLLOUT;
		pfd.revents = 0;
		
		const int numEvents = poll(&pfd,1,0);
		
		if(numEvents < 0)
		{
			setLastError(this);
			return false;
		}
		
		if(numEvents == 0)
		{
			return true;
		}
		
		if(numEvents == 1)
		{
			if(not (pfd.revents & POLLOUT))
			{
				return false;
			}
		}
		
		
		int opt;
		socklen_t optlen = sizeof(opt);
		if(0 != getsockopt(this->sockfd,SOL_SOCKET,SO_ERROR,&opt,&optlen))
		{
			setLastError(this);
			return false;
		}
		
		if(optlen!=sizeof(opt))
		{
			return false;
		}
		
		if(opt!=0)
		{		
			return false;
		}
		
		this->connected = true;
	}
	
	bool writeable = true;
	while(writeable and this->bytesWritten!= this->outputLength)
	{
		const char * src = (const char *)this->outputBuffer + this->bytesWritten;
		size_t amountToWrite = this->outputLength - this->bytesWritten;
		
		assert(amountToWrite!=0);
		const ssize_t sendResult = send(this->sockfd,src,amountToWrite,0);
		
		if(sendResult == 0)
		{
		
			return false;
		}
		
		if(sendResult == -1)
		{
			int error = errno;
			switch(error)
			{
			   //This function sends data until it runs out of data to send or the socket
				//is no longer ready to send data. The enumerated cases reccomend the latter case.
				//man page says recv returns EAGAIN or EWOULDBLOCK. These generate duplicate case values
				//on some platforms
				case EAGAIN:
				#if (EAGAIN != EWOULDBLOCK)
				case EWOULDBLOCK:
				#endif				
					writeable = false;
					continue;
				default:
					setLastError(this);
					return false;
			}
		}
	
		
		this->bytesWritten += (uint_fast32_t)sendResult;
	}
	
	bool readable = true;
	
	while(readable)
	{
		static const unsigned int BUFFER_SIZE = 16383;
		uint8_t buffer[BUFFER_SIZE];
		
		ssize_t recvResult = recv(this->sockfd,buffer,BUFFER_SIZE,0);
		
		if(recvResult == 0)
		{
			return false;
		}
		
		if(recvResult == -1)
		{
			int error = errno;
			switch(error)
			{
				//This function receives data forever or until the socket has no more.
				//The enumerated cases reccomend the latter case.
				//man page says recv returns EAGAIN or EWOULDBLOCK. These generate duplicate case values
				//on some platforms
				case EAGAIN:
				#if (EAGAIN != EWOULDBLOCK)
				case EWOULDBLOCK:
				#endif				
					readable = false;
					continue;
				default:
					setLastError(this);
					return false;
			}
		}
		
		const uint_fast16_t amountReceived = (uint_fast32_t)recvResult;
		
		http_parser_execute(& this->parser, & this->settings, buffer,amountReceived);
		
	}
	
	return true;
}


bool rsva11001_connection_close(rsva11001_connection * const this)
{
	//If already closed, do nothing
	if(this->sockfd == -1)
	{
		return true;
	}
	
	if(0 != close(this->sockfd))
	{
		setLastError(this);
		return false;
	}
	
	this->sockfd = -1;
	
	return true;
}

int rsva11001_connection_getFd(rsva11001_connection const * const this)
{
	return this->sockfd;
}


