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
    // ����һ��epoll���ĸ��ڵ�
    int epfd = epoll_create(MAXSIZE);
    if(epfd == -1)
    {
        perror("epoll_create error");
        exit(1);
    }

    // ���Ҫ�����Ľڵ㣬����Ӽ����ڵ㣨�������׽��֣� 
    int lfd = init_listen_fd(port, epfd);

    // ί���ں˼����ӵ����ϵ�ȫ���ڵ�
    struct epoll_event all[MAXSIZE];
    while(1)
    {
    	//  ����ѭ��������ֱ�����׽����¼�������ret�Ƿ����¼��׽��ֵ�����	 
    	int ret=epoll_wait(epfd,all,MAXSIZE,-1);
    	if(ret == -1){
    		perror("there is something wrong with epoll_wait function");
    		exit(1);
		}          

        //  �����������¼��������׽��֡� 
        for(int i=0; i<ret; ++i)
        {
            // ���λ�ȡÿ���׽��ֶ�Ӧ��epoll_event�ṹ���ָ�� 
            struct epoll_event *pev = &all[i];
            if(!(pev->events & EPOLLIN))
            {
                // �������׽��ֵ��¼����Ƕ�����ô��ʱ���ԣ���������ѭ�����鿴��һ���׽��֡� 
                continue;
            }
			
			// �������׽������¼��������������µĿͻ������ӣ�����do_accpt���� 
			if(pev->data.fd == lfd){
				do_accept(lfd,epfd);
			} 		          	
            else
            {
            	// �ͻ��������׽������¼������������ж��¼�������do_read���� 
                do_read(pev->data.fd, epfd);
            }
        }
    }
}





// ������
void do_read(int cfd, int epfd)
{
    // �������������������, ����buf�� 
    char line[1024] = {0};
    
    // ��Ϊ��֪���ͻ��˷������������ж�󣬻����СҲ�������ã�������ÿ�ζ�ȡһ�У�����Ҳ�Ǳ�Ե����
    // �����ȶ�ȡ�����У���������г���Ϊ0����ʾ�ͻ��˷�����EOF��Ҫ�ر�tcp���ӡ� 
    int len = get_line(cfd, line, sizeof(line));
    if(len == 0)
    {
        printf("the client disconnect...\n");
        
        // �ر��׽���, ��epoll����ɾ��cfd 
        disconnect(cfd, epfd);         
    }
    else
    {
    	printf("Request Line: %s",line);
        printf("============= Request Header ============\n");
        
		// ��������û���꣬������ 
        while(len)
        {
        	char buf[1024] = {0};
			len=get_line(cfd,buf,sizeof(buf));
            printf("-----: %s", buf);
            
        }
        printf("============= The End ============\n");
    }

    // ���������У��ж��������ͣ�����ֻ����get����
	// strncasecmp���Ƚ������ַ�����ǰn���ַ��������ִ�Сд�� 
    if(strncasecmp("get", line, 3) == 0)
    {
        // ����ͻ��˴�������get������ô�ʹ�������get���󣬲����� 
        http_request(line, cfd);
        
        // �������˾͹ر��׽���, ��epoll��ɾ��cfd��
		// û��ʵ�ֳ����ӣ�һ������ֻ�ܴ���һ�������ٴ�������Ҫ���½������ӡ� 
        disconnect(cfd, epfd);         
    }
}






