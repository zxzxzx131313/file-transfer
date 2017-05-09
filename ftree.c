#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "ftree.h"
#include "hash.h"
#include <libgen.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define MAX_BACKLOG 5
#define MAX_CONNECTIONS 12
#define BUF_SIZE 128

int check_hash(char *hash1, char *hash2);

/*construct a path in dest with the basename of src */
void construct_path(const char *dest, char *src, char *path){
        strcpy(path, dest);
        strcat(path, "/");
        strcat(path, basename(src));
}

/*getting the stat information from path */
int construct_stat(const char *path, struct stat *s){

        int err = lstat(path, s);
 	return err;
}

/*determine whether a path is referring to a file or a directory*/
int stat_info(const char *command, const char *path){

        struct stat s;
        construct_stat(path, &s);

        if(strcmp(command, "is_dir?") == 0){
                return S_ISDIR(s.st_mode);
        } else if (strcmp(command, "is_file?") == 0) {
                return S_ISREG(s.st_mode);
        } else if (strcmp(command, "is_link?") == 0) {
		return S_ISLNK(s.st_mode);
	}
        fprintf(stderr, "command not valid\n");
        return 1;
}

/*The user struct indicates the user's property and current state*/
struct user {
	int type;
	int sock_fd;
	//The user's current request
	struct request cur_req;
	int state;
};

/** create a socket and connect to the server **/
int socknconnect(unsigned short port){
	// Create the socket FD.
    	int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    	if (sock_fd < 0) {
        	perror("client: socket");
        	exit(1);
    	}

    	// Set the IP and port of the server to connect to.
    	struct sockaddr_in server;
    	server.sin_family = AF_INET;
    	server.sin_port = htons(port);
    	if (inet_pton(AF_INET, "127.0.0.1", &server.sin_addr) < 1) {
		perror("client: inet_pton");
	        close(sock_fd);
	        exit(1);
    	}

    	// Connect to the server.
	if (connect(sock_fd, (struct sockaddr *)&server, sizeof(server)) == -1) {
		perror("client: connect");
	        close(sock_fd);
	        exit(1);
    	}
	
	return sock_fd;

}

//send the request struct from client to server
void send_struct(int sock_fd, const char *source, const char *req_path, int is_filesender){

	//populate request using information from lstat(source)
	struct stat src;
	construct_stat(source, &src);

	//getting type
	unsigned int type; 
	//if the client is a file sender then its type is differnent from the file type
	if(is_filesender){
		type = htonl(TRANSFILE);
	} else {	
		int isdir = stat_info("is_dir?", source); 

		type = htonl(isdir ? REGDIR : REGFILE);
	}


	//getting path
	char path[MAXPATH];

	strncpy(path, (char *)req_path, MAXPATH-1);

	path[MAXPATH-1] = '\0';

	//getting mode
	unsigned short mode = htons(src.st_mode);


	//gettin hash
	char hash_val[BLOCKSIZE] = "\0";

	//directories do not have hash values
	if(stat_info("is_file?", source)) {
		FILE *f = fopen(source, "rb");
		if(f == NULL){
			perror("client fopen :");
			exit(1);
		}
		hash(hash_val, f);

		if(fclose(f) == EOF){
			perror("Client close file:" );
			exit(1);
		}
	}


	//getting size
	unsigned int size = htonl(src.st_size);


	//writing struct
	if(write(sock_fd, &type, sizeof(unsigned int)) != sizeof(unsigned int)){
			perror("client write type:");
			exit(1);
	}
	if(write(sock_fd, path, MAXPATH) != MAXPATH){
			perror("client write path:");
			exit(1);
	}
	if(write(sock_fd, &size, sizeof(unsigned int)) != sizeof(unsigned int)){
			perror("client write size:");
			exit(1);
	}
	if(write(sock_fd, &mode, sizeof(unsigned short)) != sizeof(unsigned short)){
			perror("client write mode:");
			exit(1);
	}
	if((stat_info("is_dir?", source) ? REGDIR : REGFILE) == REGFILE){
		if(write(sock_fd, hash_val, BLOCKSIZE) != BLOCKSIZE){
				perror("client write hash value:");
				exit(1);
		}
	}
}

/*retrive server response*/
int get_server_response(int sock_fd){
	
	//getting response from server
	unsigned int buf;
	int num_read = read(sock_fd, &buf, sizeof(unsigned int));
	int response = (int)ntohl(buf);
	
	if(num_read == 0 || num_read != sizeof(unsigned int)){
		fprintf(stderr, "Client Error: server reponse %d not vaild\n", response);
		exit(1);
	}
	return response;
}

