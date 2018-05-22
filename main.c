#include<stdio.h>
#include<string.h>
#include<sys/socket.h>
#include<linux/in.h>
#include<ev.h>
#include<fcntl.h>

#define ERROR_ON(fd)  if(fd < 0) return -1;

int setnonblock(int sockfd)
{
    if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFD, 0)|O_NONBLOCK) == -1) {
		    return -1;
	}
	return 0; }
void setaddress(const char* ip,int port,struct sockaddr_in* addr){  
    bzero(addr,sizeof(*addr));  
    addr->sin_family=AF_INET;  
    inet_pton(AF_INET,ip,&(addr->sin_addr));  
    addr->sin_port=htons(port);  
}  
static void http_read(struct ev_loop *loop, ev_io *stat, int events)
{
   char buf[1000] = {'\0'};
   read(stat->fd,buf,1000);
   printf("http_read:\n");
   printf(buf);
}

static int new_tcp_connection(const char *ip, unsigned int port)
{

    int ret;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    ERROR_ON(fd);

    setnonblock(fd);

    struct sockaddr_in addr;
    setaddress(ip, port , &addr);
    ret = connect(fd, (struct sockaddr*)(&addr), sizeof(addr));

    ERROR_ON(ret);

    return fd;
}
int main(int argc , char ** argv)
{
    if (argc < 3 )
    {
       printf("arg wrong, hyc ip port"); 
       return -1;
    }

    char * ip = argv[1];
    unsigned int  port = atoi(argv[2]);

    int  fd = new_tcp_connection(ip,port);

    if (fd < 0 )
    {
       printf("fd is %d",fd) ;
       return -1;
    }

    struct ev_loop *main_loop = ev_default_loop(0);

    struct ev_io http_readable;
    ev_io_init (&http_readable,http_read,fd,EV_READ);
    ev_io_start(main_loop,&http_readable);

    write(fd,"1234",4);
    ev_run(main_loop, 0);
}
