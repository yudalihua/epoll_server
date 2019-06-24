#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

#include "epoll_server.h"

#define MAXSIZE 2000

void epoll_run(int port)
{
    // 创建一个epoll树的根节点
    int epfd = epoll_create(MAXSIZE);
    if(epfd == -1)
    {
        perror("epoll_create error");
        exit(1);
    }

    // 添加要监听的节点，先添加监听节点（服务器套接字） 
    int lfd = init_listen_fd(port, epfd);

    // 委托内核监测添加到树上的全部节点
    struct epoll_event all[MAXSIZE];
    while(1)
    {
    	//  阻塞循环监听，直到有套接字事件发生，ret是发生事件套接字的数量	 
    	int ret=epoll_wait(epfd,all,MAXSIZE,-1);
    	if(ret == -1){
    		perror("there is something wrong with epoll_wait function");
    		exit(1);
		}          

        //  遍历所有有事件发生的套接字。 
        for(int i=0; i<ret; ++i)
        {
            // 依次获取每个套接字对应的epoll_event结构体的指针 
            struct epoll_event *pev = &all[i];
            if(!(pev->events & EPOLLIN))
            {
                // 如果这个套接字的事件不是读，那么暂时忽略，跳过本次循环，查看下一个套接字。 
                continue;
            }
			
			// 服务器套接字有事件发生，表明有新的客户端连接，调用do_accpt函数 
			if(pev->data.fd == lfd){
				do_accept(lfd,epfd);
			} 		          	
            else
            {
            	// 客户端连接套接字有事件发生，表明有读事件，调用do_read函数 
                do_read(pev->data.fd, epfd);
            }
        }
    }
}





// 读数据
void do_read(int cfd, int epfd)
{
    // 将浏览器发过来的数据, 读到buf中 
    char line[1024] = {0};
    
    // 因为不知道客户端发过来的请求有多大，缓冲大小也不好设置，于是我每次读取一行，反正也是边缘触发
    // 这里先读取请求行，如果请求行长度为0，表示客户端发来了EOF，要关闭tcp连接。 
    int len = get_line(cfd, line, sizeof(line));
    if(len == 0)
    {
        printf("the client disconnect...\n");
        
        // 关闭套接字, 从epoll树上删除cfd 
        disconnect(cfd, epfd);         
    }
    else
    {
    	printf("Request Line: %s",line);
        printf("============= Request Header ============\n");
        
		// 还有数据没读完，继续读 
        while(len)
        {
        	char buf[1024] = {0};
			len=get_line(cfd,buf,sizeof(buf));
            printf("-----: %s", buf);
            
        }
        printf("============= The End ============\n");
    }

    // 处理请求行，判断请求类型，我们只处理get请求。
	// strncasecmp，比较两个字符串的前n个字符，不区分大小写。 
    if(strncasecmp("get", line, 3) == 0)
    {
        // 如果客户端传来的是get请求，那么就处理；不是get请求，不处理。 
        http_request(line, cfd);
        
        // 处理完了就关闭套接字, 从epoll上删除cfd。
		// 没有实现长连接，一次连接只能处理一个请求，再次请求需要重新建立连接。 
        disconnect(cfd, epfd);         
    }
}






// 解析http请求消息的每一行内容
int get_line(int sock, char *buf, int size)
{
	// i是buffer的下标，buffer的大小是size。请求行一般没有我们设置的buffer大。 
    int i = 0;
    char c = '\0';
    int n;
    
    // 读取请求行，一般是读到\n 时停止while循环，而不是i==size-1的时候。
	// 读取较大的数据行的时候，一般会在i==size-1的时候停止读取，然后设置buf[size-1]='\0' 
    while ((i < size - 1) && (c != '\n'))
    {
    	// 每次读一个字节，如果读出来了，返回值肯定是1。 
        n = recv(sock, &c, 1, 0);
        if (n > 0)
        {
        	// 如果读到的是\r，那么表明一行要结束了，接着再读到一个字节，应该是\n了。
            if (c == '\r')
            {
            	// MSG_PEEK表示从缓冲区中以拷贝的方式读数据，是试探性地读。 
				// 如果\r后边的是\n，那么我就把它读出来；如果不是\n那么就不读出来，而是自己写个\n到缓冲区。 
                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))
                {
                    recv(sock, &c, 1, 0);
                }
                else
                {
                	//如果读到不是\n，那么我就放一个\n到buffer里边。 
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        }
        else
        {
            c = '\n';
        }
    }
    // 退出while循环，一行读取完毕，给读取到的字符串最后缀上一个\0 
    buf[i] = '\0';

    return i;
}






// 断开客户端的连接，关闭套接字 
void disconnect(int cfd, int epfd)
{
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
    if(ret == -1)
    {
        perror("epoll_ctl del cfd error");
        exit(1);
    }
    close(cfd);
}





