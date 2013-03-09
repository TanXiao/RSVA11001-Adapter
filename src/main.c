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

enum{
	MAX_NUM_CAMERAS=16,
	SIGFD_ID = 1,
	CAMERA_ID = 0
};


struct
{
	rsva11001_connection * conns[MAX_NUM_CAMERAS];
	rsva11001_connection * activeCamera;
	unsigned int cameraIterator;
	unsigned int numCameras;
	
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
	globals.numCameras = 4;
	strcpy(globals.host,"192.168.12.168");
	strcpy(globals.password,"1111");
	strcpy(globals.username,"1111");
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
	uint8_t const * const dst = mmap(NULL,imageSize,PROT_READ| PROT_WRITE,MAP_SHARED,fd,0);
	
	if((void*)dst == MAP_FAILED)
	{
		terminateOnErrno(mmap);	
	}
	
	//Close the file descriptor
	if(0!=close(fd))
	{
		terminateOnErrno(close);
	}
	
	memcpy((char*)dst,(char*)buffer,imageSize);
	
	if(0!=munmap((void*)dst,imageSize))
	{
		terminateOnErrno(munmap);
	}
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

int main(int argc, char * argv[]){

	bzero(&globals,sizeof(globals));
	readConfiguration(argc,argv);
	
	setupSignalFd();
	setupEpollFd();
	
	setupCameraConnections();

	sg_server * const server = sg_server_malloc();
	
	if(not server)
	{
		return 1;
	}

	reactor();

	close(globals.signalFd);
	close(globals.epollFd);
	
	cleanupCameraConnections();


	return 0;
}
