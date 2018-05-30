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
#include <errno.h>

#define READ_BUF_LEN (4096)
#define WRITE_BUF_LEN (4096)
#define ERROR_ON(fd)  if(fd < 0) return -1;

#ifdef DEBUG
#define DEBUG_PRINT(format,args...)   fprintf(stdout,"[DEBUG]%s:%d->"format,__func__,__LINE__,##args)
#else
#define DEBUG_PRINT(format,args..) ;
#endif
extern char *optarg;
extern int   optopt;
extern int   opterr;

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
        DEBUG_PRINT("set non block err\n");
        return -1;
    }
    DEBUG_PRINT("set non block ok\n");
    return 0;
}
static int new_tcp_connection(const char *ip, unsigned int port)
{

    int ret;
    struct sockaddr_in addr;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    ERROR_ON(fd);
    ret = setnonblock(fd);

    if (ret < 0)
    {
       return ret; 
    }
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
       char * buf = malloc(WRITE_BUF_LEN);

       if (buf == NULL)
       {
           return NULL; 
       }
       DEBUG_PRINT("enter build http request\n");

       strcpy(buf,method);
       strcat(buf," ");
       strcat(buf,h.path);
       strcat(buf," HTTP/1.1\r\n");
       strcat(buf,"User-Agent: hyc\r\n");
       strcat(buf,"Host: ");
       strcat(buf, h.ip);
       strcat(buf,"\r\n");
       strcat(buf,"Accept: */*\r\n");

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
	       return NULL;
       }
       /* 
       DEBUG_PRINT("request built:\n----------------------------"\
                   "------------------------------\n%s\n---------"\
                   "-------------------------------------------------\n",buf);
       */ 
       return buf;
}
  
void http_read(struct ev_loop *loop, ev_io *stat, int events)
{
   DEBUG_PRINT("enter http_read\n");
   static int request_cnt  = 0;
   char buf[READ_BUF_LEN] = {'\0'};
   int n = read(stat->fd,buf, READ_BUF_LEN);
   if (n == 0)
   {
        DEBUG_PRINT("remote closed client\n","") ;
        close(stat->fd);
        ev_io_stop(loop, stat);
        free(stat);

        //struct Host *h = (struct Host *) stat->data;
        //DEBUG_PRINT("reconnect to  client:%s:%d\n",h.ip,h.port) ;
        //new_tcp_connection_ev(h.ip, h.port ,loop);
   }
   else
   {
	   request_cnt ++; 

	   
       
	   DEBUG_PRINT("http request cnt: %d\n",request_cnt);
	   DEBUG_PRINT("path:%s\n",h.path);
	   DEBUG_PRINT("recv:\n"\
	               "------------------------------------------"\
                   "----------------\n%s\n--------------------"\
                   "--------------------------------------\n",buf);
        
       if(request_cnt >= h.n )
	   {
            close(stat->fd);
            ev_io_stop(loop, stat);
            free(stat);
            DEBUG_PRINT("run %d times quit\n",request_cnt);
            return; 
	   }

	   char *req = build_http_request("GET",h.path,"");
       DEBUG_PRINT("%s\n",req);
	   write(stat->fd,req,strlen(req));
	   free(req);
   }
}
int new_tcp_connection_ev(char * ip, unsigned int port,struct ev_loop *main_loop)
{
    DEBUG_PRINT("enter tcp:%s\n",ip);
    int  fd = new_tcp_connection(ip,port);

    if (fd < 0 )
    {
       DEBUG_PRINT("fd is %d\n",fd) ;
       return -1;
    }

    struct ev_io * http_readable = malloc(sizeof(struct ev_io));

    if (!http_readable)
    {
       DEBUG_PRINT("ev_io malloc err %d\n",http_readable) ;
       return -1;
    }

    ev_io_init(http_readable,http_read,fd,EV_READ);
    //http_readable.data = &h;
    ev_io_start(main_loop,http_readable);


    DEBUG_PRINT("write data..\n");
    char *req = build_http_request("GET",h.path,"");
    DEBUG_PRINT(" data %d\n",strlen(req));

    int ret = -1;
    while (ret == -1 ) // 解决第一次写入会概率出现EAGIN的问题，采用循环写，来判断是否会EAGIN
    {
         ret = write(fd,req,strlen(req));
        if( errno == EAGAIN)// 实际测试中第一次也会出现反回EAGIN的情况，这时buf是第一次写入数据，why?
        {
            DEBUG_PRINT(" ret %d errno:%d desc:%s\n",ret,errno,strerror(errno));
            continue ;
        }
        else if(errno != EAGAIN)
        {
            DEBUG_PRINT(" ret %d errno:%d desc:%s\n",ret,errno,strerror(errno));
            break;
        }
    }
    free(req);
}

int main(int argc , char ** argv)
{

    int i = 0;
    int n = 10000;
    unsigned int  concurrent  = 0;
    unsigned int  port  = 0;
    char * url = NULL;
    char * host = NULL;
    int c ;

    opterr = 0;
    while( (c= getopt(argc, argv , "c:u:n:h:p:")) != -1 )
    {
   	switch(c)
	    {
	   	case 'c': 
			concurrent = atoi(optarg);
			break;
	   	case 'p': 
			port = atoi(optarg);
			break;
	   	case 'h': 
			host = optarg;
			break;
	   	case 'n': 
			n = atoi(optarg);
			break;
	   	case 'u': 
			url = optarg;
			break;
	    
		default:
            DEBUG_PRINT("not support args:usage hyc -u http://www.test.com/test?useid=1#aa");
			abort();
	    }	
    
    }


    if ( concurrent == 0 || url == NULL)
    {
       DEBUG_PRINT("args wrong");
       exit(-1); 
    }


    h.ip = host;
    h.port = port;
    h.path = url;
    h.n = n;

    DEBUG_PRINT("%s:%d concurrent:%d\n",h.ip,h.port,concurrent);

    struct ev_loop *main_loop = ev_default_loop(0);

    for(i = 0; i< concurrent; i++)
    {
	   new_tcp_connection_ev(h.ip, h.port ,main_loop);
    }

    ev_run(main_loop, 0);
}
