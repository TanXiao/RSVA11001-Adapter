#define _GNU_SOURCE
#include "errno.h"
#include "inttypes.h"
#include "iso646.h"
#include "rsva11001.h"
#include "stdio.h"
#include "unistd.h"
#include "sys/signalfd.h"
#include "sys/epoll.h"
#include "signal.h"
#include "strings.h"
#include "assert.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "stdlib.h"
#include "string.h"
#include "sandgrouse/server.h"
#include "sandgrouse/connection.h"
#include "sandgrouse/queryStringParser.h"
#include "sandgrouse/util.h"
#include "limits.h"
#include "log.h"

enum{
	MAX_NUM_CAMERAS=8,
	MAX_PENDING_REQUESTS=64,
	SCGI_ID = 2,
	SIGFD_ID = 1,
	CAMERA_ID = 0
};

struct
{
	sg_server * server;
	rsva11001_connection * conns[MAX_NUM_CAMERAS];
	rsva11001_connection * activeCamera;
	unsigned int cameraIterator;
	unsigned int numCameras;
	
	struct
	{
		uint8_t * 	data;
		uint_fast32_t size;
	}lastImage[MAX_NUM_CAMERAS];
	
	struct
	{
		sg_connection * connections[MAX_PENDING_REQUESTS];
		uint_fast32_t numPending;
	}pendingRequests[MAX_NUM_CAMERAS];
	
	char username[64];
	char password[64];
	char host[256];
	
	int signalFd;
	sigset_t handledSignals;
	struct{
		struct signalfd_siginfo info;
		uint_fast32_t amountReceived;
	}lastSignal;
	
	int epollFd;
	
}globals;

static rsva11001adapter_logger * logger;

#define terminateOnErrno(_source)do{\
char buf[256];\
const int error = errno;\
const char * const str = strerror_r(error, buf,sizeof(buf));\
printf("Call to %s @ %s:%i:%s generated %s, terminating.\n",#_source,__FILE__,__LINE__,__func__,str); \
exit(1);\
}while(0);


#define terminate(msg)do{\
printf("Failure @ %s:%i:%s reason %s, terminating.\n",__FILE__,__LINE__,__func__,msg); \
exit(1);\
}while(0);

bool setupSignalFd(void)
{
	sigemptyset(&globals.handledSignals);
	sigaddset(&globals.handledSignals,SIGINT);
	sigaddset(&globals.handledSignals,SIGHUP);
	sigaddset(&globals.handledSignals,SIGQUIT);
	sigaddset(&globals.handledSignals,SIGTERM);
	
	//block these signals
	if(0!=sigprocmask(SIG_BLOCK,&globals.handledSignals,NULL))
	{
		RSVA11001ADAPTER_LOGERRNO(logger,sigprocmask);
		return false;
	}
	
	//Create an FD to listen for those signals
	globals.signalFd = signalfd(-1,&globals.handledSignals,SFD_NONBLOCK|SFD_CLOEXEC);
	
	if(globals.signalFd == -1 )
	{
		RSVA11001ADAPTER_LOGERRNO(logger,signalfd);
		return false;
	}
	
	globals.lastSignal.amountReceived = 0;
	return true;
}

bool setupEpollFd(void)
{
	globals.epollFd = epoll_create1(EPOLL_CLOEXEC);
	
	if(globals.epollFd == -1)
	{
		RSVA11001ADAPTER_LOGERRNO(logger,epoll_create1);
		return false;
	}

	struct epoll_event ev;
	ev.events = EPOLLET | EPOLLIN;
	ev.data.u32 = SIGFD_ID;
	if(0 != epoll_ctl(globals.epollFd,EPOLL_CTL_ADD,globals.signalFd,&ev))
	{
		RSVA11001ADAPTER_LOGERRNO(logger,epoll_ctl);
		return false;
	}
	
	return true;
		
}

bool setupCameraConnections(void)
{
	bzero(globals.conns,sizeof(globals.conns));
	globals.cameraIterator = 0;
	
	for(unsigned int i = 0 ; i < globals.numCameras ; ++i)
	{
		rsva11001_connection * const conn = rsva11001_connection_malloc();

		if(not conn)
		{			
			RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Failed to malloc memory for connection object");
			return false;
		}
		
		if( not rsva11001_connection_configure(conn,globals.host,globals.username,globals.password,i+1))
		{			
			RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Camera configuration invalid");
			return false;
		}

		globals.conns[i] = conn;
	}
	
	return true;
	
}

