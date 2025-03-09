/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

/* 
Please specify the group members here

# Student #1: John Courtney
# Student #2: Richard Zhang
# Student #3: 

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

/*
 * This structure is used to store per-thread data in the client
 */
typedef struct {
    int epoll_fd;        /* File descriptor for the epoll instance, used for monitoring events on the socket. */
    int socket_fd;       /* File descriptor for the client socket connected to the server. */
    long long total_rtt; /* Accumulated Round-Trip Time (RTT) for all messages sent and received (in microseconds). */
    long total_messages; /* Total number of messages sent and received. */
    float request_rate;  /* Computed request rate (requests per second) based on RTT and total messages. */
} client_thread_data_t;

/*
 * This function runs in a separate client thread to handle communication with the server
 */
void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP"; /* Send 16-Bytes message every time */
    char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;

    //Register connected client threads in epoll instance
    event.events = EPOLLOUT;
    event.data.fd = data->socket_fd;

    //Adds socket to epoll instance
    if (epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event) == -1) {
        perror("ERROR: epoll_ctl");
        pthread_exit(NULL);
    }

    //Set values to 0
	data->request_rate = 0.0;
    data->total_messages = 0;
	data->total_rtt = 0;

    //Sends the messege to the server and checks for errors
    for (int i = 0; i < num_requests; i++) {
        gettimeofday(&start, NULL);
		
        if (send(data->socket_fd, send_buf, MESSAGE_SIZE, 0) == -1) {
            perror("ERROR: send");
            pthread_exit(NULL);
        }

        int epoll_wait_result = epoll_wait(data->epoll_fd, events, MAX_EVENTS, -1);
		
        if (epoll_wait_result == -1) {
            perror("ERROR: epoll_wait");
            pthread_exit(NULL);
        }

        //Server recieves messege 
        if (recv(data->socket_fd, recv_buf, MESSAGE_SIZE, 0) == -1) {
            perror("ERROR: recv");
            pthread_exit(NULL);
        }

        //Gets final time and updates total messages and RTT
        gettimeofday(&end, NULL);
        data->total_messages++;
        data->total_rtt += (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
    }

    //Request rate calc
    data->request_rate = data->total_messages * (1000000.0f / data->total_rtt);

    //Closes socket/epoll instances
    close(data->socket_fd);
    close(data->epoll_fd);

    return NULL;
}

void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    struct sockaddr_in server_addr;

    for (int i = 0; i < num_client_threads; i++) {
        // creates socket using SOCK_STREAM and checks for errors in creation
        thread_data[i].socket_fd = socket(AF_INET, SOCK_STREAM, 0); 
		
        if (thread_data[i].socket_fd == -1) { 
            perror("ERROR: socket");
            exit(EXIT_FAILURE);
        }

        // creates epoll instance and checks for errors in creation
        thread_data[i].epoll_fd = epoll_create1(0); 
		
        if (thread_data[i].epoll_fd == -1) { 
            perror("ERROR: epoll_create1");
            exit(EXIT_FAILURE);
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
		
        //converts IPv4 address to binary
        if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
            perror("ERROR: inet_pton");
            exit(EXIT_FAILURE);
        }

        //connects to server
        if (connect(thread_data[i].socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
            perror("ERROR: connect");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < num_client_threads; i++) {
        // For each thread, launch a new one and pass the thread data
        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

	float total_request_rate = 0.0;
    long total_messages = 0;
    long long total_rtt = 0;
	
    for (int i = 0; i < num_client_threads; i++) {
        pthread_join(threads[i], NULL); 

		total_request_rate += thread_data[i].request_rate;
        total_messages += thread_data[i].total_messages;
        total_rtt += thread_data[i].total_rtt;

        close(thread_data[i].epoll_fd);
    }

    printf("Avg. RTT: %lld us\n", total_rtt / total_messages);
    printf("Tot. Request Rate: %f messages/s\n", total_request_rate);
}

void run_server() {
	int epoll_fd;
    int server_fd;
    struct epoll_event event;
    struct epoll_event events[MAX_EVENTS];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
	
    if (server_fd == -1) {
        perror("ERROR: socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
	
    // Bind ip and port to server
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("ERROR: bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Check for any incoming connections
    if (listen(server_fd, SOMAXCONN) == -1) {
        perror("ERROR: listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Create epoll 
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("ERROR: epoll_create1");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Adds server socket to epoll 
    event.events = EPOLLIN;
    event.data.fd = server_fd;
	
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("ERROR: epoll_ctl");
        close(server_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

	// Server's run to completion loop
    while (1) {
        // Wait for client connection
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
		
        if (num_events == -1) {
            perror("ERROR: epoll_wait");
            close(server_fd);
            close(epoll_fd);
            exit(EXIT_FAILURE);
        }

        // For each event received trys to accept it or go on to the next
        for (int i = 0; i < num_events; i++) {
			
            if (events[i].data.fd == server_fd) {
                int client_fd = accept(server_fd, NULL, NULL);
				
                if (client_fd == -1) {
                    perror("ERROR: accept");
                    continue; 
                }
                
                event.events = EPOLLIN;
                event.data.fd = client_fd;
				
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("ERROR: epoll_ctl");
                    close(client_fd);
                    continue; 
                }
            } else {
                // Previous connection receives the data
                char recv_buf[MESSAGE_SIZE];
				
                if (recv(events[i].data.fd, recv_buf, MESSAGE_SIZE, 0) <= 0) {
                    close(events[i].data.fd);
					
                } else {
					
                    if (send(events[i].data.fd, recv_buf, MESSAGE_SIZE, 0) == -1) {
                        perror("ERROR: send");
                        close(events[i].data.fd);
                    }
                }
            }
        }
    }

    // Close file descriptors
    close(server_fd);
    close(epoll_fd);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);

        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) num_client_threads = atoi(argv[4]);
        if (argc > 5) num_requests = atoi(argv[5]);

        run_client();
    } else {
        printf("Usage: %s <server|client> [server_ip] [server_port] [num_client_threads] [num_requests]\n", argv[0]);
        return EXIT_FAILURE;
    }

    return 0;
}
