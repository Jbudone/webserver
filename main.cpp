/* 
 * webserver
 *
 * TODO
 *   - comments
 *   - check HTTP/1.0\
 *   - newline from buffer being read into log
 *   - read port and directory from arguments (note: directory should NOT end with '/')
 *
 **/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

#define BACKLOG      1     // how many clients may be backlogged
#define BUF_LEN      1024  // size of buffer
#define FILE_BUF_LEN 512   // max buffer for file reads
#define WORD_LEN     128   // size of single word from buffer
#define TIME_BUF_LEN 128   // size of time buffer
#define STDIN        0     // STD Input Filedescriptor
#define LOGFILE_NAME "log" // name of logfile
#define PATH_BUF_LEN 512   // max size of path buffer

// response definitions
#define RESP_HTTPV "HTTP/1.0"
#define RESP_OK_CODE 200
#define RESP_OK_PHRASE "OK"
#define RESP_BADREQUEST_CODE 400
#define RESP_BADREQUEST_PHRASE "Bad Request"
#define RESP_NOTFOUND_CODE 404
#define RESP_NOTFOUND_PHRASE "Not Found"
#define RESP_CONTENTTYPE "text/html; charset=UTF-8"


#define GET_TIME_LOG time(&rawtime); timeinfo=localtime(&rawtime); strftime(buf_time, TIME_BUF_LEN, "%Y %b %d %H:%M:%S", timeinfo);
#define GET_TIME_RESPONSE time(&rawtime); timeinfo=localtime(&rawtime); strftime(buf_time, TIME_BUF_LEN, "%a, %d %b %Y %H:%M:%S %Z", timeinfo);

#define APPEND_LOG(REQUEST_LINE, RESPONSE_CODE, RESPONSE_PHRASE) GET_TIME_LOG fprintf(logfile, "%d %s %s:%d %s; HTTP/1.0 %d %s\n",(log_seq++), buf_time, buf_client_ip, buf_client_port, REQUEST_LINE, RESPONSE_CODE, RESPONSE_PHRASE);
#define APPEND_LOG_OK(REQUEST_LINE, RESPONSE_CODE, RESPONSE_PHRASE, RESPONSE_FILE) GET_TIME_LOG fprintf(logfile, "%d %s %s:%d %s; HTTP/1.0 %d %s; %s\n",(log_seq++), buf_time, buf_client_ip, buf_client_port, REQUEST_LINE, RESPONSE_CODE, RESPONSE_PHRASE, RESPONSE_FILE);

#define SEND_RESP_BADREQUEST(REQUEST_LINE) GET_TIME_RESPONSE resplen = sprintf(resp,"%s, %d, %s\nDate: %s\nContent-Type: %s\n\n",RESP_HTTPV,RESP_BADREQUEST_CODE,RESP_BADREQUEST_PHRASE,buf_time,RESP_CONTENTTYPE); send(clientfd, resp, resplen, 0); APPEND_LOG(REQUEST_LINE,RESP_BADREQUEST_CODE,RESP_BADREQUEST_PHRASE);

#define SEND_RESP_NOTFOUND(REQUEST_LINE) GET_TIME_RESPONSE resplen = sprintf(resp,"%s, %d, %s\nDate: %s\nContent-Type: %s\n\n",RESP_HTTPV,RESP_NOTFOUND_CODE,RESP_NOTFOUND_PHRASE,buf_time,RESP_CONTENTTYPE); send(clientfd, resp, resplen, 0); APPEND_LOG(REQUEST_LINE,RESP_NOTFOUND_CODE,RESP_NOTFOUND_PHRASE);

#define SEND_RESP_OK(REQUEST_LINE, RESPONSE_FILE, FILECONTENTS) GET_TIME_RESPONSE resplen = sprintf(resp,"%s, %d, %s\nDate: %s\nContent-Type: %s\n\n%s",RESP_HTTPV,RESP_OK_CODE,RESP_OK_PHRASE,buf_time,RESP_CONTENTTYPE,FILECONTENTS); send(clientfd, resp, resplen, 0); APPEND_LOG_OK(REQUEST_LINE,RESP_OK_CODE,RESP_OK_PHRASE,RESPONSE_FILE);

