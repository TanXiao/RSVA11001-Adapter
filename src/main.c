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

void setupSignalFd(void)
{
	sigemptyset(&globals.handledSignals);
	sigaddset(&globals.handledSignals,SIGINT);
	sigaddset(&globals.handledSignals,SIGHUP);
	sigaddset(&globals.handledSignals,SIGQUIT);
	sigaddset(&globals.handledSignals,SIGTERM);
	
	//block these signals
	if(0!=sigprocmask(SIG_BLOCK,&globals.handledSignals,NULL))
	{
		terminateOnErrno(sigprocmask);
	}
	
	//Create an FD to listen for those signals
	globals.signalFd = signalfd(-1,&globals.handledSignals,SFD_NONBLOCK|SFD_CLOEXEC);
	
	if(globals.signalFd == -1 )
	{
		terminateOnErrno(signalfd);
	}
	
	globals.lastSignal.amountReceived = 0;
}

void setupEpollFd(void)
{
	globals.epollFd = epoll_create1(EPOLL_CLOEXEC);
	
	if(globals.epollFd == -1)
	{
		terminateOnErrno(epoll_create1);
	}

	struct epoll_event ev;
	ev.events = EPOLLET | EPOLLIN;
	ev.data.u32 = SIGFD_ID;
	if(0 != epoll_ctl(globals.epollFd,EPOLL_CTL_ADD,globals.signalFd,&ev))
	{
		terminateOnErrno(epoll_ctl);
	}
		
}

void setupCameraConnections(void)
{
	bzero(globals.conns,sizeof(globals.conns));
	globals.cameraIterator = 0;
	
	for(unsigned int i = 0 ; i < globals.numCameras ; ++i)
	{
		rsva11001_connection * const conn = rsva11001_connection_malloc();

		if(not conn)
		{			
			terminate("Failed to malloc memory for connection object");
		}
		
		if( not rsva11001_connection_configure(conn,"192.168.12.168","1111","1111",i+1))
		{
			terminate("Bad camera connection configuration");
		}
		globals.conns[i] = conn;
	}
	
}

void readConfiguration(int argc, char * argv[])
{

	const char * const  RSVA11001_HOST = getenv("RSVA11001_HOST");
	const char * const  RSVA11001_PASSWORD = getenv("RSVA11001_PASSWORD");
	const char * const  RSVA11001_USERNAME = getenv("RSVA11001_USERNAME");
	const char * const  RSVA11001_NUM_CAMERAS = getenv("RSVA11001_NUM_CAMERAS");
	
	if( not RSVA11001_HOST)
	{
		terminate("Missing env. var. RSVA11001_HOST");
	}
	
	if ( not RSVA11001_PASSWORD)
	{
		terminate("Missing env. var. RSVA11001_PASSWORD");
	}
	
	if ( not RSVA11001_HOST)
	{
		terminate("Missing env. var. RSVA11001_HOST");
	}
	
	if ( not RSVA11001_NUM_CAMERAS)
	{
		terminate("Missing env. var. RSVA11001_NUM_CAMERAS");
	}

	strcpy(globals.host,RSVA11001_HOST);
	strcpy(globals.password,RSVA11001_PASSWORD);
	strcpy(globals.username,RSVA11001_USERNAME);
	
	errno = 0;
	char * endptr;
	
	unsigned long const v = strtoul(RSVA11001_NUM_CAMERAS,&endptr,10);
	
	if (v == ULONG_MAX and errno !=0)
	{
		terminate("Env. var. RSVA11001_NUM_CAMERAS is NaN");
	}
	
	if (v >=MAX_NUM_CAMERAS or v > UINT_MAX)
	{
		terminate("Env. var. RSVA11001_NUM_CAMERAS is too large");
	}
	
	globals.numCameras = (unsigned int) v;
}

