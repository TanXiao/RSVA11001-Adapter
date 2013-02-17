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
#include "errno.h"
#include "string.h"

int main(){
	int retVal = 0;
	
	sigset_t handledSignals;
	sigemptyset(&handledSignals);
	sigaddset(&handledSignals,SIGINT);
	sigaddset(&handledSignals,SIGHUP);
	sigaddset(&handledSignals,SIGQUIT);
	sigaddset(&handledSignals,SIGTERM);
	
	const int signalFd = signalfd(-1,&handledSignals,SFD_NONBLOCK|SFD_CLOEXEC);
	
	if(0!=sigprocmask(SIG_BLOCK,&handledSignals,NULL))
	{
		return 1;
	}
	
	struct{
		struct signalfd_siginfo info;
		uint_fast32_t amountReceived;
	}lastSignal;
	
	lastSignal.amountReceived = 0;
	
	const int epollfd = epoll_create1(EPOLL_CLOEXEC);
	
	if(epollfd==-1)
	{
		return 1;
	}
	
	unsigned int numCameras = 4;
	static const unsigned int MAX_NUM_CAMERAS = 16;
	
	
	rsva11001_connection * conns[MAX_NUM_CAMERAS];
	bzero(conns,sizeof(conns));
	
	for(unsigned int i = 0 ; i < numCameras ; ++i)
	{
		rsva11001_connection * const conn = rsva11001_connection_malloc();

		if(not conn)
		{			
			return 1;
		}
		
		if( not rsva11001_connection_configure(conn,"192.168.12.168","1111","1111",i+1))
		{
			printf("%s\n",rsva11001_connection_getLastError(conn));
			return 1;
		}
		
		conns[i] = conn;
	}
	
	rsva11001_connection * activeCamera = NULL;
	unsigned int cameraIterator = 0;
	
	enum{
		SIGFD_ID = 1,
		CAMERA_ID = 0
	};

	{
		struct epoll_event ev;
		ev.events = EPOLLET | EPOLLIN;
		ev.data.u32 = SIGFD_ID;
		if(0 != epoll_ctl(epollfd,EPOLL_CTL_ADD,signalFd,&ev))
		{
			return 1;
		}
	}
	bool shutdown = false;
	while(retVal == 0 and not shutdown)
	{
		if(not activeCamera)
		{
			activeCamera = conns[cameraIterator];
			++cameraIterator;
			
			if(cameraIterator == numCameras)
			{
				cameraIterator = 0;
			}
			
			assert(activeCamera!=NULL);
			if ( not rsva11001_connection_open(activeCamera) )
			{
				printf("%s\n",rsva11001_connection_getLastError(activeCamera));
				retVal = 1;
				continue;
			}
			
			const int fd = rsva11001_connection_getFd(activeCamera);
			struct epoll_event ev;
			ev.events = EPOLLET | EPOLLIN | EPOLLOUT;
			ev.data.u32 = CAMERA_ID;
			if(0 != epoll_ctl(epollfd, EPOLL_CTL_ADD,fd,&ev))
			{
				retVal = 1;
				continue;
			}
			
			printf("Switched to camera #%u\n" , cameraIterator);
		}
		
		static const  int MAX_EVENTS = 4;
		struct epoll_event events[MAX_EVENTS];
		int numEvents = MAX_EVENTS;
		
		while(numEvents == MAX_EVENTS){
			
			numEvents = epoll_wait(epollfd,events,MAX_EVENTS,-1);
			
			if(numEvents == -1)
			{
				retVal = 1;
				break;
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
							if(not rsva11001_connection_close(activeCamera))
							{
								retVal =1;
								break;
							}
							activeCamera = NULL;
							break;
						}
						
						if( not rsva11001_connection_process(activeCamera))
						{
							printf("%s\n",rsva11001_connection_getLastError(activeCamera));
							retVal = 1;
							break;
						}
						
						uint8_t buffer[30000];
						uint_fast32_t imageSize = sizeof(buffer);
						
						if( not rsva11001_connection_getLatestImage(activeCamera,buffer, &imageSize))
						{
							printf("%s\n",rsva11001_connection_getLastError(activeCamera));
							retVal = 1;
						}		
						
						if(imageSize!=0)
						{
							printf( "%" PRIuFAST32 " bytes from camera #%u\n" , imageSize,cameraIterator );
							
							if(not rsva11001_connection_close(activeCamera))
							{
								retVal =1;
								break;
							}
							
							activeCamera = NULL;
							
							char filename[128];
							
							snprintf(filename,sizeof(filename),"camera_%u.jpg",cameraIterator);
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
										retVal = 1;
										break;
								}
							}
							
							//Create file to contain output
							const int fd = open(filename,O_CLOEXEC|O_CREAT|O_RDWR,S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
							
							//Check for error on opening file
							if(fd == -1)
							{
								retVal = 1;
								break;
							}
							
							//Move to end of file
							if (-1 == lseek(fd,imageSize-1,SEEK_SET))
							{
								retVal =1;
								break;
							}
							
							//Write a single byte to extend the filesize
							if(1!=write(fd,"",1))
							{
								retVal =1;
								break;
							}
							
							//mmap the file
							uint8_t const * const dst = mmap(NULL,imageSize,PROT_READ| PROT_WRITE,MAP_SHARED,fd,0);
							
							if((void*)dst == MAP_FAILED)
							{
								int error = errno;
								printf("%s\n",strerror(error));
								retVal = 1 ;
								break;
							}
							
							//Close the file descriptor
							if(0!=close(fd))
							{
								retVal = 1;
								break;
							}
							
							memcpy((char*)dst,(char*)buffer,imageSize);
							
							if(0!=munmap(dst,imageSize))
							{
								retVal = 1;
								break;
							}
							
							
						}
						break;
					}
					case SIGFD_ID:
					{
						shutdown = true;
						break;
					}
					default:
						retVal =1;
						break;
				}
			}
			
		} 

	}
	
	for(unsigned int i = 0 ; i < numCameras ; ++i)
	{
		rsva11001_connection * const camera = conns[i];
		
		if( not rsva11001_connection_close(camera))
		{
			printf("%s\n",rsva11001_connection_getLastError(camera));
			return 1;
		}
		
		rsva11001_connection_destroy(camera);
		
		free(camera);
		
		conns[i] = NULL;
	}
	
	close(signalFd);
	close(epollfd);


	return retVal;
}
