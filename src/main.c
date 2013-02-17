#include "inttypes.h"
#include "iso646.h"
#include "rsva11001.h"
#include "stdio.h"


int main(){
	
	rsva11001_connection * const conn = rsva11001_connection_malloc();

	if(not conn)
	{
		
		return 1;
	}
	
	if( not rsva11001_connection_configure(conn,"192.168.12.168","1111","1111",1))
	{
		printf("%s\n",rsva11001_connection_getLastError(conn));
		return 1;
	}
	
	if( not rsva11001_connection_open(conn))
	{
		printf("%s\n",rsva11001_connection_getLastError(conn));
		return 1;
	}
	
	uint_fast32_t imageSize = 0;
	uint8_t buffer[20000];
	
	while(imageSize == 0)
	{
		if( not rsva11001_connection_process(conn))
		{
			printf("%s\n",rsva11001_connection_getLastError(conn));
			return 1;		
		}
		
		imageSize = sizeof(buffer);
		if( not rsva11001_connection_getLatestImage(conn,buffer, &imageSize))
		{
			printf("%s\n",rsva11001_connection_getLastError(conn));
			return 1;		
		}		
	}
	
	printf( PRIuFAST32 " bytes\n" , imageSize);

	rsva11001_connection_close(conn);

	free(conn);
	return 0;
}