// http请求处理
void http_request(const char* request, int cfd)
{
    // 拆分http请求行
   	char method[12] ,path[1024] ,protocol[12];
   	// 使用sscanf函数拆分请求行 
   	sscanf(request,"%[^ ] %[^ ] %[^ ]", method, path,protocol);     
    printf("method = %s, path = %s, protocol = %s\n", method, path, protocol);

    // 解码，服务器接收浏览器发送的文件时候，需要进行解码
    decode_str(path, path);        
    // 处理path，去掉path(/xx)中的/，前提是我们已经进入到工作目录里边来了。 
    // 因为path是指向字符数组的指针，只要这个指针向后移动一个位置，就能到了文件的起始地址。 
    char* file = path+1;
    // 如果没有指定访问的资源, 默认显示资源目录中的内容
    if(strcmp(path, "/") == 0)
    {
        // file的值, 资源目录的当前位置
        file = "./";
    }

    // 获取文件属性，定义一个stat变量，用stat系统调用把文件的信息放到结构体stat中。
	struct stat st;
	int ret =  stat(file,&st);
    if(ret == -1)
    {
        // 如果stat返回-1，说明文件/目录不存在，给客户端返回404页面。 
        send_respond_head(cfd, 404, "File Not Found", ".html", -1);
        send_file(cfd, "404.html");
    }

    // S_ISDIR宏判断是否是目录，这个宏包含在 <sys/stat.h>头文件中。 
    if(S_ISDIR(st.st_mode))
    {
        // 发送响应头和目录信息 
        send_respond_head(cfd, 200, "OK", get_file_type(".html"), -1);
        send_dir(cfd, file);
    }
    // S_ISDIR宏判断是否是普通文件 
    else if(S_ISREG(st.st_mode))
    {
        // 发送响应头和文件内容 
        send_respond_head(cfd, 200, "OK", get_file_type(file), st.st_size);
        // 发送文件内容
        send_file(cfd, file);
    }
}





// 发送响应头
void send_respond_head(int cfd, int no, const char* desp, const char* type, long len)
{  

    // 发送状态行。sprintf函数把个数输送到字字符数组buf中。 
    char buf[1024] = {0};
    sprintf(buf, "http/1.1 %d %s\r\n", no, desp);
    send(cfd, buf, strlen(buf), 0);
    
    // 发送响应头
    sprintf(buf,"Content-type:%s\r\n",type);
    sprintf(buf+strlen(buf),"Content-Length:%ld\r\n",len);
    send(cfd, buf, strlen(buf), 0);
    	
    // 发送一个空行
    send(cfd,"\r\n", 2, 0); 
}





