#include "stdio.h"
#include "iso646.h"

#ifndef __rsva11001adapter_log
#define __rsva11001adapter_log

typedef enum {
	RSVA11001ADAPTER_FATAL,
	RSVA11001ADAPTER_ERROR,
	RSVA11001ADAPTER_WARN,
	RSVA11001ADAPTER_INFO,
	RSVA11001ADAPTER_DEBUG,
	RSVA11001ADAPTER_TRACE
}rsva11001adapter_logLevel;

typedef struct
{
	int logFd;
	rsva11001adapter_logLevel logLevel;
}rsva11001adapter_logger;

static inline char const * rsva11001adapter_logLevel_toString(rsva11001adapter_logLevel ll)
{
	switch(ll)
	{
		case RSVA11001ADAPTER_FATAL:
			return "FATAL";
		case RSVA11001ADAPTER_ERROR:
			return "ERROR";
		case RSVA11001ADAPTER_WARN:
			return "WARN";
		case RSVA11001ADAPTER_INFO:
			return "INFO";
		case RSVA11001ADAPTER_DEBUG:
			return "DEBUG";
		case RSVA11001ADAPTER_TRACE:
			return "TRACE";
		default:
			return "???";
	}
}


static inline void rsva11001adapter_beginLogLine(int fd, const char * const file, int line, const char * const func,rsva11001adapter_logLevel ll )
{
	dprintf(fd,"%s:%i:%s - %s - ",file,line,func,rsva11001adapter_logLevel_toString(ll));
}

#define RSVA11001ADAPTER_LOG(_this,_logLevel,_msg) do{ \
if(_this->logFd != -1 and _this->logLevel >= _logLevel){\
	rsva11001adapter_beginLogLine(_this->logFd, __FILE__,__LINE__,__func__,_logLevel);\
	dprintf(_this->logFd,"%s\n",_msg);\
} }while(false);

#define RSVA11001ADAPTER_LOGFMT(_this,_logLevel,_fmt,...) do{ \
if(_this->logFd != -1 and _this->logLevel >= _logLevel){\
	rsva11001adapter_beginLogLine(_this->logFd, __FILE__,__LINE__,__func__,_logLevel);\
	dprintf(_this->logFd,_fmt"\n", ##__VA_ARGS__); \
} }while(false);

#define RSVA11001ADAPTER_LOGERRNO(_this,_cause) do{\
if(_this->logFd != -1 and _this->logLevel >= RSVA11001ADAPTER_ERROR){\
	char buf[128];\
	const int error = errno;\
	const char * const str = strerror_r(error, buf,sizeof(buf));\
	rsva11001adapter_beginLogLine(_this->logFd, __FILE__,__LINE__,__func__,RSVA11001ADAPTER_ERROR);\
	dprintf(_this->logFd,"Caused by:%s;",#_cause);\
	dprintf(_this->logFd,"%s\n",str);\
} }while(false);

#endif