/*Client: copy the content of file by reading from src and writing to file
in dest in MAXDATA bytes chunk at a time */
int send_content(const char *src, int sock_fd)  {

        FILE *src_file = fopen(src, "rb");

        if(src_file == NULL) {
                perror("Client fopen:");
		exit(1);
        }

        char file_byte[MAXDATA] = "\0";
        while(fread(&file_byte, sizeof(char), MAXDATA, src_file) != 0){
                int error = write(sock_fd, &file_byte, sizeof(char)*MAXDATA);


		int response = get_server_response(sock_fd);


		//server will ask for more data if data client sent is not 0
		if(response != MORE_DATA){
			fprintf(stderr, "Client: error when sending data to server\n");
			return 1;
		}
                if(error != MAXDATA){
                        fprintf(stderr, "Client: error when sending data to server\n");
			return 1;
                }
		
        }
	if(fclose(src_file) == EOF){
		perror("Client close file:");
		exit(1);
	}

	return 0;
}

/*create another connection to server and then start sending files */
int file_sender(const char *src, const char *req_path, int port){

	int sender_fd = socknconnect(port);

	//file sender will send the same request again
	//identify itself as file sender
	int filesender = 1;


	send_struct(sender_fd, src, req_path, filesender);

	//send file content to server
	int error = send_content(src, sender_fd);


	if(error){
		fprintf(stderr, "Client send file %s failed\n", src);
		exit(1);
	}


	//file sender will exit after data transfer is complete.
	if(close(sender_fd) == -1){
		perror("Client close file sender socket");
		exit(1);
	}
	exit(0);
}

/* send file to server if received SENDFILE response */
int try_send_file(const char *src, const char *req_path, int sock_fd, int port) {

	//send to server information about this file
	

        send_struct(sock_fd, src, req_path, 0);

        int response = get_server_response(sock_fd);

	int error = 0;
	if(response == SENDFILE){
		//create file sender to send file in child process
		
		pid_t result = fork();
		
		if(result == 0){
			
			file_sender(src, req_path, port);
		} else if(result < 0){
			perror("Client: fork");
			exit(1);
		} else {
			//parent wait for child
			int status = -1;
		
			if(waitpid(result, &status, 0) == -1){
				perror("Client: waitpid");
				exit(1);
			}
			if(WIFEXITED(status)) {
				if(WEXITSTATUS(status)){
					error = 1;
				}
			}
		}
	 } else if(response == ERROR){
		 error = 1;
	 } else if(response != OK){
		 error = 1;
		 fprintf(stderr, "Client: server response %d not valid\n", response);
	 }

        return error;
		
}

/*send a directory tree to server recursively, fork a child for each sub-directories*/
int send_dir(const char *src, const char *req_path, int sock_fd, int port){
	
	//create a process for child directories
        pid_t result = fork();

        //child process
        if(result == 0){
		int error = 0;


                struct stat src_stat;
                construct_stat(src, &src_stat);

                //check if there is somthing in src directory
                DIR *dir_src = opendir(src);
                if(dir_src == NULL) {
                        perror("Client: opendir");
                        exit(1);
                }


                struct dirent *dp_src = readdir(dir_src);

                while(dp_src != NULL){

			char src_dir_path[strlen(src) + strlen(dp_src->d_name) + 2];
			construct_path(src, dp_src->d_name, src_dir_path);

			char sub_req_path[strlen(req_path) + strlen(dp_src->d_name) + 2];
			construct_path(req_path, dp_src->d_name, sub_req_path);

                        //skip file starts with . and symbolic links
                        if(dp_src->d_name[0] != '.' && stat_info("is_link?", src_dir_path) != 1){
                                if(stat_info("is_file?", src_dir_path)){


                                        error = try_send_file(src_dir_path, sub_req_path, sock_fd, port);


					if(error){
						exit(1);
					}
				

                                } else if(stat_info("is_dir?", src_dir_path)) {


                                        //if there is directory inside, fork another
                                        //process to handle it
					
					send_struct(sock_fd, src_dir_path, sub_req_path, 0);
					int response = get_server_response(sock_fd);

					if(response == ERROR){
						error += 1;
					}

                                        error += send_dir(src_dir_path, sub_req_path, sock_fd, port);
                                	if(error){
						exit(1);
					}

				}
                        }
                        dp_src = readdir(dir_src);
                }
		if(closedir(dir_src) == -1){
			perror("Client close dir:");
			exit(1);
		}
		exit(0);

	} else if(result > 0){


		//parent will wait for its child
		int status = -1;
		int error = 0;
		if(wait(&status) == -1){
			perror("Client: waitpid");
			error = 1;
		}
		if(WIFEXITED(status)) {
			// if child exited abnormally, error set to 1
			if(WEXITSTATUS(status)){
				error = 1;
			}
		}
		return error;
	} else {
		perror("Client: fork");
		exit(1);
	}

}