bool readConfiguration(int argc, char * argv[])
{

	const char * const  RSVA11001_HOST = getenv("RSVA11001_HOST");
	const char * const  RSVA11001_PASSWORD = getenv("RSVA11001_PASSWORD");
	const char * const  RSVA11001_USERNAME = getenv("RSVA11001_USERNAME");
	const char * const  RSVA11001_NUM_CAMERAS = getenv("RSVA11001_NUM_CAMERAS");
	
	if( not RSVA11001_HOST)
	{
		RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Missing env. var. RSVA11001_HOST");
		return false;
	}
	
	if ( not RSVA11001_PASSWORD)
	{
		RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Missing env. var. RSVA11001_PASSWORD");
		return false;
	}
	
	if ( not RSVA11001_HOST)
	{
		RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Missing env. var. RSVA11001_HOST");
		return false;
	}
	
	if ( not RSVA11001_NUM_CAMERAS)
	{
		RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Missing env. var. RSVA11001_NUM_CAMERAS");
		return false;
	}

	strcpy(globals.host,RSVA11001_HOST);
	strcpy(globals.password,RSVA11001_PASSWORD);
	strcpy(globals.username,RSVA11001_USERNAME);
	
	errno = 0;
	char * endptr;
	
	unsigned long const v = strtoul(RSVA11001_NUM_CAMERAS,&endptr,10);
	
	if (v == ULONG_MAX and errno !=0)
	{
		RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Env. var. RSVA11001_NUM_CAMERAS is NaN");
		return false;
	}
	
	if (v >=MAX_NUM_CAMERAS or v > UINT_MAX)
	{
		RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Env. var. RSVA11001_NUM_CAMERAS is too large");
		return false;
	}
	
	globals.numCameras = (unsigned int) v;
	
	RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_INFO,"Host:%s",globals.host);
	RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_INFO,"Username:%s",globals.username);
	RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_INFO,"Number of Cameras:%u",globals.numCameras);
	return true;
}

bool openNextCameraConnection(void)
{
	globals.activeCamera = globals.conns[globals.cameraIterator++];
	
	if(globals.cameraIterator >= globals.numCameras)
	{
		globals.cameraIterator = 0;
		
	}

	RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_TRACE,"Switched to camera #%u",globals.cameraIterator);	

	assert(globals.activeCamera != NULL);
	if(not rsva11001_connection_open(globals.activeCamera))
	{
		RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_FATAL,"Error from rsva11001_connection_open:%s",rsva11001_connection_getLastError(globals.activeCamera));
		return false;
	}
	
	const int fd = rsva11001_connection_getFd(globals.activeCamera);
	struct epoll_event ev;
	ev.events = EPOLLET | EPOLLIN | EPOLLOUT;
	ev.data.u32 = CAMERA_ID;
	if(0 != epoll_ctl(globals.epollFd, EPOLL_CTL_ADD,fd,&ev))
	{
		RSVA11001ADAPTER_LOGERRNO(logger,epoll_ctl);
		return false;
	}

	return true;
}

bool sendJpeg(sg_connection * conn, uint8_t const * const data, uint_fast32_t size)
{
	
	static const unsigned MAX_NUM_HEADERS = 8;
	unsigned int numHeaders =5;
	
	uint8_t const * headerKeys[MAX_NUM_HEADERS];
	headerKeys[0] = (uint8_t const *)"Content-Type";
	headerKeys[1] = (uint8_t const *)"Status";
	headerKeys[2] = (uint8_t const *)"Content-Length";
	headerKeys[3] = (uint8_t const *)"Connection";
	headerKeys[4] = (uint8_t const *)"Cache-Control";
	
	uint8_t const * headerValues[MAX_NUM_HEADERS];
	headerValues[0] = (uint8_t const *)"image/jpeg";
	headerValues[1] =(uint8_t const *)"200 OK";
	
	char buffer[16];
	if(sizeof(buffer) == snprintf(buffer,sizeof(buffer),"%" PRIuFAST32 ,size) )
	{
		sg_send404(conn);
		return true;
	}
	
	headerValues[2] = (uint8_t const *)buffer;

	headerValues[3] = (uint8_t const*)"Close";
	headerValues[4] = (uint8_t const*)"no-cache";
	
	uint8_t const * const host = sg_connection_getHeader(conn,"HTTP_HOST");
	
	if(host)
	{
		headerKeys[numHeaders] = "Host";
		headerValues[numHeaders] = host;
		numHeaders++;
	}
	
	assert(numHeaders < MAX_NUM_HEADERS);
	if ( not sg_connection_writeResponse(conn,headerKeys,headerValues,numHeaders,data,size) )
	{
		return false;
	}
}

