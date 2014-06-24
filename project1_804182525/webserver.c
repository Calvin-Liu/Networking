/* A simple server in the internet domain using TCP
   The port number is passed as an argument 
   This version runs forever, forking off a separate 
   process for each connection
*/
#include <stdio.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <stdlib.h>
#include <strings.h>
#include <sys/wait.h>   /* for the waitpid() system call */
#include <signal.h>     /* signal name macros, and the kill() prototype */

#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>

void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

void error(char *msg)
{
	perror(msg);
	exit(1);
}

void contentType(char* type, char* filename)
{
	if(strstr(filename, ".html") != NULL)
		strcpy(type, "text/html; charset=UTF-8\n");
	else if(strstr(filename, ".gif") != NULL)
		strcpy(type, "image/gif\n");
	else if(strstr(filename, ".jpeg") != NULL)
		strcpy(type, "image/jpeg\n");
	else if(strstr(filename, ".jpg") != NULL)
		strcpy(type, "image/jpg\n");
	else if(strstr(filename, ".txt") != NULL)
		strcpy(type, "text/plain\n");
		return;
}

void header(int socket, int status, char* contentType, int contentLength)
{
	write(socket, "HTTP/1.1 ", 9);
	
	//status
	char statusString[128];
	switch(status)
	{
		case 200:
			strcpy(statusString, "200 OK");
			break;
		case 404:
			strcpy(statusString, "404 Not Found");
			break;
		case 500:
			strcpy(statusString, "500 Internal Server Error");
		default:
			strcpy(statusString, "Unknown error type");
	}

	write(socket, statusString, (int) strlen(statusString)+1);
	
	//content language
	write(socket, "\nContent-Language: en-US\n", 25);

	//content type
	write(socket, "Content-Type: ", 14);
	write(socket, contentType, strlen(contentType));
	
	//contentLength
	char length[1024];
	sprintf(length, "Content-Length;%d", contentLength);
	write(socket, length, strlen(length));
	
	//connection
	write(socket, "\nConnection: keep-alive\n\n", 25);
}

void parseRequest(char* buffer, char* httpRequest)
{
	//filename that follows GET response
	char* substring = strstr(httpRequest, "GET /");
	if(substring != NULL)
	{
		//now for the filename
		char* substringEnd = strstr(substring, "HTTP");
		substring = &substring[5];
		int querySize = strlen(substring) - strlen(substringEnd) - 1;
		strncpy(buffer, substring, querySize);
		buffer[querySize] = '\0';
	}
	return;
}

void sendFile(int socket, FILE * fd)
{
	char buffer[1024];
	long int dataLength;
	//iterate and send byte by byte
	while(dataLength = fread(buffer, 1, 1024, fd))
		write(socket, buffer, dataLength);
	if(ferror(fd))
		fprintf(stderr, "read error\n");
}

void dostuff(int socket)
{
	int n;
	char buffer[1024];
	
	//read in HTTP request
	bzero(buffer, 1024); //place 1024 zero-valued bytes in the area pointed by buffer
	n = read(socket, buffer, 1023); //read the packet into the buffer
	if(n < 0)
		error("ERROR reading from socket");

	//dump request to console
	printf("Here is the message: %s\n", buffer);
	char file[1024];
	bzero(file, 1024);
	
	//get name of the requested file
	parseRequest(file, buffer);

	char testMessage[1024];
	bzero(testMessage, 1024);
	int dataLength;

	FILE* fd = fopen(file, "r");

	if(fd == NULL)
	{
		header(socket, 404, "text/html", 0);
		printf("Failed to open '%s'", file);	
	}
	else //send header and file
	{
		//find file length
		fseek(fd, 0L, SEEK_END);
		dataLength = ftell(fd);
		
		//set pointer back to the beginning
		fseek(fd, 0L, SEEK_SET);
		
		char content[1024];
		contentType(content, file);
		
		//send all the file bytes
		header(socket, 200, content, dataLength);		
		sendFile(socket, fd);

		fclose(fd);	
	}

	if(n < 0)
		error("ERROR writing to socket");
}

int main(int argc, char *argv[])
{
	int sockfd, newsockfd, portno, pid; //listen on sock_fd, new connection on newsockfd
	socklen_t clilen;
	struct sockaddr_in serv_addr, cli_addr; //connector's address info
	struct sigaction sa; //for signal SIGCHLD
	
	if(argc < 2)
	{
		fprintf(stderr, "ERROR, no port provided\n");
		exit(1);
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0)
		error("ERROR opening socket");
	bzero((char *) &serv_addr, sizeof(serv_addr));
	portno = atoi(argv[1]);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portno);

	if(bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
		error("ERROR on binding");

	listen(sockfd, 5);
	clilen = sizeof(cli_addr);

	/****************Kill Zombie Processes***********/
	sa.sa_handler = sigchld_handler; //reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if(sigaction(SIGCHLD, &sa, NULL) == -1)
	{
		perror("sigaction");
		exit(1);
	}
	/************************************************/
	
	while(1) //main accept() loop
	{
		newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
		if(newsockfd < 0)
			error("ERROR on accept");

		pid = fork();
		if(pid < 0)
			error("ERROR on fork");

		if(pid == 0) //this is the child process
		{			
			close(sockfd); //child doesn't need the listener
			dostuff(newsockfd);
			exit(0);
		}
		else
			close(newsockfd); //parent doesn't need this
	}

	return 0;
}
