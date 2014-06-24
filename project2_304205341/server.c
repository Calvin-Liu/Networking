#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h> 
#include <string.h> 
#include <unistd.h> 
#include <errno.h>
#include <memory.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <fcntl.h>

struct packet
{
	//0 - request
	//1 - ACK
	//2 - data
	//3 - FIN
	//for ACK, data should be empty
	int type;
	int length;
	int sequence_number;
	char data[1024];
};



/**********************************************
TODO:
-How to check if packets are out of order
-Messages are not printing sent,sent,sent,recv,recv,recv
************************************************/

int main(int argc, char *argv[])
{
    srand(time(NULL));
	/***
	* for printing number of arguments
	* fprintf(stderr, "%d", argc);
	****/
	int TIMEOUT = 5;
	if(argc != 5)
	{
		fprintf(stderr, "Arguments expected: <portnumber> <congestion window size> <probability loss> <probability corruption>\n");
		exit(1);
	}
	unsigned short port_number = atoi(argv[1]);
	int congestion_window_size = atoi(argv[2]);
	double probability_loss = atof(argv[3]);
	double probability_corruption = atof(argv[4]);
	int sock;
	struct sockaddr_in serv_addr;
	struct sockaddr_in client_addr;
	unsigned int client_addr_length = sizeof(client_addr);

	if(port_number < 1000)
	{
		fprintf(stderr, "Please enter a port number that is greater than 1000\n");
		exit(1);
	}
	if(congestion_window_size < 0)
	{
		fprintf(stderr,"Window size must at least be 1\n");
		exit(1);
	}	
	if(probability_loss > 1 || probability_corruption > 1 || probability_loss < 0 || probability_corruption < 0)
	{
		fprintf(stderr, "Probabilities must be between 0 and 1\n");
		exit(1);
	}

	//Request the file
	fprintf(stdout, "Waiting for file request....\n");

	//Set up the socket
	if((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		error("Error: cannot open socket");
	bzero(&serv_addr, sizeof(serv_addr));
	memset(&serv_addr, 0, sizeof(&serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port_number);

	if(bind(sock,(struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
		error("Error: cannot bind");

	int FIN = 0;
	int once = 0;
	int size_of_file_packets;

	struct packet req_packet;

	//Just for knowing the filename (first packet)
	if(recvfrom(sock, &req_packet, sizeof(req_packet), 0, (struct sockaddr *) &client_addr, &client_addr_length) < 0)
	{
        error("Error on receiving packet from client");
        exit(1);
	}

	//Keep track of the sequence number+ACK	
	int packet_seq_n = req_packet.sequence_number;
	//Fix me: Do I need to add 1 to the ACK number
	int packet_ack_n = (req_packet.sequence_number + strlen(req_packet.data));
	int size_of_first_packet = packet_ack_n;

    int fin_received = 0;
	//Message only for first data packet
	//seq = size of file length. ack = 0;
	fprintf(stdout, "DATA received seq#%d, ACK#%d, FIN %d, content-length:%d\n", packet_seq_n, packet_ack_n, FIN, strlen(req_packet.data));
	//Open the file to send packets to the client. Do this only once
	FILE* fd = fopen(req_packet.data, "r");
    unsigned int size_of_file = 0;
    if(fd == NULL)
    {
        fin_received = 1;
        printf("Error: could not open file.\n");
        req_packet.type = 3;
        sendto(sock, &req_packet, 1036, 0, (struct sockaddr *) &client_addr, client_addr_length);
    }
    else
    {   
	    fseek(fd, 0, SEEK_END);
	    size_of_file = ftell(fd);
	    fseek(fd, 0, SEEK_SET);
    }
	//Print the file name requested
	fprintf(stdout, "File requested is %s\n", req_packet.data);
	fprintf(stdout, "---------------------------------------------------------------\n");
	
	size_of_file_packets = size_of_file/1024 + 1;
	if(size_of_file % 1024 == 0)
		size_of_file_packets--;
	
	int buffer_window_size = (congestion_window_size * 1024); 
	char buffer_window[buffer_window_size];
	int buffer_window_offset = 0;
	int num_packets_received = 0;
	int num_packets_sent = 0;
	int num_pack_recv_in_window = 0;

	//fcntl(fd, F_SETFL, O_NONBLOCK);
	//Need to be able to send multiple packets and ACKs at the same time
	time_t timer = time(NULL);
    int sequence_number = 0;
    int window_end = congestion_window_size;
    int current_packet_position = 0;
    int data_length;
    int packets_acked = 0; //use this as condition for exiting loop!
    struct packet ack_packet;
    while(packets_acked < size_of_file_packets - 1)//change this check to ack of end of file
    {
        if(fin_received == 1)
            break;

        if(current_packet_position < window_end) //only send a packet if we are within our window
        {
            req_packet.type = 2; //2 for data being sent       
            bzero(req_packet.data, 1024);
            //send file data
            req_packet.sequence_number = sequence_number;
                data_length = fread (req_packet.data, 1, 1024, fd);
                    
                    
            req_packet.length = data_length;
            if(sendto(sock, &req_packet, sizeof(req_packet), 0, (struct sockaddr *) &client_addr, client_addr_length) < 0)
            {
                printf("Error in sending the file\n");
                exit(1);
            }
            current_packet_position++;

            fprintf(stdout, "DATA sent seq#%d, content-length:%d\n", sequence_number, data_length);

            sequence_number += data_length;
        }

        //check for an ACK

        fcntl(sock, F_SETFL, O_NONBLOCK); //set our recvfrom to NON-BLOCKING i.e. returns -1 if no data yet
        if(time(NULL) <= timer + TIMEOUT) //if we haven't timed out yet
        {
            if(recvfrom(sock, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *) &client_addr, &client_addr_length) < 0)
            {
                continue; //nothing to do except send another packet and then check again for an ACK
            }
            else //this means we have received an ACK!
            {
                if(ack_packet.type == 3)
                {
                    fin_received = 1;
                    break;
                }
                if(probability_loss == 0)
                    ; //do nothing, 0 percent chance of loss
                else if((double)rand()/(double)RAND_MAX <= probability_loss)
                    continue; //pretend we didn't get anything and wait for a new packet
                //if our packet is corrupted
                if(probability_corruption == 0)
                    ; //do nothing, 0 percent chance of corruption
                else if((double)rand()/(double)RAND_MAX <= probability_corruption)
                    continue; //nothing we can do for packet corruption except timeout or wait for another ack
 

                
                fprintf(stdout, "ACK received ACK#%d\n", ack_packet.sequence_number);
                timer = time(NULL); //reset the timer
                int packet_being_acked = (ack_packet.sequence_number/1024) - 1;
                if(ack_packet.sequence_number % 1024 != 0)//end packet
                    packet_being_acked++;
                if(packet_being_acked >= window_end - congestion_window_size) // if the packet being acked is relevant
                {
                    window_end = packet_being_acked + 1 + congestion_window_size; // move the window end to the correct spot
                    fprintf(stdout, "sliding window...\n");
                    packets_acked = packet_being_acked;   
                    //otherwise we don't have to do anything with our ack packet
                }
            }
        }
        else //we have timed out
        {
            current_packet_position = window_end - congestion_window_size;//put the packet position in the right spot
            int data_offset = current_packet_position *1024; 
            fseek(fd, data_offset, SEEK_SET); //put the file pointer in the correct position to resend window
            sequence_number = data_offset;
            timer = time(NULL); //reset the timer       
        }
    }

    //We have stopped sending the file at this point. We are beginning to close connection.
    //printf("Comes out of while loop\n");	
    struct packet closing_fin;
    while(!fin_received)
    {
        recvfrom(sock, &closing_fin, sizeof(closing_fin), 0, (struct sockaddr *) &client_addr, &client_addr_length);
        if(closing_fin.type == 1)
            printf("ACK received ACK#%d\n", closing_fin.sequence_number);
        if(closing_fin.type == 3)
            fin_received = 1;
    }
        printf("Received FIN\n");
        //	printf("It is a FIN type\n");
        closing_fin.type = 1;
        printf("Sending FINACK.\n");
        sendto(sock, &closing_fin, sizeof(closing_fin), 0, (struct sockaddr *) &client_addr, client_addr_length);
        //	printf("Sending ACK\n");
        closing_fin.type = 3;
        printf("Sending FIN.\n");
        sendto(sock, &closing_fin, sizeof(closing_fin), 0, (struct sockaddr *) &client_addr, client_addr_length);
        //	printf("Sending FIN\n");
        recvfrom(sock, &closing_fin, sizeof(closing_fin), 0, (struct sockaddr *) &client_addr, &client_addr_length);
        printf("Received final FINACK; closing connection.\n");
        //	printf("Received FIN\n");
    close(sock);	
    return 0;
}