// 发送目录内容
void send_dir(int cfd, const char* dirname)
{
    // 拼一个html页面<table></table>
    char buf[4094] = {0};
	
	// sprintf这个函数把格式输出到字符串里边。 
    sprintf(buf, "<html><head><title>目录名: %s</title></head>", dirname);
    sprintf(buf+strlen(buf), "<body><h1>当前目录: %s</h1><table>", dirname);

    char enstr[1024] = {0};
    char path[1024] = {0};
    
    //  下面定义：ptr是一个指针，指向dirent[]	 
    struct dirent** ptr;
    
	// scandir返回值是目录项的个数。 所有的目录项文件都被放到了ptr指向的一个结构体数组里边。 
    int num = scandir(dirname, &ptr, NULL, alphasort);
    
	// 遍历dirent结构体数组 
    for(int i=0; i<num; ++i)
    {
    	char* name = ptr[i]->d_name;

        // 拼接文件的完整路径，因为当前进程并没有进入到dirname里边去，而是和dirname出在同一个目录下。 
        sprintf(path, "%s/%s", dirname,name);
        printf("path =%s========\n",path);
        struct stat st;
        stat(path,&st);
		// 发送文件的时候进行编码 
        encode_str(enstr, sizeof(enstr), name);
        // 如果是文件
        if(S_ISREG(st.st_mode))
        {
        	// 下边格式字符串中，有一个a标签，属于它的双引号要用反斜杠进行转义，不然会和属于格式字符串的双引号冲突。 
            sprintf(buf+strlen(buf),             
                    "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
                    enstr, name, (long)st.st_size);
        }
        // 如果是目录
        else if(S_ISDIR(st.st_mode))
        {
            sprintf(buf+strlen(buf), 
                    "<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>",
                    enstr, name, (long)st.st_size);
        }
        send(cfd, buf, strlen(buf), 0);
        memset(buf, 0, sizeof(buf));
        // 字符串拼接
    }

    sprintf(buf+strlen(buf), "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);

    printf("dir message send OK!!!!\n");
    
#if 0

    // 使用opendir系统调用打开目录，返回的是一个DIR。 
	DIR* dir = opendir(dirname); 
    if(dir == NULL)
    {
        perror("opendir error");
        exit(1);
    }

    // 使用readdir系统调用读目录，传入DIR，返回的是dirent，是一个目录的条目。
	struct dirent* ptr = NULL;
	while((ptr=readdir(dir)) != NULL) {
		char* name=ptr->d_name;
	}
    closedir(dir);
#endif
}






// 发送文件
void send_file(int cfd,const char * filename)
{
    // 打开文件
    int fd = open(filename, O_RDONLY);
    if(fd == -1)
    {
        return;
    }

    // 循环读文件，把文件一次读buf大小到buf，读一次，发送一次。 
    char buf[4096] = {0};
    int len = 0;
    while((len=read(fd,buf,sizeof(buf)))>0){
    	
    	send(cfd,buf,len,0);
	}
	if(len == -1){
		perror("read file error");
		exit(1);
	}
	close(fd);
}


// 接受新连接处理
void do_accept(int lfd, int epfd)
{
    struct sockaddr_in client;
    socklen_t len = sizeof(client);
    int cfd = accept(lfd, (struct sockaddr*)&client, &len);
    if(cfd == -1)
    {
        perror("accept error");
        exit(1);
    }

    // 打印客户端信息
    char ip[64] = {0};
    printf("New Client IP: %s, Port: %d, cfd = %d\n",
           inet_ntop(AF_INET, &client.sin_addr.s_addr, ip, sizeof(ip)),
           ntohs(client.sin_port), cfd);

    // 设置cfd为非阻塞
    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(cfd, F_SETFL, flag);

    // 得到的新节点挂到epoll树上
    struct epoll_event ev;
    ev.data.fd = cfd;
    // 边沿非阻塞模式
    ev.events = EPOLLIN | EPOLLET;
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
    if(ret == -1)
    {
        perror("epoll_ctl add cfd error");
        exit(1);
    }
}






//  初始化服务器端listen的套接字lfd
int init_listen_fd(int port, int epfd)
{
    // 创建listen的套接字lfd
    int lfd = socket(AF_INET,SOCK_STREAM,0) ;
    if(lfd == -1)
    {
        perror("socket error");
        exit(1);
    }

    // 创建服务器端socket的地址和端口信息 
    struct sockaddr_in serv;
    memset(&serv,0,sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_port = htons(port);
	serv.sin_addr.s_addr = htonl(INADDR_ANY); 	


    // 设置套接字选项中的端口复用，也就是time_wait状态的端口可以重复使用。setsockopt第二个参数SOL_SOCKET。 
    int flag = 1;
    setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));
    
	//设置完端口复用，再绑定lfd和和其地址信息。 
	int ret = bind(lfd,(struct sockaddr*)&serv,sizeof(serv));
    if(ret == -1)
    {
        perror("bind error");
        exit(1);
    }

    // 设置监听,backlog队列设置为64 
    ret = listen(lfd, 64);
    if(ret == -1)
    {
        perror("listen error");
        exit(1);
    }

    // 把lfd添加到epoll树上，也就是注册lfd到epoll例程（树）上。
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = lfd; 
	ret=epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);
    if(ret == -1)
    {
        perror("epoll_ctl add lfd error");
        exit(1);
    }
    return lfd;
}






// 16进制数转化为10进制
int hexit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return 0;
}

/*
 *  这里的内容是处理%20之类的东西！是"解码"过程。
 *  %20 URL编码中的‘ ’(space)
 *  %21 '!' %22 '"' %23 '#' %24 '$'
 *  %25 '%' %26 '&' %27 ''' %28 '('......
 *  相关知识html中的‘ ’(space)是&nbsp
 */
 
 
 
 
 
void encode_str(char* to, int tosize, const char* from)
{
    int tolen;

    for (tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from) 
    {
        if (isalnum(*from) || strchr("/_.-~", *from) != (char*)0) 
        {
            *to = *from;
            ++to;
            ++tolen;
        } 
        else 
        {
            sprintf(to, "%%%02x", (int) *from & 0xff);
            to += 3;
            tolen += 3;
        }

    }
    *to = '\0';
}





void decode_str(char *to, char *from)
{
    for ( ; *from != '\0'; ++to, ++from  ) 
    {
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) 
        { 

            *to = hexit(from[1])*16 + hexit(from[2]);

            from += 2;                      
        } 
        else
        {
            *to = *from;

        }

    }
    *to = '\0';

}





// 通过文件名获取文件的类型
const char *get_file_type(const char *name)
{
    char* dot;

    // 自右向左查找‘.’字符, 如不存在返回NULL
    dot = strrchr(name, '.');   
    if (dot == NULL)
        return "text/plain; charset=utf-8";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp( dot, ".wav" ) == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}

