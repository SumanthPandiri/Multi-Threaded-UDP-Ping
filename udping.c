#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "Practical.h"
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h> 

 
//initialize the condition and mutex variables to the client send and client receive functions
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
struct timespec timeSent;


//This is the struct that holds the command line arguments
struct cmd_settings {
    char* maxPackets;
    char* pingInterval;
    char* size;
    char* portNumber;
    char* ip;
    bool printMode;
    bool serverMode;

};
struct cmd_settings settings;

//overall statistics for summary printing, made global for ease
int packetSentCount = 0;
int packetRecievedCount = 0;
double totalTime = 0;
float minTime = 0;
float maxTime = 0;

//struct to hold the socket
struct socket_args {
    struct addrinfo *servAddr;
    struct sockaddr_storage fromAddr;
    socklen_t fromAddrLen;
    int sock;
};


//signal exits the program and prints the statistics if it is in client mode
void printstats() {
    if (settings.serverMode) {
        exit(1);
    }   

    printf("\n%d packets transmitted, %d received, %d%% packet loss, time %lf ms\n", packetSentCount, packetRecievedCount, 
    (int)(100*(float)((packetSentCount - packetRecievedCount)/packetSentCount)), totalTime);
    printf("rtt min/avg/max = %.3f/%.3f/%.3f msec\n\n", minTime, (float)totalTime/packetRecievedCount, maxTime);

    exit(1);
}

//This function sends the ping packets to the server
void* clientUDPSend(void* argu) {
    struct socket_args* arg = (struct socket_args*) argu;
    //assigns the server ip
    pthread_mutex_lock(&mutex);

    //this creates the packet using size specified
    int echoStringLen = atoi(settings.size);
    char *echoString = malloc((echoStringLen) * sizeof(char));
    for(int i=0 ; i < echoStringLen; i++) {
      echoString[i] = 'a';
    }
    
    //Loop to send the ping packets to the server using pthread_cond_timedwait to time the waits
    for (int count = 0; count < atoi(settings.maxPackets); count++) {
        // Send the string to the server
        int numBytes = sendto(arg->sock, echoString, echoStringLen, 0,
            arg->servAddr->ai_addr, arg->servAddr->ai_addrlen);
        if (numBytes < 0)
            DieWithSystemMessage("sendto() failed");
        else if (numBytes != echoStringLen)
            DieWithUserMessage("sendto()", "sent unexpected number of bytes");

        //this is the time sent global variable getting updated
        clock_gettime(CLOCK_REALTIME, &timeSent);

        //increments the global variable
        packetSentCount++;
        //this is the time to wait
        struct timespec waitTime;
        double decWait = atof(settings.pingInterval) - (int)atof(settings.pingInterval);

        waitTime.tv_sec = timeSent.tv_sec + (int)atof(settings.pingInterval);
        if ((timeSent.tv_nsec + (decWait * 1000000000.0))>1000000000) {
            waitTime.tv_sec += 1;
            waitTime.tv_nsec = timeSent.tv_nsec + (decWait * 1000000000.0) - 1000000000;
        }
        else {
            waitTime.tv_nsec = timeSent.tv_nsec + (decWait * 1000000000.0);
        }
        
        //The function is locked until it gets opened up by the receive function
        pthread_cond_timedwait(&cond, &mutex, &waitTime);
    }
    
    pthread_mutex_unlock(&mutex);
    return 0;
}