void openNextCameraConnection(void)
{
	globals.activeCamera = globals.conns[globals.cameraIterator++];
	
	if(globals.cameraIterator >= globals.numCameras)
	{
		globals.cameraIterator = 0;
		
	}

	assert(globals.activeCamera != NULL);
	if(not rsva11001_connection_open(globals.activeCamera))
	{
		terminate(rsva11001_connection_getLastError(globals.activeCamera));
	}
	
	const int fd = rsva11001_connection_getFd(globals.activeCamera);
	struct epoll_event ev;
	ev.events = EPOLLET | EPOLLIN | EPOLLOUT;
	ev.data.u32 = CAMERA_ID;
	if(0 != epoll_ctl(globals.epollFd, EPOLL_CTL_ADD,fd,&ev))
	{
		terminateOnErrno(epoll_ctl);
	}
	
	printf("Switched to camera #%u\n" , globals.cameraIterator);

	
	
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

void writeImageFile(uint8_t const * const buffer, uint_fast32_t imageSize)
{
	assert(buffer!=NULL);
	
	char filename[128];
		
	snprintf(filename,sizeof(filename),"camera_%u.jpg",globals.cameraIterator);
	filename[sizeof(filename)-1]='\0';
	
	//Unlink existing file
	if(0!=unlink(filename))
	{
		const int error = errno;
		switch(error)
		{
			//Means file didn't exist, not a problem
			case ENOENT:
				break;
			default:
				terminateOnErrno(unlink);
		}
	}
	
	//Create file to contain output
	const int fd = open(filename,O_CLOEXEC|O_CREAT|O_RDWR,S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	
	//Check for error on opening file
	if(fd == -1)
	{
		terminateOnErrno(open);
	}
	
	//Move to end of file
	if (-1 == lseek(fd,imageSize-1,SEEK_SET))
	{
		terminateOnErrno(lseek);
	}
	
	//Write a single byte to extend the filesize
	if(1!=write(fd,"",1))
	{
		terminateOnErrno(write);
	}
	
	//mmap the file
	uint8_t * const dst = mmap(NULL,imageSize,PROT_READ| PROT_WRITE,MAP_SHARED,fd,0);
	
	//Check for mmap failure
	if((void*)dst == MAP_FAILED)
	{
		terminateOnErrno(mmap);	
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
}

void processActiveCamera(void)
{
	assert(globals.activeCamera!=NULL);
	
	if( not rsva11001_connection_process(globals.activeCamera))
	{
		terminate(rsva11001_connection_getLastError(globals.activeCamera));
	}
	
	uint8_t buffer[30000];
	uint_fast32_t imageSize = sizeof(buffer);
	
	if( not rsva11001_connection_getLatestImage(globals.activeCamera,buffer, &imageSize))
	{
		terminate(rsva11001_connection_getLastError(globals.activeCamera));
	}		
	
	if(imageSize!=0)
	{
		printf( "%" PRIuFAST32 " bytes from camera #%u\n" , imageSize,globals.cameraIterator );
		
		if(not rsva11001_connection_close(globals.activeCamera))
		{
			terminate(rsva11001_connection_getLastError(globals.activeCamera));
		}
		
		writeImageFile(buffer,imageSize);
		
		globals.activeCamera = NULL;
	}
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

void reactor(void){
	
	bool shutdown  = false;
	while(not shutdown)
	{
		if(not globals.activeCamera)
		{
			openNextCameraConnection();
		}
		
		static const  int MAX_EVENTS = 4;
		struct epoll_event events[MAX_EVENTS];
		int numEvents = MAX_EVENTS;
		
		while(numEvents == MAX_EVENTS)
		{		
			numEvents = epoll_wait(globals.epollFd,events,MAX_EVENTS,-1);
			
			if(numEvents == -1)
			{
				const int error = errno;
				
				//This just seems to happen. It can be safely ignored
				if (errno == EINTR)
				{
					continue;
				}
				
				terminateOnErrno(epoll_wait);
			}
			
			for(unsigned int i = 0 ; i < numEvents; ++i)
			{
				const uint32_t id = events[i].data.u32;
				
				switch(id)
				{
					case CAMERA_ID:
					{
						uint32_t const ev = events[i].events;
						
						if( (ev & EPOLLERR) or (ev & EPOLLHUP) )
						{
							if(not rsva11001_connection_close(globals.activeCamera))
							{
								terminate("Got ERR on HUP on camera connection socket");
							}
							globals.activeCamera = NULL;
							break;
						}
						
						processActiveCamera();
						break;
					}
					case SIGFD_ID:
					{
						shutdown = true;
						break;
					}
					
					case SCGI_ID:
					{
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
							char const * errmsg = sg_server_getLastError(globals.server);
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
		return false;
	}
	
	sg_server_setSocketPath(globals.server,"/tmp/rsva11001adapter.socket");
	
	if( not sg_server_init(globals.server) )
	{
		return false;
	}
	
	const int fd = sg_server_getFileDescriptor(globals.server);
	if(fd < 0)
	{
		return false;
	}
	
	struct epoll_event ev;
	bzero(&ev,sizeof(ev));
	
	ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
	ev.data.u32 = SCGI_ID;
	
	if(0!=epoll_ctl(globals.epollFd,EPOLL_CTL_ADD,fd,&ev))
	{
		terminateOnErrno("epoll_ctl");
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

	bzero(&globals,sizeof(globals));
	readConfiguration(argc,argv);
	
	setupSignalFd();
	setupEpollFd();
	
	setupCameraConnections();

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
