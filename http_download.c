#include<stdio.h>
#include<netinet/in.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<errno.h>
#include<netdb.h>
#include<unistd.h>
#include <sys/ioctl.h>


struct HTTP_RES_HEADER
{

    int status_code;//HTTP/1.1 '200' OK

    char content_type[128];//Content-Type: application/gzip

    long content_length;//Content-Length: 11683079

};


int split_url(char *url, char *host, int *port, char *filepath, char *filename) {
	char *start = strstr(url, "//");
	start += 2;
	char *host_end = strstr(start, "/");
	strcpy(filepath, host_end);
	
	char *port_end = strstr(start, ":");
	if (port_end != NULL) {
		strncpy(host, start, port_end - start);
		host[port_end - start + 1] = '\0';
		*port = atoi(port_end + 1);
	}else {
		strncpy(host, start , host_end - start);
		host[host_end - start + 1] = '\0';
		*port = 80;
	}

	int i,j=0;
	char *p = filepath + 1;
	for (i = 0; i< strlen(filepath) - 1; i++) {
		if (*p != '/') {
			filename[j++] = *p++;
		}
		else {
			j=0;
			p++;
		}
	}
	filename[j] = '\0';
	printf("host : %s, port : %d, filepath : %s, filename : %s\n", host, *port, filepath, filename);
	return 0;
}

int create_socket(char *host, int port, char *ip) {
	int sockfd, ret, recvbuf, error,len;
	int nonblock = 1;
	struct addrinfo hints, *res, *ressave;
	fd_set wset;
	struct timeval tval;
	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	char cport[4] = {0};
	sprintf(cport, "%d", port);
	printf("create socket, host : %s, port : %s\n", host, cport);
	
	if((ret = getaddrinfo(host, cport, &hints, &res)) != 0) {
		printf("getaddrinfo error: %d, %s\n", errno, strerror(errno));
		return ret;
	}
	ressave = res;
	do {
		sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (sockfd < 0)
			continue;

		recvbuf = 5 * 1024 * 1024;
		setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &recvbuf, sizeof(recvbuf));

		ioctl(sockfd, FIONBIO, &nonblock);

		if ((ret = connect(sockfd, res->ai_addr, res->ai_addrlen)) == 0) {
			printf("connect success");
			break;
		}

		if (errno == EINPROGRESS) {
			FD_ZERO(&wset);
			FD_SET(sockfd, &wset);
			tval.tv_sec = 5;
			tval.tv_usec = 0;

			ret = select(sockfd + 1, NULL, &wset, NULL, &tval);
			if (ret > 0) {
				if (FD_ISSET(sockfd, &wset)) {
					len = sizeof(error);
					if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && !error){
						printf("getsockopt, no error, connect success\n");
						break;
					}
				}
			}
		} 
		close(sockfd);
	}while((res = res->ai_next) != NULL);

	if (res == NULL) {
		printf("connect error : %s, %d\n", host, port);
		return -1;
	}

	inet_ntop(res->ai_family, &res->ai_addr, ip, 256);
	printf("ip : %s\n", ip);

	freeaddrinfo(ressave);
	printf("sockfd : %d\n", sockfd);
	return sockfd;
}

int sendData(int fd, const char *data) {
	fd_set wset, eset;
	int i = 0;
	int ret;
	int len = strlen(data);
	struct timeval tval;

	while(i < len) {
		FD_ZERO(&wset);		
		FD_SET(fd, &wset);
		FD_ZERO(&eset);
		FD_SET(fd, &eset);

		tval.tv_sec = 3;
		tval.tv_usec = 0;

		ret = select(fd + 1, NULL, &wset, &eset, &tval);
		printf("select ret : %d\n", ret);
		if (ret > 0 && FD_ISSET(fd, &wset)) {
			ret = send(fd, data + i, len - i, 0);
			printf("send ret: %d\n", ret);
		}

		if (ret < 0 || FD_ISSET(fd, &eset)) {
			printf("send data failed\n");
			return -1;
		}

		if (ret == 0) {
			printf("select timeout\n");
		}
		i += ret;
	}
	return 0;
}