/*rcopy_client connect to server and walks through the directory tree with the name passed in
 * send each file/directory to server*/
int rcopy_client(char *source, char *host, unsigned short port){
	
	//connect to the server
	int sock_fd = socknconnect(port);
	
	//we need a relative path as the root directory
	char root[MAXPATH] = "\0";
      	strncpy(root, basename(source), MAXPATH-1);


	//sending info about the root directory, 
	//which will be in sandbox/dest
	send_struct(sock_fd, source, root, 0);

	//error will be set to 1 if client encountered any error
	int error = 0;

	if(stat_info("is_file?", source)) {


		error += try_send_file(source, root, sock_fd, port);


	} else if (stat_info("is_dir?", source)){
		

		//getting response from server
		int resp = get_server_response(sock_fd);

		if(resp != ERROR){
			//open dir and check files
			DIR *dir_src = opendir(source);
			if(dir_src == NULL) {
				perror("Client: opendir");
				exit(-1);
			}

			struct dirent *dp_src = readdir(dir_src);
			while(dp_src != NULL){
				char src_dir_path[strlen(source) + strlen(dp_src->d_name) + 2];
				construct_path(source, dp_src->d_name, src_dir_path);

				char req_path[strlen(root) + strlen(dp_src->d_name) + 2];
				construct_path(root, dp_src->d_name, req_path);

				//skip file starts with . and sybolic links
				if(dp_src->d_name[0] != '.' && stat_info("is_link?", src_dir_path) != 1){
					
					if(stat_info("is_file?", src_dir_path)){
						

						error += try_send_file(src_dir_path, req_path, sock_fd, port);

						
					} else if(stat_info("is_dir?", src_dir_path)) {

						//there is directory inside source						
						
						send_struct(sock_fd, src_dir_path, req_path, 0);
						int response = get_server_response(sock_fd);
	
						if(response == ERROR){
							error += 1;
						}

						error += send_dir(src_dir_path, req_path, sock_fd, port);
					}
				}
				dp_src = readdir(dir_src);
			}
			if(closedir(dir_src) == -1){
				perror("Client close dir:");
				error = 1;
			}
		} else {
			error = 1;
			fprintf(stderr, "Client: %s file /directory type imcompatiable\n", source);
		}
	}
	if(close(sock_fd) == -1){
		perror("Client close sock_fd");
		error = 1;
	}
	if(error){
		return 1;
	} else {
		return 0;
	}
}

/* Accept a connection. 
 * Return the new client's file descriptor or -1 on error.
 */
int accept_connection(int fd, struct user *users) {
  
	int user_index = 0;
	while (user_index < MAX_CONNECTIONS && users[user_index].sock_fd != -1){
		user_index++;
	}

    	int client_fd = accept(fd, NULL, NULL);
    	if (client_fd < 0) {
        	close(fd);
        	exit(1);
    	}

	if(user_index == MAX_CONNECTIONS) {
		fprintf(stderr, "Server: max concureent connections\n");
		close(client_fd);
		return -1;
	}

	//intialize users[index].cur_req
	users[user_index].sock_fd = client_fd;
	users[user_index].cur_req.type = -1;
	strcpy(users[user_index].cur_req.path, "\0");
	users[user_index].cur_req.mode = -1;
	strcpy(users[user_index].cur_req.hash, "\0");
	users[user_index].cur_req.size = -1;
	users[user_index].state = AWAITING_TYPE;
	//initialize end_of_file;
	users[user_index].cur_req.end_of_file = 0;


    	return client_fd;
}

/*read a integer from client*/
int read_int(int index, struct user *users){
	
	int fd = users[index].sock_fd;

	unsigned int buf;

	int num_read = read(fd, &buf, sizeof(unsigned int));

	int integer = ntohl(buf);


	if(num_read == 0)
	{
		fprintf(stderr, "Server: read_int reads 0 byte\n");
		return -1;
	}

	return integer;
}

/*read a path from client with maximum MAXPATH-1 bytes*/
int read_path(int index, char *buf, struct user *users){
	
	int fd = users[index].sock_fd;

   	int num_read = read(fd, buf, MAXPATH);

    	buf[MAXPATH-1] = '\0';


	if(num_read == 0){
		fprintf(stderr, "Server: read_path reads 0 bytes\n");
		return fd;
	}

    	return 0;
}

