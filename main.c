#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/socket.h>
#include<linux/in.h>
#include<ev.h>
#include<fcntl.h>

#define ERROR_ON(fd)  if(fd < 0) return -1;
#define DEBUG_PRINT(format,args...)   fprintf(stdout,"[DEBUG]%s:%d->"format,__func__,__LINE__,##args)

struct Host
{
   char *ip;
   unsigned int  port;
};
void setaddress(const char* ip,int port,struct sockaddr_in* addr){  
    bzero(addr,sizeof(*addr));  
    addr->sin_family=AF_INET;  
    inet_pton(AF_INET,ip,&(addr->sin_addr));  
    addr->sin_port=htons(port);  
}
static int new_tcp_connection(const char *ip, unsigned int port)
{

    int ret;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    ERROR_ON(fd);

    setnonblock(fd);

    struct sockaddr_in addr;
    setaddress(ip, port , &addr);
     connect(fd, (struct sockaddr*)(&addr), sizeof(addr));


    return fd;
}
int setnonblock(int sockfd)
{
    if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFD, 0)|O_NONBLOCK) == -1) 
    {
        return -1;
    }
    return 0;
}
  
void http_read(struct ev_loop *loop, ev_io *stat, int events)
{
   static request_cnt  = 0;
   char buf[1000] = {'\0'};
   int n = read(stat->fd,buf,1000);
   if (n == 0)
   {
      DEBUG_PRINT("remote closed client\n","") ;
      close(stat->fd);
      ev_io_stop(loop, stat);
      struct Host *h = (struct Host *) stat->data;
      DEBUG_PRINT("reconnect to  client:%s:%d\n",h->ip,h->port) ;
      int fd = new_tcp_connection(h->ip, h->port);

    struct ev_io http_readable;
    ev_io_init (&http_readable,http_read,fd,EV_READ);
    http_readable.data = h;
    ev_io_start(loop,&http_readable);

   char *req = "GET / HTTP/1.1\r\nHost: 127.0.0.1:19890\r\n\r\n";
    write(fd,req,strlen(req));
    request_cnt = 0;

   }
   else
   {
	   //DEBUG_PRINT("http_read: %d\n",request_cnt);
	   //DEBUG_PRINT(buf);
	   request_cnt ++; 
	   char *req = "GET / HTTP/1.1\r\nHost: 127.0.0.1:19890\r\n\r\n";
	   write(stat->fd,req,strlen(req));
   }
}
int new_tcp_connection_ev(char * ip, unsigned int port,struct ev_loop *main_loop)
{
    struct Host h ;
    h.ip = ip;
    h.port = port;
    int  fd = new_tcp_connection(ip,port);

    if (fd < 0 )
    {
       DEBUG_PRINT("fd is %d",fd) ;
       return -1;
    }

    struct ev_io http_readable;
    ev_io_init (&http_readable,http_read,fd,EV_READ);
    http_readable.data = &h;
    ev_io_start(main_loop,&http_readable);
}

int main(int argc , char ** argv)
{


    char * ip = NULL;
    unsigned int  port  = 0;
    unsigned int  concurrent  = 0;
    int c ;

    opterr = 0;
    while( (c= getopt(argc, argv , "c:h:p:")) != -1 )
    {
   	switch(c)
	    {
	   	case 'c': 
			concurrent = atoi(optarg);
			break;
	   	case 'h': 
			ip = optarg;
			break;
	   	case 'p': 
			port = atoi(optarg);
			break;
	    
		default:
			abort();
	    }	
    
    }

    DEBUG_PRINT("%s:%d concurrent:%d\n",ip,port,concurrent);
    if (ip == NULL || port == 0 || concurrent == 0)
    {
       DEBUG_PRINT("args wrong");
       exit(-1); 
    }

    struct Host h ;
    h.ip = ip;
    h.port = port;
    int  fd = new_tcp_connection(ip,port);

    if (fd < 0 )
    {
       DEBUG_PRINT("fd is %d",fd) ;
       return -1;
    }

    struct ev_loop *main_loop = ev_default_loop(0);

    struct ev_io http_readable;
    ev_io_init (&http_readable,http_read,fd,EV_READ);
    http_readable.data = &h;
    ev_io_start(main_loop,&http_readable);

    char *req = "GET / HTTP/1.1\r\nHost: 127.0.0.1:19890\r\n\r\n";
    write(fd,req,strlen(req));
    ev_run(main_loop, 0);
}
