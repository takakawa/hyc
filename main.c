#include<string.h>
#include<getopt.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<ev.h>
#include<fcntl.h>
#include<errno.h>
#include<sys/time.h> 
#include<time.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

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
struct global_data
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
   char          *headers[100];
   unsigned int   header_num;
   unsigned int  port;
   unsigned int  n;
   unsigned int  rate;
   unsigned int  intervalus;
   unsigned int  concurrent;
   int           pressure_start_timestamp; // 本connection开始的时间
};

struct connection 
{
    unsigned int id;// connection id
    int          request_count;
    int          request_start_timestamp; // 本connection开始的时间
    int          request_send_timestamp; // 每次发送更新这个时间戳
    int          request_total_time; // connection 处理请求的总时间,不含由于设置rate而引入的sleep时间
    char         url[MAX_URL_LEN];
    char         writebuf[WRITE_BUF_LEN ];
    char         readbuf[READ_BUF_LEN ];
    char         host[MAX_HOST_LEN];
    unsigned int port;
    unsigned int fd;
    struct param * param;
};




struct global_data global_data;
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
       PRINT("Connect Error:%d  errno:%d %s\n",ret,errno,strerror(errno)); 
       return -2;
    }
    ret = setnonblock(fd);
    if (ret < 0)
    {
       PRINT("setnonblock:%d  errno:%d %s\n",ret,errno,strerror(errno)); 
       return ret; 
    }
    DEBUG_PRINT("fd:%d\n",fd);
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

       for(int i=0;i < conn->param->header_num;i++)
       {
            strcat(buf,conn->param->headers[i]);  
            strcat(buf,"\r\n");
       }

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
    int send_index  = 0;
    int total_send_len = strlen(conn->writebuf);
    

    conn->request_send_timestamp = getCurrentTime();
    while(send_index < total_send_len)
    {
    
        int writelen = write(conn->fd,conn->writebuf+send_index ,total_send_len - send_index);

        DEBUG_PRINT("send:from %d len %d\n%s\n",send_index,total_send_len-writelen,conn->writebuf);
       
        send_index += writelen;

        if (errno == EAGAIN)
        {
            usleep(100);
            continue;
        }
        else if(errno == 0)
        {
            break; 
        }
        else
        {
             PRINT("fd:%d writelen:%d errno:%d desc:%s\n",conn->fd, writelen,errno,strerror(errno));
             exit(-1); 
        }
    }

    return 0;
} 
int receive_connection(struct connection *conn)
{
        DEBUG_PRINT("recv data from fd[%d]\n",conn->fd);
        conn->request_count ++; 
      
       int total_recv_len = 0;
       while(1)
       {
       
           int n = read(conn->fd,conn->readbuf, READ_BUF_LEN);
       
           DEBUG_PRINT("recv:%d\n"\
	               "------------------------------------------"\
                   "----------------\n%s\n--------------------"\
                   "--------------------------------------\n",n,conn->readbuf);

           if (n < 0)
           {
                if ( errno == EAGAIN)
               {
                   DEBUG_PRINT("recv EAGIN ,ret:%d\n",n);
                   break;
               }
               else
               {
                  PRINT("recv err:%d %s %s:%d\n",errno,strerror(errno),conn->param->ip,conn->param->port) ;
                  exit(-2); 
               }

           }
           else if( n == 0)
           {
              return 0; 
           }
           DEBUG_PRINT("recv data %d bytes\n",n);
           total_recv_len += n; 
       } 
	   DEBUG_PRINT("conn[%d] http request cnt: %d\n",conn->id, conn->request_count);
	   conn->request_total_time += (getCurrentTime()- conn->request_send_timestamp );
       return total_recv_len;

}
void http_read(struct ev_loop *loop, ev_io *stat, int events)
{
   struct connection *conn =  stat->data;

   int n =   receive_connection(conn);


   if (n >0)
   {
       usleep(conn->param->intervalus);

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
    char *   ip   = param->ip;
    unsigned port = param->port;

    int      fd   = new_connection(ip,port);
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
void summary(struct global_data *gdata)
{
    int request_cnt          = 0;
    int request_total_time   = 0;//请求纯耗时，不含等待时间
    float request_qps        = 0;
    struct global_data * data = gdata;
 
    int http_pressure_time   = getCurrentTime() - data->conns[0]->param->pressure_start_timestamp ;//压测时间
    for(int i=0; i< data->conns_num; i++)
    {
        int request_cnt_tmp        = data->conns[i]->request_count; 
        int request_total_time_tmp = data->conns[i]->request_total_time; 
        float request_qps_tmp      = ((float)(request_cnt_tmp)/((float)(http_pressure_time)/1000.0f));

        printf("\t\tConnection %3d Summary\n",data->conns[i]->id);
        printf("Total SendRequest: %d\n",request_cnt_tmp);
        printf("Total Time       : %d ms\n",request_total_time_tmp);
        printf("Total QPS        : %f\n",request_qps_tmp);
        printf("Request Latency  : %f ms\n",(float)request_total_time/request_cnt_tmp);
        request_qps                += request_qps_tmp;
        request_cnt                += data->conns[i]->request_count; 
        request_total_time         += data->conns[i]->request_total_time; 
    }
    
    printf("\nSummary:\n");
    printf("Total PressTime  : %d ms\n",http_pressure_time);
    printf("Total SendRequest: %d\n",request_cnt);
    printf("Total Time       : %d ms\n",request_total_time);
    printf("Total QPS        : %f\n",request_qps);
    printf("Request Latency  : %f ms\n",(float)request_total_time/request_cnt);
 
}
static void timer_callback(struct ev_loop *loop,ev_timer *w,int revents)
{
    summary((struct global_data*)w->data); 
    exit(0);
}

void stop(int sig)
{   
   summary(&global_data);
   exit(0);
}
void read_thread()
{

    struct ev_loop *main_loop = ev_default_loop(0);
    

}
int main(int argc , char ** argv)
{

    int i = 0;
    int c ;
    int n = -1;
    unsigned int  t = 0;
    unsigned int  concurrent  = 1;
    unsigned int  port  = 0;
    unsigned int  rate  = 0;
    char * url  = NULL;
    char * host = NULL;
    char * method = NULL;
    char * postdata = NULL;
    char * args_pattern = "c:u:n:h:p:t:X:d:H:r:";

    memset(&param, 0, sizeof(param));

    opterr = 0;
    while( (c= getopt(argc, argv , args_pattern)) != -1 )
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
	   	case 'H': 
                        param.headers[param.header_num++] = optarg;
			break;
	   	case 'X': 
			method = optarg;
			break;
	   	case 'r': 
			rate = atoi(optarg);
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
                        printf("Usage:%s\n",args_pattern);
			exit(0);
	    }	
    }


    if ( concurrent == 0 || url == NULL)
    {
       printf("args:%s\n",args_pattern);
       exit(-1); 
    }

    signal(SIGINT, stop);
    

    param.ip = host;
    param.port = port;
    param.path = url;
    param.n = n;
    param.rate = rate;
    param.concurrent=concurrent;
    param.postdata= postdata;
    param.method = method?method:"GET";
    param.pressure_start_timestamp = getCurrentTime();

    if (rate*concurrent)
    {
       param.intervalus = 1000000/rate;
    }

    printf("HYC runs with %d concurrency, sleep intervalus:%d\n",concurrent,param.intervalus);

    struct ev_loop *main_loop = ev_default_loop(0);
    
    global_data.conns = malloc(sizeof(struct connection *)*concurrent);
    global_data.conns_num = concurrent;

    for(i = 0; i< concurrent; i++)
    {
	   global_data.conns[i] = new_connection_chain(&param ,main_loop);
    }

    if ( t > 0 )
    {
        printf("add timer:%ds\n",t);
        ev_timer timer_watcher;
        ev_init(&timer_watcher,timer_callback);
        timer_watcher.data = &global_data;
        ev_timer_set(&timer_watcher,t,0);// t秒后开始执行,非周期 
        ev_timer_start(main_loop,&timer_watcher);
    }
    ev_run(main_loop, 0);
    summary(&global_data);// all events is topped ,will comes here

    free(global_data.conns);
}
