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
#include <time.h>

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
struct timer_data
{
   struct connection ** conns;
   unsigned int  conns_num;
};

struct param
{
   char          *ip;
   char          *path;
   char          *postdata;
   char          *method;
   unsigned int  port;
   unsigned int  n;
};

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
    unsigned int fd;
    struct param * param;
};




struct timer_data timer_data;
struct   param param;
unsigned int  g_connection_id = 0;

struct connection *  new_connection_chain(struct param * param,struct ev_loop *main_loop);
int continue_connection(struct connection *conn, struct ev_loop *main_loop);

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
static int new_connection(const char *ip, unsigned int port)
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

int   build_http_request(struct connection * conn)
{
       char * method = conn->param->method;
       char * path   = conn->param->path;
       char * body   = conn->param->postdata;
       char * ip     = conn->param->ip;
       char * buf    = conn->writebuf;

       strcpy(buf,method);
       strcat(buf," ");
       strcat(buf,path);
       strcat(buf," HTTP/1.1\r\n");
       strcat(buf,"User-Agent: hyc\r\n");
       strcat(buf,"Host: ");
       strcat(buf,ip);
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
	       return -1;
       }
       /* 
       DEBUG_PRINT("request built:\n----------------------------"\
                   "------------------------------\n%s\n---------"\
                   "-------------------------------------------------\n",buf);
       */ 
       return 0;
}
int flush_connection(struct connection * conn)
{   
    int writelen = write(conn->fd,conn->writebuf,strlen(conn->writebuf));
    DEBUG_PRINT("send:\n%s\n",conn->writebuf);
    conn->request_send_timestamp = getCurrentTime();

    if (errno)
    {
        PRINT("fd:%d writelen:%d errno:%d desc:%s\n",conn->fd, writelen,errno,strerror(errno));
        exit(-1);
    }
    return 0;
} 
int receive_connection(struct connection *conn)
{
        conn->request_count ++; 
       
	   DEBUG_PRINT("conn[%d] http request cnt: %d\n",conn->id, conn->request_count);
	   DEBUG_PRINT("recv:\n"\
	               "------------------------------------------"\
                   "----------------\n%s\n--------------------"\
                   "--------------------------------------\n",conn->readbuf);
       
        conn->request_total_time += (getCurrentTime()- conn->request_send_timestamp );

}
void http_read(struct ev_loop *loop, ev_io *stat, int events)
{
   struct connection *conn =  stat->data;

   int n = read(stat->fd,conn->readbuf, READ_BUF_LEN);
   if (n >0)
   {
       receive_connection(conn);
       if(conn->param->n > 0 && conn->request_count >= conn->param->n )
	   {
            PRINT("conn:%d run %d times quit,set :%d\n",conn->id, conn->request_count,conn->param->n);
            close(stat->fd);
            ev_io_stop(loop, stat);
            free(stat);
            return; 
	   }

       flush_connection(conn);
   }
   else
   {
        PRINT("remote closed client\n") ;
        close(stat->fd);
        ev_io_stop(loop, stat);
        free(stat);

        PRINT("reconnect to  client:%s:%d\n",conn->param->ip,conn->param->port) ;
        continue_connection(conn ,loop);
   }

}
int continue_connection(struct connection *conn, struct ev_loop *main_loop)
{
   int  fd = new_connection(conn->host,conn->port);

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

    conn->id   = g_connection_id++ ; 
    conn->fd   = fd;

    flush_connection(conn);

}

struct connection * new_connection_chain(struct param * param,struct ev_loop *main_loop)
{
    char * ip     = param->ip;
    unsigned port = param->port;

    int  fd = new_connection(ip,port);

    if (fd < 0 )
    {
       PRINT("fd is %d\n",fd) ;
       exit(-1);
    }

    struct ev_io * http_readable = malloc(sizeof(struct ev_io));

    if (!http_readable)
    {
       PRINT("ev_io malloc err %d\n",http_readable) ;
       exit(-1);
    }
    struct connection * conn = malloc(sizeof(struct connection));
    if(!conn)
    {
    
       PRINT("conn malloc err \n") ;
       exit(-1);
    }

    strncpy(conn->host, ip, MAX_HOST_LEN);
    conn->port                 = port;
    conn->fd                   = fd;
    conn->request_count        = 0;
    conn->request_total_time   = 0;
    conn->id                   = g_connection_id++ ; 
    conn->param                = param;

    ev_io_init(http_readable,http_read,fd,EV_READ);
    http_readable->data = conn;
    ev_io_start(main_loop,http_readable);