/*read a hash value with size BLOCKSIZE from client*/
int read_hash(int index, char *buf, struct user *users){

	int fd = users[index].sock_fd;

	int num_read = read(fd, buf, BLOCKSIZE);


	if(num_read == 0){
		return fd;
	}

    	return 0;
}

/*read a mode from client */
mode_t read_mode(int index, struct user *users){

	int fd = users[index].sock_fd;

	unsigned short buf;

   	int num_read = read(fd, &buf, sizeof(unsigned short));

	mode_t mode = (mode_t)ntohs(buf);


	if(num_read == 0){
		fprintf(stderr, "Server: read_mode reads 0 bytes\n");
		return -1;
	}

    	return mode;
}

/*decide the response to client based on local directory content*/
int reply_to_client(struct request req){

	struct stat server_stat;
       	errno = 0;	
	int error = construct_stat(req.path, &server_stat);
	
	if(req.type == REGFILE){


		if(error == -1){
			//check if such file exists
			if(errno == ENOENT){


				//reply SENDFILE if file with such path does not exist
				return SENDFILE;
			} else {
				perror("Server stat:");
				return ERROR;
			}

		} else if(error == 0){

			//return ERROR is the exsiting file with the same name is not a regular file
			if(S_ISREG(server_stat.st_mode) != 1){

				fprintf(stderr, "imcompatiable type\n");
				return ERROR;
			}



			//if file with such path exists, comparing size and hash
			if(req.size == server_stat.st_size){

				char server_hash[BLOCKSIZE];
				FILE *f = fopen(req.path, "rb");
				if(f == NULL){
					perror("client fopen:");
					return ERROR;
				}

				int hash_err = hash(server_hash, f);
				if(hash_err){
					return ERROR;
				}

				int diff_hash = check_hash(req.hash, server_hash);


				if(fclose(f) == EOF ){
                        		perror("fclose");
					return ERROR;
                		}

				if(diff_hash){
					return SENDFILE;
				} 
				
				return OK;
				
			} else {
				return SENDFILE;
			}
		}

	} else if(req.type == REGDIR) {

		if(error == -1){

			//check if such directory exists
			if(errno == ENOENT){

				//if such directory does not exists, create it
				int mkdir_err = mkdir(req.path, req.mode);
                		if(mkdir_err == -1){
					perror("mkdir");
					return ERROR;
				}
				
			} else {
				perror("Server stat:");
				return ERROR;
			}

		} else if(error == 0){

			//return ERROR is the exsiting file with the same name is not a directory
			if(S_ISDIR(server_stat.st_mode) != 1){
				fprintf(stderr, "Server: %s imcompatiable type\n",req.path);
				return ERROR;
			}

			//if such directory exists, change its mode
			if(chmod(req.path, req.mode) == -1){
                        	perror("chmod");
				return ERROR;
			}
		}
		return OK;
	} 
	return ERROR;
	
}

/*read data from client, read maximum MAXDATA bytes at a time*/
int read_data(int index, struct user *users){

	int fd = users[index].sock_fd;

	struct request *req = &(users[index].cur_req);

	char data_byte[MAXDATA] = "\0";

	int num_read = read(fd, &data_byte, sizeof(char)*MAXDATA);


	//if num_read equals to 0 then it is the end of file.
	if(num_read != MAXDATA && num_read != 0){
		perror("Server read");
		return ERROR;
	}

	FILE *file = NULL;
	
	if(req->end_of_file != 1){
		file = fopen(req->path, "wb");
		req->end_of_file = 1;
	} else {
		//if this is not the first handling this file transfer, append to it
		file = fopen(req->path, "ab");
	}
		

	

	int max = (req->size < MAXDATA) ? req->size: MAXDATA;

	int num_write = fwrite(&data_byte, sizeof(char), max, file);

	req->size = req->size - num_write;


	//to the string length of the data read
	if(num_write != max ){
		fprintf(stderr, "Server fwrite %s: failed\n", req->path);
		return ERROR;
	}

	if(fclose(file) == EOF){
		perror("Server close file ");
		return ERROR;
	}

	if(num_read == 0){
		
		if(chmod(req->path, req->mode) == -1){
                    	perror("chmod");
			return ERROR;
		}
		return OK;
	} else {
		
		//if num_read is not 0 the there could be more data in file
		return MORE_DATA;
	}
}