#define SEND_CHUNK(FILECONTENTS) resplen = sprintf(resp,"%s",FILECONTENTS); send(clientfd, resp, resplen, 0);

using namespace std;


int main(int argc, char **argv) {
    
    // 
    int sockfd, clientfd; // server/client socket file descriptor
    struct addrinfo hints, *servinfo, *res; // server address
    struct sockaddr_storage clientaddr; // client address
    socklen_t sin_size;
    int errno;
    int one = 1;
    char *port, *cwd;
    char buf[BUF_LEN], resp[BUF_LEN], filebuf[FILE_BUF_LEN], pathbuf[PATH_BUF_LEN];
    char method[WORD_LEN], requri[WORD_LEN], httpv[WORD_LEN], reqdir[WORD_LEN];
    int reqdirlen, resplen, filebuflen;
    struct stat st_buf;
    FILE *file = NULL, *logfile = NULL;
    int listener; // listening socket descriptor
    fd_set master; // master selector
    char buf_input[BUF_LEN]; // buffer for std input
    time_t rawtime;
    struct tm *timeinfo;
    char buf_time[TIME_BUF_LEN]; // time buffer
    int log_seq=1;
    char buf_client_ip[INET_ADDRSTRLEN];
    unsigned int buf_client_port;

    // setup our address details
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    // get our port and directory
    port = "3490";
    cwd = "www"; // NOTE: cwd must NOT end with '/' !!!

    // open logfile for logging purposes
    logfile = fopen(LOGFILE_NAME,"a+");

    if ((errno = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
	printf("Error in getaddrinfo: %s\n", gai_strerror(errno));
	return -1;
    }


    // try to bind to an available address
    for (res = servinfo; res != NULL; res = res->ai_next) {
	if ((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1) {
	    continue;
	}

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int)) == -1) {
	    printf("Error reusing address\n");
	    return -1;
	}

	if (bind(sockfd, res->ai_addr, res->ai_addrlen) == 0) {
	    break; // successfully binded
	}

	close(sockfd); // couldn't bind, close and try the next one
    }

    if (res == NULL) {
	printf("Error could not bind socket\n");
	return -1;
    }

    freeaddrinfo(servinfo); 

    
    // listen for client
    listener=listen(sockfd, BACKLOG);
    if (listener == -1) {
	printf("Error in listening for client\n");
	return -1;
    }

    printf("server: waiting for connections....\n");

    while (true) {

	sin_size = sizeof clientaddr;
	memset(buf, 0, BUF_LEN);
	memset(resp, 0, BUF_LEN);
	memset(method, 0, WORD_LEN);
	memset(requri, 0, WORD_LEN);
	memset(httpv, 0, WORD_LEN);
	memset(reqdir, 0, WORD_LEN);
	memset(buf_input, 0, BUF_LEN);
	memset(buf_client_ip, 0, INET_ADDRSTRLEN);
	memset(pathbuf, 0, PATH_BUF_LEN);

	// selecting stuff
	FD_ZERO(&master);
	FD_SET(sockfd, &master);
	FD_SET(STDIN, &master);

	if (select(sockfd+1, &master, NULL, NULL, NULL) == -1) {
		printf("Error selecting stuff\n");
		return -1;
	}

	if (FD_ISSET(STDIN, &master)) {
		// input was read
		printf("INPUT WAS READ!!\n");
		scanf("%s",buf_input);
		if (strcmp(buf_input, "q")==0) {
			printf("quit server..\n");
			break;
		}
		continue;
	} else if (FD_ISSET(sockfd, &master)) {
		// found client
		printf("Found client ??\n");
		clientfd = accept(sockfd, (struct sockaddr *)&clientaddr, &sin_size);
		inet_ntop(clientaddr.ss_family, &(((struct sockaddr_in*)&clientaddr)->sin_addr), buf_client_ip, sizeof buf_client_ip);
		buf_client_port = ((struct sockaddr_in*)&clientaddr)->sin_port;
	} else {
		printf("something weird just happened..\n");
	}
	
	if (clientfd == -1) {
	    printf("Error accepting client\n");
	    continue;
	}

	if (recv(clientfd, buf, BUF_LEN, 0)) {
	    printf("Received: %s", buf);

	    // read request from user
	    sscanf(buf,"%s %s %s",method,requri,httpv);

	    // set method and httpv to uppercase
	    int i=0;
	    char c;
	    while (method[i]) {
		c=method[i];
		if (islower(c)) method[i]=toupper(c);
		i++;
	    }
	    i=0;
	    while (httpv[i]) {
		c=httpv[i];
		if (islower(c)) httpv[i]=toupper(c);
		i++;
	    }
	    i=0;


	    // parse input
	    if (strcmp(method,"GET")==0) {

		// does the uri attempt to go out of directory (../)
		if (strstr(requri,"..")) {
		    SEND_RESP_BADREQUEST(buf)
		    close(clientfd);
		    printf("USER attempting to locate outside of cwd\n");
		    continue;
		}

		// prepend server directory to uri (make sure req begins with '/')
		if (requri[0]=='/') {
		    reqdirlen = sprintf(reqdir,"%s%s",cwd,requri);
		} else {
		    reqdirlen = sprintf(reqdir,"%s/%s",cwd,requri);
		}
		printf("REQUEST DIR: %s  [%s][%s]\n",reqdir,requri,cwd);

		if ((errno = stat(reqdir, &st_buf)) != 0) {
		    // ERROR: could not open file/directory
		    SEND_RESP_NOTFOUND(buf)
		    close(clientfd);
		    printf("COULD NOT find file\n");
		    continue;
		}

		// is the request a directory? if so default to dir.index.html
		if (S_ISDIR(st_buf.st_mode)) {
		    // append index.html (since this is a directory)

		    // is the last element a '/' ? if not, append it
		    if (reqdir[reqdirlen-1] != '/') {
			strcat(reqdir,"/");
		    }
		    strcat(reqdir,"index.html");

		    if ((errno = stat(reqdir, &st_buf)) != 0) {
			// ERROR: could not open file/directory
			SEND_RESP_NOTFOUND(buf)
			close(clientfd);
			printf("COULD NOT find file\n");
			continue;
		    }
		}

		// does file exist
		if (S_ISREG(st_buf.st_mode)) {
		    file = fopen(reqdir,"r");
		    if (file == NULL) {
			printf("Error reading file: %s\n",reqdir);
			close(clientfd);
			continue;
		    }

		    // get absolute path of file
		    realpath(reqdir, pathbuf);

		    bool hasSent = false;
		    while (true) {
			memset(resp,0,sizeof(resp));
			memset(filebuf,0,sizeof(filebuf));
			if (fread((void*)filebuf, 1, FILE_BUF_LEN-1, file) == 0) {
			    break;
			}

			// send current file buffer contents
			if (hasSent) {
				SEND_CHUNK(filebuf)
			} else {
				SEND_RESP_OK(buf,pathbuf,filebuf)
			}
			hasSent = true;
			printf("READING FILE\n");
		    }
		    if (!hasSent) {
			// file is empty, nothing was sent, send a blank response
			SEND_RESP_OK(buf,pathbuf,"")
		    }
		    fclose(file);
		    printf("READ FILE\n");
		} else {
		    SEND_RESP_NOTFOUND(buf)
		    close(clientfd);
		    printf("COULD NOT FIND FILE: %s\n",reqdir);
		    continue;
		}

		
		printf("there ya go, user!\n");
	    } else {
		// bad request from user
		SEND_RESP_BADREQUEST(buf)
	    }
	}

	close(clientfd);
    }

    fclose(logfile);
    return 0;
}