bool writeImageFile(uint8_t const * const buffer, uint_fast32_t imageSize)
{
	assert(buffer!=NULL);
	
	char filename[128];
		
	snprintf(filename,sizeof(filename),"camera_%u.jpg",globals.cameraIterator);
	filename[sizeof(filename)-1]='\0';
	
	RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_TRACE,"Unlinking file:%s",filename);
	//Unlink existing file
	if(0!=unlink(filename))
	{
		const int error = errno;
		switch(error)
		{
			//Means file didn't exist, not a problem
			case ENOENT:
				RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_DEBUG,"File did not exist");
				break;
			default:
				RSVA11001ADAPTER_LOGERRNO(logger,unlink);
				return false;
		}
	}
	
	//Create file to contain output
	RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_TRACE,"Opening file:%s",filename);
	const int fd = open(filename,O_CLOEXEC|O_CREAT|O_RDWR,S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	
	//Check for error on opening file
	if(fd == -1)
	{
		RSVA11001ADAPTER_LOGERRNO(logger,open);
		return false;
	}
	
	//Set the filesize
	const uint_fast32_t filesize = imageSize;
	RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_TRACE,"Setting file size to %" PRIuFAST32,filesize);
	if(0!=ftruncate(fd,filesize))
	{
		RSVA11001ADAPTER_LOGERRNO(logger,ftruncate);
		return false;
	}
	
	//mmap the file
	uint8_t * const dst = mmap(NULL,imageSize,PROT_READ| PROT_WRITE,MAP_SHARED,fd,0);
	
	//Check for mmap failure
	if((void*)dst == MAP_FAILED)
	{
		RSVA11001ADAPTER_LOGERRNO(logger,mmap);
		return false;
	}
	
	//Close the file descriptor
	if(0!=close(fd))
	{
		terminateOnErrno(close);
	}
	
	//Copy the image into the file
	memcpy((char*)dst,(char*)buffer,imageSize);
	
	//Store the last image
	
	//Unmap the existing image if any
	if(globals.lastImage[globals.cameraIterator].data != NULL)
	{
		if(0!=munmap((void*)globals.lastImage[globals.cameraIterator].data,globals.lastImage[globals.cameraIterator].size))
		{
			terminateOnErrno(munmap);
		}
	}
	
	globals.lastImage[globals.cameraIterator].data = dst;
	globals.lastImage[globals.cameraIterator].size = imageSize;
	
	//Fill any outstanding requests
	for(unsigned int i = 0 ; i < globals.pendingRequests[globals.cameraIterator].numPending ; ++i)
	{
		sg_connection * conn = globals.pendingRequests[globals.cameraIterator].connections[i];
		
		assert(conn!=NULL);
		
		sendJpeg(conn,globals.lastImage[globals.cameraIterator].data,globals.lastImage[globals.cameraIterator].size);
		
		globals.pendingRequests[globals.cameraIterator].connections[i] = NULL;
	}

	globals.pendingRequests[globals.cameraIterator].numPending  = 0;
	
	if ( not sg_server_process(globals.server) )
	{
		char const * errmsg = sg_server_getLastError(globals.server);
		terminate(errmsg);
	}
	
	return true;
}

//Return value
// 0 - no error
// -1 - error
// -2 - fatal error
int processActiveCamera(void)
{
	assert(globals.activeCamera!=NULL);
	
	if( not rsva11001_connection_process(globals.activeCamera))
	{
		RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_ERROR,"Error processing camera connection: %s" , rsva11001_connection_getLastError(globals.activeCamera));
		return -1;
	}
	
	uint8_t buffer[30000];
	uint_fast32_t imageSize = sizeof(buffer);
	
	if( not rsva11001_connection_getLatestImage(globals.activeCamera,buffer, &imageSize))
	{
		RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_ERROR,"Error getting latest image from camera connection: %s" , rsva11001_connection_getLastError(globals.activeCamera));
		return -1;
	}		
	
	if(imageSize!=0)
	{
		RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_TRACE,"%" PRIuFAST32 " bytes from camera #%u\n" , imageSize,globals.cameraIterator);

		if(not rsva11001_connection_close(globals.activeCamera))
		{
			RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_FATAL,"Error closing camera connection: %s" , rsva11001_connection_getLastError(globals.activeCamera));			
			return -2;
		}
		
		if( not writeImageFile(buffer,imageSize) )
		{
			RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_ERROR,"Error writing image file");
			return -1;
		}
		
		globals.activeCamera = NULL;
	}
	
	return 0;
}