/*server keeps receive connection from clients and read from each connect clients in a loop*/
void rcopy_server(unsigned short port) {

	struct user users[MAX_CONNECTIONS];
	
	for (int index = 0; index < MAX_CONNECTIONS; index++) {
        	users[index].sock_fd = -1;
        	users[index].type = -1;
    	}
	

	// Create the socket FD.
    	int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    	if (sock_fd < 0) {
        	perror("server: socket");
        	exit(1);
    	}

	// make sure this port can be reused after being released.
        int reuse = 1;
        int err = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (-1 == err){
                perror("setsockopt");
                exit(1);
        }


    	// Set information about the port (and IP) we want to be connected to.
    	struct sockaddr_in server;
    	server.sin_family = AF_INET;
    	server.sin_port = htons(port);
    	server.sin_addr.s_addr = INADDR_ANY;

    	memset(&server.sin_zero, 0, 8);

    	// Bind the selected port to the socket.
    	if (bind(sock_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
    		perror("server: bind");
        	close(sock_fd);
        	exit(1);
    	}

    	// Announce willingness to accept connections on this socket.
    	if (listen(sock_fd, MAX_BACKLOG) < 0) {
        	perror("server: listen");
        	close(sock_fd);
        	exit(1);
    	}

    	// The client accept - message accept loop.
    	int max_fd = sock_fd;
    	fd_set all_fds, listen_fds;
    	FD_ZERO(&all_fds);
    	FD_SET(sock_fd, &all_fds);
	
    	while (1) {
        // select updates the fd_set it receives
	listen_fds = all_fds;
        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            	perror("server: select");
            	exit(1);
        }
        
	// Is it the original socket? Create a new connection ...
        if (FD_ISSET(sock_fd, &listen_fds)) {
            	int client_fd = accept_connection(sock_fd, users);
            	if (client_fd > max_fd) {
               		max_fd = client_fd;
            	}
            	FD_SET(client_fd, &all_fds);
        } 

        // check the clients.
	for(int index = 0; index < MAX_CONNECTIONS; index ++){
		if(users[index].sock_fd > -1 && FD_ISSET(users[index].sock_fd, &listen_fds)){
			
			struct request *req = &(users[index].cur_req);
			int *status = &(users[index].state);

			//check the state of server and populate its request struct
			if(*status == AWAITING_TYPE){
				req->type = read_int(index, users);
				//if client exited, then read_int will read nothing
				//remove it from the list of listened file descriptors
				//and move on to the next client.
				if(req->type == -1){
					FD_CLR(users[index].sock_fd, &all_fds);
					continue;
				}
				*status = AWAITING_PATH;

			} else if(*status == AWAITING_PATH) {
				read_path(index, req->path, users);
				*status = AWAITING_SIZE;

			} else if(*status == AWAITING_SIZE) {
				req->size = read_int(index, users);
				*status = AWAITING_PERM;

			} else if(*status == AWAITING_PERM) {
				req->mode = read_mode(index, users);
				
				//only regular file has hash values.
				if(req->type != REGDIR){
					*status = AWAITING_HASH;
				} else {
					*status = AWAITING_TYPE;
					
					//if user is sending a directory's information
					//just reply it with OK then move on
					int reply = reply_to_client(*req);
					
					unsigned int buf = htonl(reply);
					int num_write = write(users[index].sock_fd, &buf, sizeof(unsigned int));
					if(num_write == 0){
						perror("Server: write");
					}

				}
			} else if(*status == AWAITING_HASH) {
				
				read_hash(index, req->hash, users);
			
				//information on this path is complete,if user is file sender then 
				//proceed to data transfer
				//else check existing files then reply to client whether to send file or not

				if(req->type == TRANSFILE){
					*status = AWAITING_DATA;
				} else if(req->type == REGFILE){
					
					int reply = reply_to_client(*req);

					unsigned int buf = htonl(reply);
					int num_write = write(users[index].sock_fd, &buf, sizeof(unsigned int));
					if(num_write == 0){
						perror("Server: write");
					}

					*status = AWAITING_TYPE;					
				} else {
					fprintf(stderr, "Server: type %d invalid\n", *status);
				}				
			} else if(*status == AWAITING_DATA) {

				//only file sender can get here, read data from file sender
				int reply = read_data(index, users);


				unsigned int buf = htonl(reply);
				int num_write = write(users[index].sock_fd, &buf, sizeof(unsigned int));
				if(num_write == 0){
					perror("Server: write");
				}

				if(reply != MORE_DATA){
					//after sending file file sender will close
					FD_CLR(users[index].sock_fd, &all_fds);	
				}
			} else {
				fprintf(stderr, "Server: status code %d invalid\n", *status);
			}
        	}
	
	}		
	
    }		

   

}