//function to recieve the pings from the server and compute round trip time using clock_gettime
void* clientUDPReceive(void* argu) {
    struct socket_args* arg = (struct socket_args*)argu;
    int echoStringLen = atoi(settings.size);
    char buffer[echoStringLen]; 

    
    //loop to recieve the response from server and verify the reception from expected source and print the data
    for (packetRecievedCount = 0; packetRecievedCount < atoi(settings.maxPackets); packetRecievedCount++) {
     // Block until receive message from a client
        // I/O buffer
        int numBytes = recvfrom(arg->sock, buffer, echoStringLen, 0, (struct sockaddr *) &arg->fromAddr, &arg->fromAddrLen);
        if (numBytes <= 0)
            DieWithSystemMessage("recvfrom.() failed");
        else if (numBytes != echoStringLen) {
            printf("StringLen = %d, Received %d bytes \n", echoStringLen, numBytes);
            DieWithUserMessage("recvfrom()", "received unexpected number of bytes");
        }
        
        //calculates Round trip times and print them
        struct timespec timeReceived;
        clock_gettime(CLOCK_REALTIME, &timeReceived);
        double seconds = (double)(timeReceived.tv_sec - timeSent.tv_sec);
        double nano = (timeReceived.tv_nsec - timeSent.tv_nsec);
        if ((timeReceived.tv_nsec - timeSent.tv_nsec) < 0) {
            seconds--;
            nano = (timeReceived.tv_nsec + 1000000000 - timeSent.tv_nsec);
        }
        double milli = nano / 1000000;
        double rtt = seconds + milli;
        if (settings.printMode) {
            printf("%-5d %-5s %f\n", packetRecievedCount+1, settings.size, rtt);
        }

        //updates the overall statistics
        totalTime += rtt;
        if (minTime == 0) {
            minTime = rtt;
        }
        if (rtt < minTime) {
            minTime = rtt;
        }
        if (rtt > maxTime) {
            maxTime = rtt;
        }

        //continue the send function - uncomment this to skip the wait if packet is allready recieved
        //pthread_cond_signal(&cond);
    }

    close(arg->sock);
    return 0;
}



// Function that the second thread will execute
void* serverUDP() {
    char *service = settings.portNumber;

    // Construct the server address structure
    struct addrinfo addrCriteria;                   // Criteria for address
    memset(&addrCriteria, 0, sizeof(addrCriteria)); // Zero out structure
    addrCriteria.ai_family = AF_UNSPEC;             // Any address family
    addrCriteria.ai_flags = AI_PASSIVE;             // Accept on any address/port
    addrCriteria.ai_socktype = SOCK_DGRAM;          // Only datagram socket
    addrCriteria.ai_protocol = IPPROTO_UDP;         // Only UDP socket

    struct addrinfo *servAddr; // List of server addresses
    int rtnVal = getaddrinfo(NULL, service, &addrCriteria, &servAddr);
    if (rtnVal != 0)
        DieWithUserMessage("getaddrinfo() failed", gai_strerror(rtnVal));

    // Create socket for incoming connections
    int sock = socket(servAddr->ai_family, servAddr->ai_socktype,
        servAddr->ai_protocol);
    if (sock < 0)
        DieWithSystemMessage("socket() failed");

    // Bind to the local address
    if (bind(sock, servAddr->ai_addr, servAddr->ai_addrlen) < 0)
        DieWithSystemMessage("bind() failed");

    // Free address list allocated by getaddrinfo()
    freeaddrinfo(servAddr);
    for (;;) { // Run forever
        struct sockaddr_storage clntAddr; // Client address
        // Set Length of client address structure (in-out parameter)
        socklen_t clntAddrLen = sizeof(clntAddr);

        // Block until receive message from a client
        char buffer[10000]; // I/O buffer
        // Size of received message
        ssize_t numBytesRcvd = recvfrom(sock, buffer, 10000, 0,
            (struct sockaddr *) &clntAddr, &clntAddrLen);
        if (numBytesRcvd < 0)
            DieWithSystemMessage("recvfrom() failed!");

        // Send received datagram back to the client
        ssize_t numBytesSent = sendto(sock, buffer, numBytesRcvd, 0,
            (struct sockaddr *) &clntAddr, sizeof(clntAddr));
        if (numBytesSent < 0)
            DieWithSystemMessage("sendto() failed)");
        else if (numBytesSent != numBytesRcvd)
            DieWithUserMessage("sendto()", "sent unexpected number of bytes");
    }

}


