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
#include <sys/time.h> 

#define READ_BUF_LEN (4096)
#define WRITE_BUF_LEN (4096)
#define MAX_URL_LEN (256)
#define MAX_HOST_LEN (128)
#define ERROR_ON(fd)  if(fd < 0) return -1;

#define PRINT(format,args...)   fprintf(stdout,"[DEBUG]%s:%d->"format,__func__,__LINE__,##args)

#ifdef DEBUG
#define DEBUG_PRINT(format,args...)   fprintf(stdout,"[DEBUG]%s:%d->"format,__func__,__LINE__,##args)
#else
#define DEBUG_PRINT(format,args...)   1
#endif

extern char *optarg;
extern int   optopt;
extern int   opterr;

struct connection 
{
    unsigned int id;// connection id
    int          request_count;
    int          request_start_timestamp; // 本connection开始的时间
    int          request_send_timestamp; // 每次发送更新这个时间戳
    int          request_total_time; // connection 处理请求的总时间
    char         url[MAX_URL_LEN];
    char         writebuf[WRITE_BUF_LEN ];
    char         readbuf[READ_BUF_LEN ];
    char         host[MAX_HOST_LEN];
    unsigned int port;
};


struct Host
{
   char *ip;
   unsigned int  port;
   unsigned int  n;
   char *path;
};

struct Host h ;

int new_tcp_connection_chain(char * ip, unsigned int port,struct ev_loop *main_loop);
int continue_tcp_connection(struct connection *conn, struct ev_loop *main_loop);

long getCurrentTime()    
{    
     struct timeval tv;    
     gettimeofday(&tv,NULL);    
     return tv.tv_sec * 1000 + tv.tv_usec / 1000;    
}
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
    
    setaddress(ip, port , &addr);
    ret = connect(fd, (struct sockaddr*)(&addr), sizeof(addr));
    if(ret < 0)
    {
       DEBUG_PRINT("Connect Error:%d  errno:%d %s\n",ret,errno,strerror(errno)); 
       return -2;
    }
    ret = setnonblock(fd);
    if (ret < 0)
    {
       return ret; 
    }
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
   struct connection *conn =  stat->data;

   int n = read(stat->fd,conn->readbuf, READ_BUF_LEN);
   if (n == 0)
   {
        PRINT("remote closed client\n") ;
        close(stat->fd);
        ev_io_stop(loop, stat);
        free(stat);

        PRINT("reconnect to  client:%s:%d\n",h.ip,h.port) ;
        continue_tcp_connection(conn ,loop);
   }
   else
   {
	   conn->request_count ++; 

       
	   DEBUG_PRINT("http request cnt: %d\n",conn->request_count);
	   DEBUG_PRINT("path:%s\n",h.path);
	   DEBUG_PRINT("recv:\n"\
	               "------------------------------------------"\
                   "----------------\n%s\n--------------------"\
                   "--------------------------------------\n",conn->readbuf);
       
        conn->request_total_time += (getCurrentTime()- conn->request_send_timestamp );

       if(conn->request_count >= h.n )
	   {
            close(stat->fd);
            free(conn);
            ev_io_stop(loop, stat);
            free(stat);
            PRINT("run %d times quit\n",conn->request_count);
            return; 
	   }

       DEBUG_PRINT("%s\n",conn->writebuf);
	   write(stat->fd,conn->writebuf,strlen(conn->writebuf));
       conn->request_send_timestamp = getCurrentTime();
   }
}
int continue_tcp_connection(struct connection *conn, struct ev_loop *main_loop)
{
   int  fd = new_tcp_connection(conn->host,conn->port);

    if (fd < 0 )
    {
       PRINT("fd is %d\n",fd) ;
       return -1;
    }

    struct ev_io * http_readable = malloc(sizeof(struct ev_io));

    if (!http_readable)
    {
       PRINT("ev_io malloc err %d\n",http_readable) ;
       return -1;
    }
    
    ev_io_init(http_readable,http_read,fd,EV_READ);
    http_readable->data = conn;
    ev_io_start(main_loop,http_readable);

   int ret = write(fd,conn->writebuf,strlen(conn->writebuf));
    conn->request_send_timestamp = getCurrentTime();
    if( errno == EAGAIN)// 实际测试中第一次也会出现反回EAGIN的情况，这时buf是第一次写入数据，why?
    {
        DEBUG_PRINT(" ret %d errno:%d desc:%s\n",ret,errno,strerror(errno));
    }
    else if(errno != EAGAIN)
    {
        DEBUG_PRINT(" ret %d errno:%d desc:%s\n",ret,errno,strerror(errno));
    }

}
int new_tcp_connection_chain(char * ip, unsigned int port,struct ev_loop *main_loop)
{
    int  fd = new_tcp_connection(ip,port);

    if (fd < 0 )
    {
       PRINT("fd is %d\n",fd) ;
       return -1;
    }

    struct ev_io * http_readable = malloc(sizeof(struct ev_io));

    if (!http_readable)
    {
       PRINT("ev_io malloc err %d\n",http_readable) ;
       return -1;
    }
    struct connection * conn = malloc(sizeof(struct connection));
    if(!conn)
    {
    
       PRINT("conn malloc err \n") ;
       return -1;
    }

    strncpy(conn->host, ip, MAX_HOST_LEN);
    conn->port = port;

    ev_io_init(http_readable,http_read,fd,EV_READ);
    http_readable->data = conn;
    ev_io_start(main_loop,http_readable);

    char *req = build_http_request("GET",h.path,"");
    strncpy(conn->writebuf,req,WRITE_BUF_LEN);
    free(req);


    int ret = write(fd,conn->writebuf,strlen(conn->writebuf));
    conn->request_send_timestamp = getCurrentTime();
    if( errno == EAGAIN)// 实际测试中第一次也会出现反回EAGIN的情况，这时buf是第一次写入数据，why?
    {
        PRINT(" ret %d errno:%d desc:%s\n",ret,errno,strerror(errno));
    }
    else if(errno != EAGAIN)
    {
        PRINT(" ret %d errno:%d desc:%s\n",ret,errno,strerror(errno));
    }
}
static void timer_callback(struct ev_loop *loop,ev_timer *w,int revents)
{

    PRINT("Result\n");
    //PRINT("Total SendRequest: %d\n",request_cnt);
    //PRINT("Total Time(ms)   : %d\n",request_total_time);
    //PRINT("Total QPS        : %f\n",(float)(request_cnt)/((float)(request_total_time)/1000));
    exit(0);
}
int main(int argc , char ** argv)
{

    int i = 0;
    int c ;
    int n = 10000;
    unsigned int  t = 10;
    unsigned int  concurrent  = 0;
    unsigned int  port  = 0;
    char * url  = NULL;
    char * host = NULL;

    opterr = 0;
    while( (c= getopt(argc, argv , "c:u:n:h:p:t:")) != -1 )
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
	   	case 't': 
			t = atoi(optarg);
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
	   new_tcp_connection_chain(h.ip, h.port ,main_loop);
    }

    if ( t > 0 )
    {
        ev_timer timer_watcher;
        ev_init(&timer_watcher,timer_callback);
        ev_timer_set(&timer_watcher,t,0);// t秒后开始执行,非周期 
        ev_timer_start(main_loop,&timer_watcher);
    }
    ev_run(main_loop, 0);
}