// ����http������Ϣ��ÿһ������
int get_line(int sock, char *buf, int size)
{
	// i��buffer���±꣬buffer�Ĵ�С��size��������һ��û���������õ�buffer�� 
    int i = 0;
    char c = '\0';
    int n;
    
    // ��ȡ�����У�һ���Ƕ���\n ʱֹͣwhileѭ����������i==size-1��ʱ��
	// ��ȡ�ϴ�������е�ʱ��һ�����i==size-1��ʱ��ֹͣ��ȡ��Ȼ������buf[size-1]='\0' 
    while ((i < size - 1) && (c != '\n'))
    {
    	// ÿ�ζ�һ���ֽڣ�����������ˣ�����ֵ�϶���1�� 
        n = recv(sock, &c, 1, 0);
        if (n > 0)
        {
        	// �����������\r����ô����һ��Ҫ�����ˣ������ٶ���һ���ֽڣ�Ӧ����\n�ˡ�
            if (c == '\r')
            {
            	// MSG_PEEK��ʾ�ӻ��������Կ����ķ�ʽ�����ݣ�����̽�Եض��� 
				// ���\r��ߵ���\n����ô�ҾͰ������������������\n��ô�Ͳ��������������Լ�д��\n���������� 
                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))
                {
                    recv(sock, &c, 1, 0);
                }
                else
                {
                	//�����������\n����ô�Ҿͷ�һ��\n��buffer��ߡ� 
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
    // �˳�whileѭ����һ�ж�ȡ��ϣ�����ȡ�����ַ������׺��һ��\0 
    buf[i] = '\0';

    return i;
}






// �Ͽ��ͻ��˵����ӣ��ر��׽��� 
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





// http������
void http_request(const char* request, int cfd)
{
    // ���http������
   	char method[12] ,path[1024] ,protocol[12];
   	// ʹ��sscanf������������� 
   	sscanf(request,"%[^ ] %[^ ] %[^ ]", method, path,protocol);     
    printf("method = %s, path = %s, protocol = %s\n", method, path, protocol);

    // ���룬������������������͵��ļ�ʱ����Ҫ���н���
    decode_str(path, path);        
    // ����path��ȥ��path(/xx)�е�/��ǰ���������Ѿ����뵽����Ŀ¼������ˡ� 
    // ��Ϊpath��ָ���ַ������ָ�룬ֻҪ���ָ������ƶ�һ��λ�ã����ܵ����ļ�����ʼ��ַ�� 
    char* file = path+1;
    // ���û��ָ�����ʵ���Դ, Ĭ����ʾ��ԴĿ¼�е�����
    if(strcmp(path, "/") == 0)
    {
        // file��ֵ, ��ԴĿ¼�ĵ�ǰλ��
        file = "./";
    }

    // ��ȡ�ļ����ԣ�����һ��stat��������statϵͳ���ð��ļ�����Ϣ�ŵ��ṹ��stat�С�
	struct stat st;
	int ret =  stat(file,&st);
    if(ret == -1)
    {
        // ���stat����-1��˵���ļ�/Ŀ¼�����ڣ����ͻ��˷���404ҳ�档 
        send_respond_head(cfd, 404, "File Not Found", ".html", -1);
        send_file(cfd, "404.html");
    }

    // S_ISDIR���ж��Ƿ���Ŀ¼������������ <sys/stat.h>ͷ�ļ��С� 
    if(S_ISDIR(st.st_mode))
    {
        // ������Ӧͷ��Ŀ¼��Ϣ 
        send_respond_head(cfd, 200, "OK", get_file_type(".html"), -1);
        send_dir(cfd, file);
    }
    // S_ISDIR���ж��Ƿ�����ͨ�ļ� 
    else if(S_ISREG(st.st_mode))
    {
        // ������Ӧͷ���ļ����� 
        send_respond_head(cfd, 200, "OK", get_file_type(file), st.st_size);
        // �����ļ�����
        send_file(cfd, file);
    }
}





// ������Ӧͷ
void send_respond_head(int cfd, int no, const char* desp, const char* type, long len)
{  

    // ����״̬�С�sprintf�����Ѹ������͵����ַ�����buf�С� 
    char buf[1024] = {0};
    sprintf(buf, "http/1.1 %d %s\r\n", no, desp);
    send(cfd, buf, strlen(buf), 0);
    
    // ������Ӧͷ
    sprintf(buf,"Content-type:%s\r\n",type);
    sprintf(buf+strlen(buf),"Content-Length:%ld\r\n",len);
    send(cfd, buf, strlen(buf), 0);
    	
    // ����һ������
    send(cfd,"\r\n", 2, 0); 
}





// ����Ŀ¼����
void send_dir(int cfd, const char* dirname)
{
    // ƴһ��htmlҳ��<table></table>
    char buf[4094] = {0};
	
	// sprintf��������Ѹ�ʽ������ַ�����ߡ� 
    sprintf(buf, "<html><head><title>Ŀ¼��: %s</title></head>", dirname);
    sprintf(buf+strlen(buf), "<body><h1>��ǰĿ¼: %s</h1><table>", dirname);

    char enstr[1024] = {0};
    char path[1024] = {0};
    
    //  ���涨�壺ptr��һ��ָ�룬ָ��dirent[]	 
    struct dirent** ptr;
    
	// scandir����ֵ��Ŀ¼��ĸ����� ���е�Ŀ¼���ļ������ŵ���ptrָ���һ���ṹ��������ߡ� 
    int num = scandir(dirname, &ptr, NULL, alphasort);
    
	// ����dirent�ṹ������ 
    for(int i=0; i<num; ++i)
    {
    	char* name = ptr[i]->d_name;

        // ƴ���ļ�������·������Ϊ��ǰ���̲�û�н��뵽dirname���ȥ�����Ǻ�dirname����ͬһ��Ŀ¼�¡� 
        sprintf(path, "%s/%s", dirname,name);
        printf("path =%s========\n",path);
        struct stat st;
        stat(path,&st);
		// �����ļ���ʱ����б��� 
        encode_str(enstr, sizeof(enstr), name);
        // ������ļ�
        if(S_ISREG(st.st_mode))
        {
        	// �±߸�ʽ�ַ����У���һ��a��ǩ����������˫����Ҫ�÷�б�ܽ���ת�壬��Ȼ������ڸ�ʽ�ַ�����˫���ų�ͻ�� 
            sprintf(buf+strlen(buf),             
                    "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
                    enstr, name, (long)st.st_size);
        }
        // �����Ŀ¼
        else if(S_ISDIR(st.st_mode))
        {
            sprintf(buf+strlen(buf), 
                    "<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>",
                    enstr, name, (long)st.st_size);
        }
        send(cfd, buf, strlen(buf), 0);
        memset(buf, 0, sizeof(buf));
        // �ַ���ƴ��
    }

    sprintf(buf+strlen(buf), "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);

    printf("dir message send OK!!!!\n");
    
#if 0

    // ʹ��opendirϵͳ���ô�Ŀ¼�����ص���һ��DIR�� 
	DIR* dir = opendir(dirname); 
    if(dir == NULL)
    {
        perror("opendir error");
        exit(1);
    }

    // ʹ��readdirϵͳ���ö�Ŀ¼������DIR�����ص���dirent����һ��Ŀ¼����Ŀ��
	struct dirent* ptr = NULL;
	while((ptr=readdir(dir)) != NULL) {
		char* name=ptr->d_name;
	}
    closedir(dir);
#endif
}






// �����ļ�
void send_file(int cfd,const char * filename)
{
    // ���ļ�
    int fd = open(filename, O_RDONLY);
    if(fd == -1)
    {
        return;
    }

    // ѭ�����ļ������ļ�һ�ζ�buf��С��buf����һ�Σ�����һ�Ρ� 
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


// ���������Ӵ���
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

    // ��ӡ�ͻ�����Ϣ
    char ip[64] = {0};
    printf("New Client IP: %s, Port: %d, cfd = %d\n",
           inet_ntop(AF_INET, &client.sin_addr.s_addr, ip, sizeof(ip)),
           ntohs(client.sin_port), cfd);

    // ����cfdΪ������
    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(cfd, F_SETFL, flag);

    // �õ����½ڵ�ҵ�epoll����
    struct epoll_event ev;
    ev.data.fd = cfd;
    // ���ط�����ģʽ
    ev.events = EPOLLIN | EPOLLET;
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
    if(ret == -1)
    {
        perror("epoll_ctl add cfd error");
        exit(1);
    }
}






//  ��ʼ����������listen���׽���lfd
int init_listen_fd(int port, int epfd)
{
    // ����listen���׽���lfd
    int lfd = socket(AF_INET,SOCK_STREAM,0) ;
    if(lfd == -1)
    {
        perror("socket error");
        exit(1);
    }

    // ������������socket�ĵ�ַ�Ͷ˿���Ϣ 
    struct sockaddr_in serv;
    memset(&serv,0,sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_port = htons(port);
	serv.sin_addr.s_addr = htonl(INADDR_ANY); 	


    // �����׽���ѡ���еĶ˿ڸ��ã�Ҳ����time_wait״̬�Ķ˿ڿ����ظ�ʹ�á�setsockopt�ڶ�������SOL_SOCKET�� 
    int flag = 1;
    setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));
    
	//������˿ڸ��ã��ٰ�lfd�ͺ����ַ��Ϣ�� 
	int ret = bind(lfd,(struct sockaddr*)&serv,sizeof(serv));
    if(ret == -1)
    {
        perror("bind error");
        exit(1);
    }

    // ���ü���,backlog��������Ϊ64 
    ret = listen(lfd, 64);
    if(ret == -1)
    {
        perror("listen error");
        exit(1);
    }

    // ��lfd��ӵ�epoll���ϣ�Ҳ����ע��lfd��epoll���̣������ϡ�
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






// 16������ת��Ϊ10����
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
 *  ����������Ǵ���%20֮��Ķ�������"����"���̡�
 *  %20 URL�����еġ� ��(space)
 *  %21 '!' %22 '"' %23 '#' %24 '$'
 *  %25 '%' %26 '&' %27 ''' %28 '('......
 *  ���֪ʶhtml�еġ� ��(space)��&nbsp
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





// ͨ���ļ�����ȡ�ļ�������
const char *get_file_type(const char *name)
{
    char* dot;

    // ����������ҡ�.���ַ�, �粻���ڷ���NULL
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