int main(int argc, char *argv[]) {
    int errcheck;
    char* cmaxPackets = "0x7fffffff";
    char* ipingInterval = "1.0";
    char* ssize = "12";
    char* pportNumber = "33333";
    char* cip = "12";
    bool serverMode = false;
    bool noPrint = false;

    pthread_t tid1;
    pthread_t tid2;

    //initializes the signaling for when the user presses ctrl+c
    signal(SIGINT, printstats);

    //reads in the command line arguments for max packets, ping interval, size, port number, server mode, and no print
    for (int i = 1; i < argc; i++) {
            //printf("%s ", argv[i]);
            if (!strcmp(argv[i], "-c")) {
                cmaxPackets = argv[i+1];}

            if (!strcmp(argv[i], "-i")){
                ipingInterval = argv[i+1];}

            if (!strcmp(argv[i], "-p")){
                pportNumber = argv[i+1];
            }
            if (!strcmp(argv[i], "-s")) {
                ssize = argv[i+1];}

            if (!strcmp(argv[i], "-n")){
                noPrint = true;}

            if (!strcmp(argv[i], "-S")){
                serverMode = true;}
 
        
    } 
    //reads in the ip address
    cip = argv[argc-1];

    //sets the arguments to the global struct for all functions to be able to use
    settings.maxPackets = cmaxPackets;
    settings.pingInterval = ipingInterval;
    settings.size = ssize;
    settings.portNumber = pportNumber;
    settings.ip = cip;
    settings.serverMode = serverMode;
    settings.printMode = !noPrint;

    //printf("%s\n%s\n%s\n%s\n", cmaxPackets, ssize, pportNumber, cip);

    //sets the signal handler
    signal(SIGINT, printstats);
    
    if (!serverMode) {

        //prints the command line arguments to the standard error
        if (!noPrint) {
            fprintf(stderr, "%-15s %15s\n", "Count", cmaxPackets);
            fprintf(stderr, "%-15s %15s\n", "Size", ssize);
            fprintf(stderr, "%-15s %15s\n", "Interval", (ipingInterval));
            fprintf(stderr, "%-15s %15s\n", "Port", pportNumber);
            fprintf(stderr, "%-15s %15s\n\n", "Server_ip", cip);
        }
        else {
            fprintf(stderr, "%-15s %15s\n", "Count", cmaxPackets);
            fprintf(stderr, "%-15s %15s\n", "Size", ssize);
            fprintf(stderr, "%-15s %15s\n", "Interval", ipingInterval);
            fprintf(stderr,"**********\n\n");
        
        }
        
        //create the socket
        char *server = settings.ip;     
        char *servPort = settings.portNumber;

        // Construct the server address structure
        struct socket_args *arg = malloc(sizeof (struct socket_args)); 

        // Tell the system what kind(s) of address info we want
        struct addrinfo addrCriteria;
        memset(&addrCriteria, 0, sizeof(addrCriteria)); // Zero out structure
        addrCriteria.ai_family = AF_UNSPEC;             // Any address family
        // For the following fields, a zero value means "don't care"
        addrCriteria.ai_socktype = SOCK_DGRAM;          // Only datagram sockets
        addrCriteria.ai_protocol = IPPROTO_UDP;         // Only UDP protocol

        int rtnVal = getaddrinfo(server, servPort, &addrCriteria, &arg->servAddr);
        if (rtnVal != 0)
            DieWithUserMessage("getaddrinfo() failed", gai_strerror(rtnVal));

        // Create a datagram/UDP socket
        arg->sock = socket(arg->servAddr->ai_family, arg->servAddr->ai_socktype,
            arg->servAddr->ai_protocol); // Socket descriptor for client
        if (arg->sock < 0)
            DieWithSystemMessage("socket() failed");
        
        arg->fromAddrLen = sizeof(arg->fromAddr);

        void *actualargs = (void*)arg;

        //create threads for sending and receiving
        errcheck = pthread_create(&tid1, NULL, clientUDPSend, actualargs);
        if (errcheck != 0) {
            DieWithSystemMessage("pthread_create() failed");
        }

        errcheck = pthread_create(&tid2, NULL, clientUDPReceive, actualargs);
        if (errcheck != 0) {
            DieWithSystemMessage("pthread_create() failed");
        }
    }

    if (serverMode) {
        // Create the second thread
        serverUDP();
    }


    //check if threads are finished
    if (!serverMode) {
        if (pthread_join(tid1, NULL) != 0) {
            DieWithSystemMessage("pthread_join() failed");
        }

        if (pthread_join(tid2, NULL) != 0) {
            DieWithSystemMessage("pthread_join() failed");
        }
        else{
            printstats();
        }
    }
}


