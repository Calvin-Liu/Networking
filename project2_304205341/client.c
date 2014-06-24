
/*
 A simple client in the internet domain using TCP
 Usage: ./client hostname port (./client 192.168.0.151 10000)
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>      // define structures like hostent
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <time.h>

void error(char *msg)
{
    perror(msg);
    exit(0);
}

struct packet
{
        int type; //0 == request / 1 == ACK / 2 == DATA / 3 == TEARDOWN
        int length;
        int sequence_number;
        char data[1024];
};

int main(int argc, char *argv[])
{
    srand(time(NULL));
    
    int sockfd; //Socket descriptor
    int portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server; //contains tons of information, including the server's IP address

    if (argc < 5) {
       fprintf(stderr,"usage %s hostname port filename loss_probability corruption_probability\n", argv[0]);
       exit(0);
    }
  
    double loss_probability = atof(argv[4]); 
    double corruption_probability = atof(argv[5]);
  
    portno = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_DGRAM, 0); //create a new socket
    if (sockfd < 0) 
        error("ERROR opening socket\n");
    
    server = gethostbyname(argv[1]); //takes a string like "www.yahoo.com", and returns a struct hostent which contains information, as IP address, address type, the length of the addresses...
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
   
    //create the first request packet that we send to our server
    struct packet curPack;
    curPack.type = 0;
    if(curPack.length = strlen(argv[3]) > 1024)
	error("Too long of a filename.\n");
    curPack.sequence_number = 0;
    strcpy(curPack.data, argv[3]);
    
    //network address settings
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; //initialize server's address
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);
   
    //send the requested filename to the server.
    printf("Requesting file '%s' from server\n", curPack.data);
    sendto(sockfd, &curPack/*buffer*/, 1036/*buffer size*/, 0, (struct sockaddr*)&serv_addr, sizeof(struct sockaddr));

    //now we receive packets from the server and ack them back
    unsigned int sock_addr_length = sizeof(serv_addr);

    FILE *fp = fopen("test.test", "ab+");
    int current_sequence_number = 0;
    int teardown = 0;
    //we keep receiving data for the file until the teardown packet is sent
    while(!teardown)
    {
        recvfrom(sockfd, &curPack, sizeof(curPack), 0, (struct sockaddr *) &serv_addr, &sock_addr_length);
       
        if(curPack.type == 3)
            break; 
        //if our packet is lost
        if(loss_probability == 0)
            ; //do nothing, 0 percent chance of loss
        else if((double)rand()/(double)RAND_MAX <= loss_probability)
            continue; //pretend we didn't get anything and wait for a new packet

        //if our packet is corrupted
        if(corruption_probability == 0)
            ; //do nothing, 0 percent chance of corruption
        else if((double)rand()/(double)RAND_MAX <= corruption_probability)
        {
            //packet is corrupted, send ack for last in-order packet
            curPack.length = 0;
            curPack.type = 1;
            curPack.sequence_number = current_sequence_number;

            printf("Re-sending ACK for last in-order packet (packet corrupted): ACK number %d\n", current_sequence_number);
            sendto(sockfd, &curPack/*buffer*/, 1036/*buffer size*/, 0, (struct sockaddr*)&serv_addr, sizeof(struct sockaddr));        
            continue; //once we send the packet, we end our loop.
        }    
         
        //do this if we are not corrupted or lost	 
        //printf("This is the contents we received from the server: %s\n", curPack.data);
        
        //write the packet to a file
        
        //what to do if the packet is out of order
        if(curPack.sequence_number != current_sequence_number)
        {
            //need to re-ACK the last in-order packet we received and discard current packet
            curPack.length = 0;
            curPack.type = 1;
            curPack.sequence_number = current_sequence_number;
            
            printf("Re-sending ACK for last in-order packet: ACK number %d\n", current_sequence_number);
            sendto(sockfd, &curPack/*buffer*/, 1036/*buffer size*/, 0, (struct sockaddr*)&serv_addr, sizeof(struct sockaddr));        
        } 

        //otherwise it is in order
        else
        {
            int data_received = fwrite(&curPack.data, 1, curPack.length, fp);

            printf("Data received: Sequence number %d\n", current_sequence_number); 
            current_sequence_number += data_received;
            if(curPack.length < 1024)
                teardown = 1;

        
            bzero(curPack.data, 1024);
            curPack.length = 0;
            curPack.sequence_number = current_sequence_number;
                       
            curPack.type = 1; 
            
            printf("ACK sent: ACK number %d\n", current_sequence_number);

            sendto(sockfd, &curPack/*buffer*/, 1036/*buffer size*/, 0, (struct sockaddr*)&serv_addr, sizeof(struct sockaddr));
        }
    }
   
    //send a fin packet
    curPack.type = 3;    
    sendto(sockfd, &curPack/*buffer*/, 1036/*buffer size*/, 0, (struct sockaddr*)&serv_addr, sizeof(struct sockaddr));
    //need to wait for a finack to terminate
    printf("Sent FIN; waiting for FINACK from server.\n");
    curPack.type = -1;
    while(curPack.type != 1)
        recvfrom(sockfd, &curPack, sizeof(curPack), 0, (struct sockaddr *) &serv_addr, &sock_addr_length);	 

    //need to wait for a fin from server
    printf("Waiting for FIN from server.\n");
    curPack.type = -1;
    while(curPack.type != 3)
        recvfrom(sockfd, &curPack, sizeof(curPack), 0, (struct sockaddr *) &serv_addr, &sock_addr_length);	 
    printf("Sending final FINACK to server.\n");
    curPack.type = 1;    
    sendto(sockfd, &curPack/*buffer*/, 1036/*buffer size*/, 0, (struct sockaddr*)&serv_addr, sizeof(struct sockaddr));

    //terminate connection
    close(sockfd);
}
