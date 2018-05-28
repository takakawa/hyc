#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<getopt.h>
#include<unistd.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<ev.h>
#include<fcntl.h>
#include"url.h"

#define ERROR_ON(fd)  if(fd < 0) return -1;
#define DEBUG_PRINT(format,args...)   fprintf(stdout,"[DEBUG]%s:%d->"format,__func__,__LINE__,##args)
extern char *optarg;
extern int optopt;
extern int opterr;

int new_tcp_connection_ev(char * ip, unsigned int port,struct ev_loop *main_loop);

struct Host
{
   char *ip;
   unsigned int  port;
   unsigned int  n;
   char *path;
};

struct Host h ;

void setaddress(const char* ip,int port,struct sockaddr_in* addr)
{  
    memset(addr,0, sizeof(*addr));  
    addr->sin_family=AF_INET;  
    inet_pton(AF_INET,ip,&(addr->sin_addr));  
    addr->sin_port=htons(port);  
}
int setnonblock(int sockfd)
{
    if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFD, 0)|O_NONBLOCK) == -1) 
    {
        return -1;
    }
    return 0;
}
static int new_tcp_connection(const char *ip, unsigned int port)
{

    int ret;
    struct sockaddr_in addr;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    ERROR_ON(fd);
    setnonblock(fd);
    setaddress(ip, port , &addr);
    ret = connect(fd, (struct sockaddr*)(&addr), sizeof(addr));
/*    
    if(ret < 0)
    {
       DEBUG_PRINT("Connect Error:%d\n",ret); 
       return -2;
    }
    
    */

    return fd;
}

char * build_http_request(const char* method, char *url,const char *body)
{
       char * buf = malloc(4096);

       if (buf == NULL)
       {
           return NULL; 
       }

       url_data_t *parsed = url_parse(url);
       if(parsed == NULL)
       {
           return NULL; 
       }

       DEBUG_PRINT("input path:%s\n",url);
       DEBUG_PRINT("host:%s\n",parsed->host);
       DEBUG_PRINT("path:%s\n",parsed->path);

       strcpy(buf,method);
       strcat(buf," ");
       strcat(buf,parsed->path);
       strcat(buf," HTTP/1.1\r\n");
       strcat(buf,"User-Agent: hyc\r\n");
       strcat(buf,"Accept: */*\r\n");

       strcat(buf,"Host: ");
       strcat(buf, parsed->host);
       strcat(buf,"\r\n");

       if (strcmp("GET",method) == 0)
       {
	    strcat(buf,"\r\n");
       }
       else if(strcmp("POST",method) ==0)
       {
	     strcat(buf,"Content-Length: ");
	     char contentlen[10] = {0};
	     sprintf(contentlen,"%d",strlen(body));
	     strcat(buf,contentlen);

	     strcat(buf,"\r\n\r\n");
	     strcat(buf,body);
       }
       else
       {
	       DEBUG_PRINT("Current not supoorted method","");
	       free(buf);
	       free(parsed);
	       return NULL;
       }
       free(parsed);
       return buf;
}
  
void http_read(struct ev_loop *loop, ev_io *stat, int events)
{
   static int request_cnt  = 0;
   char buf[1000] = {'\0'};
   int n = read(stat->fd,buf,1000);
   if (n == 0)
   {
	DEBUG_PRINT("remote closed client\n","") ;
	close(stat->fd);
	ev_io_stop(loop, stat);
	free(stat);

	//struct Host *h = (struct Host *) stat->data;
	DEBUG_PRINT("reconnect to  client:%s:%d\n",h.ip,h.port) ;
	new_tcp_connection_ev(h.ip, h.port ,loop);
	request_cnt  ++;
   }
   else
   {
	   request_cnt ++; 

	   if(request_cnt > h.n)
	   {
		close(stat->fd);
		ev_io_stop(loop, stat);
		free(stat);
		DEBUG_PRINT("run %d times quit",request_cnt);
     		return; 
	   }
	   DEBUG_PRINT("http_read: %d\n",request_cnt);
	   DEBUG_PRINT("recv:%s\n",buf);
	   DEBUG_PRINT("path:%s\n",h.path);
	   char *req = build_http_request("GET",h.path,"");
	   write(stat->fd,req,strlen(req));
	   free(req);
   }
}
int new_tcp_connection_ev(char * ip, unsigned int port,struct ev_loop *main_loop)
{
    int  fd = new_tcp_connection(ip,port);

    if (fd < 0 )
    {
       DEBUG_PRINT("fd is %d",fd) ;
       return -1;
    }

    struct ev_io * http_readable = malloc(sizeof(struct ev_io));

    if (!http_readable)
    {
        
       DEBUG_PRINT("ev_io malloc err %d",http_readable) ;
       return -1;
    }

    ev_io_init (http_readable,http_read,fd,EV_READ);
    //http_readable.data = &h;
    ev_io_start(main_loop,http_readable);


    char *req = build_http_request("GET",h.path,"");
    write(fd,req,strlen(req));
    free(req);
}

int main(int argc , char ** argv)
{

    int i = 0;
    int n = 10000;
    unsigned int  concurrent  = 0;
    char * url = NULL;
    int c ;

    opterr = 0;
    while( (c= getopt(argc, argv , "c:u:n:")) != -1 )
    {
   	switch(c)
	    {
	   	case 'c': 
			concurrent = atoi(optarg);
			break;
	   	case 'n': 
			n = atoi(optarg);
			break;
	   	case 'u': 
			url = optarg;
			break;
	    
		default:
			abort();
	    }	
    
    }


    if ( concurrent == 0 || url == NULL)
    {
       DEBUG_PRINT("args wrong","");
       exit(-1); 
    }

    url_data_t *parsed = url_parse(url);
    if (parsed == NULL)
    {
       DEBUG_PRINT("URL WRONG:%s\n",url); 
       exit(-1);
    }
    DEBUG_PRINT("hostname:%s\n",parsed->host);
    DEBUG_PRINT("port:%s\n",parsed->port);
    DEBUG_PRINT("path:%s\n",parsed->path);

    h.ip = parsed->host;
    h.port = atoi(parsed->port);
    h.path = url;
    h.n = n;

    DEBUG_PRINT("%s:%d concurrent:%d\n",h.ip,h.port,concurrent);

    struct ev_loop *main_loop = ev_default_loop(0);

    for(i = 0; i< concurrent; i++)
    {
	    new_tcp_connection_ev(h.ip, h.port ,main_loop);
    }

    ev_run(main_loop, 0);
    url_free(parsed);
}
