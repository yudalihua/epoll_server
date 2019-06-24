#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "epoll_server.h"

int main(int argc, const char* argv[])
{
    if(argc < 3)
    {
        printf("eg: ./a.out port path\n");
        exit(1);
    }

    // �Ѷ˿�ת����int�� 
    int port = atoi(argv[1]);
    
    // �޸Ľ��̵Ĺ���Ŀ¼, �������������ֱ��ʹ���ļ��Ϳ��ԣ�������ȥ��Ŀ¼��ƴ�� �� 
    int ret = chdir(argv[2]);
    if(ret == -1)
    {
        perror("chdir error");
        exit(1);
    }
    
    // ����epollģ�� 
    epoll_run(port);

    return 0;
}