bool handleNewRequest(sg_connection * conn)
{
	const char * const method = sg_connection_getHeader(conn,"REQUEST_METHOD");
	
	if(method == NULL or 0!= strcmp(method,"GET"))
	{
		sg_send404(conn);
		return true;
	}
	
	const char * const pathInfo = sg_connection_getHeader(conn, "PATH_INFO");
	
	if(pathInfo == NULL)
	{
		sg_send404(conn);
		return true;
	}
	
	if(0!=strcmp("/camera.jpg",pathInfo))
	{
		sg_send404(conn);
		return true;
	}
	

	const char * const queryString = sg_connection_getHeader(conn,"QUERY_STRING");
	
	if(queryString == NULL)
	{
		sg_send404(conn);
		return true;
	}
	
	sg_queryStringParser parser;
	
	if ( not sg_queryStringParser_init(&parser,queryString) )
	{
		sg_send404(conn);
		return true;
	}
	
	unsigned int src;
	
	uint8_t buffer[256];
	uint_fast32_t length = sizeof(buffer);
	
	
	if( not sg_queryStringParser_findFirst(&parser,"source",buffer,&length))
	{
		sg_send404(conn);
		return true;
	}
	
	if ( length == 0)
	{
		src = 0;
	}
	else
	{
		char * endptr;
		errno = 0;
		const unsigned long v = strtoul((const char *)buffer,&endptr,10);
		
		if(v == ULONG_MAX and errno!=0)
		{
			sg_send404(conn);
			return true;
		}
		
		if(*endptr!='\0')
		{
			sg_send404(conn);
			return true;
		}
		
		if(v > UINT_MAX)
		{
			sg_send404(conn);
			return true;
		}
		
		if( v >= globals.numCameras )
		{
			sg_send404(conn);
			return true;
		}
		
		src = (unsigned int)v;
	}
	
	assert(src < globals.numCameras);
	
	length = sizeof(buffer);
	if( not sg_queryStringParser_findFirst(&parser,"wait",buffer,&length))
	{
		sg_send404(conn);
		return true;
	}
	
	//If requested to wait for the latest image, don't fill it now
	if(length == 1 and buffer[0] == '1')
	{
		uint_fast32_t const index = globals.pendingRequests[src].numPending;
		globals.pendingRequests[src].connections[index] = conn;
		globals.pendingRequests[src].numPending++;
	}
	else
	{
		uint8_t const * const imageData = globals.lastImage[src].data;
		uint_fast32_t  const imageSize = globals.lastImage[src].size;
		
		if (imageData == NULL)
		{
			sg_send404(conn);
			return true;
		}
		
		if(not sendJpeg(conn,imageData,imageSize))
		{
			return false;
		}
	}
	
	return true;
}

bool reactor(void){
	
	bool shutdown  = false;
	while(not shutdown)
	{
		if(not globals.activeCamera)
		{
			if( not openNextCameraConnection() )
			{
				RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Failed to open next camera connection");
				shutdown = true;
				continue;
			}
		}
		
		static const  int MAX_EVENTS = 4;
		struct epoll_event events[MAX_EVENTS];
		int numEvents = MAX_EVENTS;
		
		while(numEvents == MAX_EVENTS and globals.activeCamera!=NULL)
		{		
			numEvents = epoll_wait(globals.epollFd,events,MAX_EVENTS,-1);
			
			if(numEvents == -1)
			{
				const int error = errno;
				
				//This just seems to happen. It can be safely ignored
				if (errno == EINTR)
				{
					RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_DEBUG,"Ignoring EINTR from epoll_wait");
					continue;
				}
				
				RSVA11001ADAPTER_LOGERRNO(logger,epoll_wait);
				return false;
			}
			
			RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_TRACE,"Got %i events from epoll_wait",numEvents);
			
			for(unsigned int i = 0 ; i < numEvents; ++i)
			{
				const uint32_t id = events[i].data.u32;
				
				switch(id)
				{
					case CAMERA_ID:
					{
						RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_TRACE,"Got event on camera connection");
						uint32_t const ev = events[i].events;
						
						//Check for error or socket close on camera connection
						if( (ev & EPOLLERR) or (ev & EPOLLHUP) )
						{
							//Not a big deal, just log and close the connection
							RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_ERROR,"Got ERR or HUP on camera connection socket");
							if(not rsva11001_connection_close(globals.activeCamera))
							{
								//If the connection failed to close, there is an issue, shutdown
								shutdown = true;
								RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_FATAL,"Failed to close camera connection: %s",rsva11001_connection_getLastError(globals.activeCamera));
								break;
							}
							globals.activeCamera = NULL;
							break;
						}
						
						switch(processActiveCamera())
						{
							case 0:
								break;
							
							//Error
							case -1:
							{
								//Not a big deal, just log and close the connection
								RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_ERROR,"Error while processing camera connnection");
								if(not rsva11001_connection_close(globals.activeCamera))
								{
									//If the connection failed to close, there is an issue, shutdown
									shutdown = true;
									RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_FATAL,"Failed to close camera connection: %s",rsva11001_connection_getLastError(globals.activeCamera));
									break;
								}
								globals.activeCamera = NULL;
							}
							break;
							//Fatal error
							case -2:
							default:
							{
								shutdown = true;
								RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Failed to processing camera connection");
								break;
							}

						}
						break;
					}
					case SIGFD_ID:
					{
						RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_DEBUG,"Got event on signal fd");
						shutdown = true;
						break;
					}
					
					case SCGI_ID:
					{
						RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_TRACE,"Got event on SCGI connection");
						if ( not sg_server_process(globals.server) )
						{
							char const * errmsg = sg_server_getLastError(globals.server);
							terminate(errmsg);
						}
						//Check for new request						
						sg_connection * conn;
						while( NULL != (conn = sg_server_getNextConnection(globals.server)))
						{
							handleNewRequest(conn);
						}
						if ( not sg_server_process(globals.server) )
						{
							char const * const errmsg = sg_server_getLastError(globals.server);
							terminate(errmsg);
						}

					}
					break;
					default:
						terminate("Unknown ID returned in epoll event");
				}
			}
			
		}
		
	}
	
	return true;
	
}