    build_http_request(conn);
    flush_connection(conn);
    return conn;
}
static void timer_callback(struct ev_loop *loop,ev_timer *w,int revents)
{

    int request_cnt          = 0;
    int request_total_time   = 0;
    int request_qps          = 0;
    struct timer_data * data = w->data;
 
    for(int i=0; i< data->conns_num; i++)
    {
        int request_cnt_tmp        = data->conns[i]->request_count; 
        int request_total_time_tmp = data->conns[i]->request_total_time; 
        int request_qps_tmp        = (int)((float)(request_cnt_tmp)/((float)(request_total_time_tmp)/1000));

        printf("Connection %3d Result:\n",data->conns[i]->id);
        printf("Total SendRequest: %d\n",request_cnt_tmp);
        printf("Total Time(ms)   : %d\n",request_total_time_tmp);
        printf("Total QPS        : %d\n",request_qps_tmp);
        printf("Request Latency  : %f ms\n",(float)request_total_time_tmp/request_cnt_tmp);
        request_qps                += request_qps_tmp;
        request_cnt                += data->conns[i]->request_count; 
        request_total_time         += data->conns[i]->request_total_time; 
    }
    
    printf("\nTotal Result:\n");
    printf("Total SendRequest: %d\n",request_cnt);
    printf("Total Time(ms)   : %d\n",request_total_time);
    printf("Total QPS        : %d\n",request_qps);
    printf("Request Latency  : %f ms\n",(float)request_total_time/request_cnt);
    exit(0);
}
void stop(int sig)
{   
    int request_cnt          = 0;
    int request_total_time   = 0;
    int request_qps          = 0;
    struct timer_data * data = &timer_data;
 
    for(int i=0; i< data->conns_num; i++)
    {
        int request_cnt_tmp        = data->conns[i]->request_count; 
        int request_total_time_tmp = data->conns[i]->request_total_time; 
        int request_qps_tmp        = (int)((float)(request_cnt_tmp)/((float)(request_total_time_tmp)/1000));

        printf("Connection %3d Result:\n",data->conns[i]->id);
        printf("Total SendRequest: %d\n",request_cnt_tmp);
        printf("Total Time(ms)   : %d\n",request_total_time_tmp);
        printf("Total QPS        : %d\n",request_qps_tmp);
        printf("Request Latency  : %f ms\n",(float)request_total_time_tmp/request_cnt_tmp);
        request_qps                += request_qps_tmp;
        request_cnt                += data->conns[i]->request_count; 
        request_total_time         += data->conns[i]->request_total_time; 
    }
    
    printf("\nTotal Result:\n");
    printf("Total SendRequest: %d\n",request_cnt);
    printf("Total Time(ms)   : %d\n",request_total_time);
    printf("Total QPS        : %d\n",request_qps);
    printf("Request Latency  : %f ms\n",(float)request_total_time/request_cnt);
    exit(0);


}
int main(int argc , char ** argv)
{

    int i = 0;
    int c ;
    int n = -1;
    unsigned int  t = 10;
    unsigned int  concurrent  = 0;
    unsigned int  port  = 0;
    char * url  = NULL;
    char * host = NULL;
    char * method = NULL;
    char * postdata = NULL;

    opterr = 0;
    while( (c= getopt(argc, argv , "c:u:n:h:p:t:X:d:")) != -1 )
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
	   	case 'X': 
			method = optarg;
			break;
	   	case 'd': 
			postdata = optarg;
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

    signal(SIGINT, stop);

    param.ip = host;
    param.port = port;
    param.path = url;
    param.n = n;
    param.postdata= postdata;
    param.method = method?method:"GET";

    DEBUG_PRINT("%s:%d concurrent:%d\n",param.ip,param.port,concurrent);

    struct ev_loop *main_loop = ev_default_loop(0);
    

    timer_data.conns = malloc(sizeof(struct connection *)*concurrent);
    timer_data.conns_num = concurrent;

    for(i = 0; i< concurrent; i++)
    {
	   timer_data.conns[i] = new_connection_chain(&param ,main_loop);
    }

    if ( t > 0 )
    {
        ev_timer timer_watcher;
        ev_init(&timer_watcher,timer_callback);
        timer_watcher.data = &timer_data;
        ev_timer_set(&timer_watcher,t,0);// t秒后开始执行,非周期 
        ev_timer_start(main_loop,&timer_watcher);
    }
    ev_run(main_loop, 0);
    free(timer_data.conns);
}