int recvData(int fd, char *data, int size) {
	fd_set rset, eset;
	int ret;
	struct timeval tval;

	FD_ZERO(&rset);
	FD_ZERO(&eset);
	FD_SET(fd, &rset);
	FD_SET(fd, &eset);

	tval.tv_sec = 1;
	tval.tv_usec = 0;
	
	ret = select(fd + 1, &rset, NULL, &eset, &tval);
	if (ret > 0 && FD_ISSET(fd, &rset)) {
		ret = recv(fd, data, size, 0);
		return ret;
	}

	printf("recv data failed, %s\n", strerror(errno));
	return -1;
}

struct HTTP_RES_HEADER parse_header(const char *response)

{

    /*获取响应头的信息*/

    struct HTTP_RES_HEADER resp;



    char *pos = strstr(response, "HTTP/");

    if (pos)//获取返回代码

        sscanf(pos, "%*s %d", &resp.status_code);



    pos = strstr(response, "Content-Type:");

    if (pos)//获取返回文档类型

        sscanf(pos, "%*s %s", resp.content_type);



    pos = strstr(response, "Content-Length:");

    if (pos)//获取返回文档长度

        sscanf(pos, "%*s %ld", &resp.content_length);



    return resp;

}




int download(int fd, char *host, char *filepath, char *filename, char *ip, int port) {
	char header[2048] = {0};
	char response[2048] = {0};
	char *p = response;
	int ret;
	FILE *fp;
	int recvlen = 0;
	char buffer[4096];
	snprintf(header, 2048, \
		"GET %s HTTP/1.1\r\n"\
		"Accept: */*\r\n"\
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537(KHTML, like Gecko) Chrome/47.0.2526Safari/537.36\r\n"\
		"Host: %s\r\n"\
		"Connection: close\r\n"\
		"\r\n", filepath, host);

	printf("before , header: %s\n", header);
	sendData(fd, header);
	printf("sendData done : %s\n", header);

	while(1) {
		ret = recvData(fd, p, 1);
		if (ret != 1) {
			printf("recvData error\n");
			break;
		}
		if (*(p-3) == '\r' && *(p-2) == '\n' && *(p-1) == '\r' && *p == '\n') {
			printf("recv complete http response\n");
			p++;
			break;
		}
		p++;
	}
	*p = '\0';
	printf("response : %s\n", response);

	struct HTTP_RES_HEADER resp = parse_header(response);
	printf("response, status code : %d, content type : %s, content length : %ld\n", resp.status_code, resp.content_type, resp.content_length);

	if (resp.status_code != 200) {
		printf("status code error\n");
		return -1;
	}

	ret = access(filename, F_OK);
	if (!ret) {
		printf("file exists, remove\n");
		remove(filename);
	}

	fp = fopen(filename, "ab+");
	if (fp == NULL) {
		printf("fopen %s failed\n", filename);
		return -1;
	}

	while (recvlen < resp.content_length) {
		ret = recvData(fd, buffer, sizeof(buffer));
		if (ret <= 0) {
			printf("recv data failed\n");
			break;
		}
		fwrite(buffer, ret, 1, fp);
		recvlen += ret;
		//printf("recvlen : %d\n", recvlen);
	}
	printf("recv done\n");
	fclose(fp);
	return 0;
}

int main() {
	char *downloadUrl = "http://www.live555.com/liveMedia/public/live.2020.07.21.tar.gz"; //live555下载链接
	char host[256] = {0};
	char ip[256] = {0};
	int port = 0;
	char filepath[1024] = {0};
	char filename[1024] = {0};
	int sockfd;
	split_url(downloadUrl, host, &port, filepath, filename);
	sockfd = create_socket(host, port, ip);
	download(sockfd, host, filepath, filename, ip, port);
	return 0;
}