void cleanupCameraConnections(void)
{
	unsigned int cameraIterator = 0;
	
	for(unsigned int i = 0 ; i < globals.numCameras ; ++i)
	{
		rsva11001_connection * const camera = globals.conns[i];
		
		if( not rsva11001_connection_close(camera))
		{
			terminate(rsva11001_connection_getLastError(camera));
		}
		
		rsva11001_connection_destroy(camera);
		
		free(camera);
		
		globals.conns[i] = NULL;
	}
}

bool setupScgiServer(void)
{
	globals.server = sg_server_malloc();
	
	if(not globals.server)
	{
		RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Failed to malloc sg_server");
		return false;
	}
	
	sg_server_setSocketPath(globals.server,"/tmp/rsva11001adapter.socket");
	
	if( not sg_server_init(globals.server) )
	{
		RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_FATAL,"Failed to initialize sg_server %s", sg_server_getLastError(globals.server));
		free(globals.server);
		return false;
	}
	
	const int fd = sg_server_getFileDescriptor(globals.server);
	if(fd < 0)
	{
		RSVA11001ADAPTER_LOGFMT(logger,RSVA11001ADAPTER_FATAL,"Incoherent File Descriptor from sg_server:%i",fd);
		sg_server_destroy(globals.server);
		free(globals.server);
		return false;
	}
	
	struct epoll_event ev;
	bzero(&ev,sizeof(ev));
	
	ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
	ev.data.u32 = SCGI_ID;
	
	if(0!=epoll_ctl(globals.epollFd,EPOLL_CTL_ADD,fd,&ev))
	{
		RSVA11001ADAPTER_LOGERRNO(logger,epoll_ctl);
		sg_server_destroy(globals.server);
		free(globals.server);
		return false;
	}
	
	return true;
}

void closeScgiServer(void)
{
	sg_server_destroy(globals.server);
	free(globals.server);
	globals.server = NULL;
}

int main(int argc, char * argv[]){

	rsva11001adapter_logger localLogger;
	logger = &localLogger;
	
	//Set the logger's output to stdout
	logger->logFd = fileno(stdout);
	//Set the loglevel
	logger->logLevel = RSVA11001ADAPTER_TRACE;
	
	RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_INFO,"Started");
	
	bzero(&globals,sizeof(globals));
	
	if( not readConfiguration(argc,argv) )
	{
		RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Failed reading configuration");
		return 1;
	}
	
	if( not setupSignalFd() )
	{
		RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Failed setting up signal fd");
		return 1;
	}

	if( not setupEpollFd() )
	{
		RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Failed setting up epoll fd");
		return 1;
	}
	
	if ( not setupCameraConnections() )
	{
		RSVA11001ADAPTER_LOG(logger,RSVA11001ADAPTER_FATAL,"Failed setting up camera connections");
		return 1;
	}
	
	if( not setupScgiServer())
	{
		goto cleanUpFds;
	}

	reactor();

	closeScgiServer();
	
	cleanUpFds:
	close(globals.signalFd);
	close(globals.epollFd);
	
	cleanupCameraConnections();
	
	
	return 0;
}
