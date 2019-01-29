#include <sys/types.h> 
#include <sys/socket.h> 
#include <netinet/in.h>                      // ORI TOR 3057066999
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "threadpool.h"

#define RFC1123FMT "%a,%d %b %Y %H:%M:%S GMT"
#define END_OF_LINE "\r\n"
#define USAGE_MSG "Usage: server <port> <pool-size> <max-number-of-request>\n"
#define BUF_LEN 128

													// FUNCTION DECLERATION //
void constractResponse(int,char*,char*);
bool checkRequest(char*,int);
void* connectionHandler(void*);
void errorHandler(int,char*,char*,char*);
char* get_mime_type(char*);
int work(void*);
int is_regular_file(const char *);
int isDirectory(const char *); 
bool pathExists(char*);
bool notEndsWithSlash(char *);
void handleDirectory(int, char*);
bool isFile(char*);
void sendFile(char*,int);
int getHtmlLength(char *,char *,char *);
int getHeaderLength(char* ,char* ,char*);
void getDate(char*);
void sendFile(char* ,int);
void sendDir(int,char*);
bool hasPremissions(char *,int);
int getContentLengthForError(char*);
bool is_folder_writable(char*);
bool isNumber(const char *);

int main(int argc, char const *argv[])
{
	if(argc != 4) 
	{
		printf(USAGE_MSG); 
		return EXIT_SUCCESS;
	}
	else
	{
		int port,poolSize,maxNumOfReq;
		if(!isNumber(argv[1]) || !isNumber(argv[2]) || !isNumber(argv[3])) 
		{
			printf("%s\n", USAGE_MSG);
			exit(EXIT_SUCCESS);
		}
		port = atoi(argv[1]);
		poolSize = atoi(argv[2]);
		maxNumOfReq = atoi(argv[3]);
		if(port < 0 || poolSize < 0 || maxNumOfReq < 0)
		{
			printf("%s\n", USAGE_MSG);
			exit(EXIT_SUCCESS);
		}

		threadpool* pool = create_threadpool(poolSize); // creating the thread pool. if failed exit.
		if(!pool)
		{
			printf("%s\n", USAGE_MSG);
			free(pool);
			exit(EXIT_SUCCESS);
		}
		int sd;    
		struct sockaddr_in serv; 

		sd = socket(PF_INET,SOCK_STREAM,0);            // socet initialize.
		if (sd < 0) 
		{
			perror("ERROR opening socket"); 
			exit(EXIT_SUCCESS);
		} 

		serv.sin_family = PF_INET;  
		serv.sin_port = htons(port);   
		serv.sin_addr.s_addr = htonl(INADDR_ANY); 


		if (bind(sd, (struct sockaddr *)&serv, sizeof(serv)) < 0)   
		{
			perror("ERROR on binding");    
			exit(EXIT_SUCCESS);
		}              

		listen(sd,10); 
		int *sock_fd = (int*)malloc(sizeof(int)*maxNumOfReq); // arrays to keep the new sock fd .
		if(!sock_fd)
		{
			errorHandler(sd,"500 Internal server error","Some server side error",NULL);
			return EXIT_SUCCESS;
		}
		memset(sock_fd,'\0',sizeof(int)*maxNumOfReq);
		for(int i=0;i<maxNumOfReq;i++)
		{
			sock_fd[i] = accept(sd, NULL, NULL);
			if(sock_fd[i]<0)
			{
				perror("error on accept\n");
				free(sock_fd);
				exit(1);
			}
			else
			{
				dispatch(pool,work,&sock_fd[i]); // for each client we send a thread to handle his request.
			}

		}
		destroy_threadpool(pool);
		free(sock_fd);
		close(sd);

	}
	return EXIT_SUCCESS;
}
int work(void *fd)
{
	char r_buffer[2]; 
	char buffer[4000];
	memset(r_buffer,'\0',2);   
	memset(buffer,'\0',4000);
	int sock_fd = *(int*)fd;
	int rc =0;
	while( (rc = read(sock_fd,r_buffer,1)) > 0 ) // reading the request first line
	{
		strcat(buffer,r_buffer);
		if(strcmp(r_buffer,"\n") == 0)
			break;

	}
	if(!checkRequest(buffer,sock_fd)) // if first line is not as requested error 400
	{
		errorHandler(sock_fd,"400 bad request","Bad Request",NULL);
		return EXIT_SUCCESS;
	}
	char *method = strtok(buffer," ");
	if(strcmp(method,"GET") !=0) 
	{
		errorHandler(sock_fd,"501 Not supported","Method is not supported.",NULL);
		return EXIT_SUCCESS;
	}
	char *path = strtok(NULL," ");
	if(strcmp(path,"/") == 0) //
	{
		handleDirectory(sock_fd,"./");
		return EXIT_SUCCESS;
	}
	if(*path != '/') // if its not starts with / than path dont exists.
	{
		errorHandler(sock_fd,"404 Not Found","Not Found",path);
		return EXIT_SUCCESS;
	}
	path++;
	if(*path == '/') // if theres another / than were trying to access a folder with no premission for this exercise
	{
		errorHandler(sock_fd,"403 Forbiden","Access denied",path);
		return EXIT_SUCCESS;		
	}
	if(!pathExists(path)) 
	{
		errorHandler(sock_fd,"404 Not Found","File Not Found",path);
		return EXIT_SUCCESS;
	}
	if(isDirectory(path) && notEndsWithSlash(path)) 
	{
		errorHandler(sock_fd,"302 Found","Directory must end with slash",path);
		return EXIT_SUCCESS;
	}
	if(isDirectory(path))
	{
		if(hasPremissions(path,sock_fd))
			handleDirectory(sock_fd,path);
		else
			errorHandler(sock_fd,"403 Forbiden","Access denied",path);
			
		return EXIT_SUCCESS;
	}
	if(!is_regular_file(path) || !hasPremissions(path,sock_fd))
	{
		errorHandler(sock_fd,"403 Forbiden","Access denied",path);
		return EXIT_SUCCESS;
	}
	else
	{
		sendFile(path,sock_fd);
		return EXIT_SUCCESS;
	}

	close(sock_fd);
	return EXIT_SUCCESS;
}
bool hasPremissions(char *path,int fd)
{
	char * tempPath = (char*)malloc(strlen(path)+1);
	if(!tempPath)
	{
		errorHandler(fd,"500 Internal server error","Some server side error",path);
		return EXIT_SUCCESS;
	}
	memset(tempPath,'\0',strlen(path)+1);
	strcpy(tempPath,path);
	char * subPath = (char*)malloc(strlen(path)+1);
	if(!subPath)
	{
		errorHandler(fd,"500 Internal server error","Some server side error",path);
		return EXIT_SUCCESS;
	}
	memset(subPath,'\0',strlen(path)+1);
	char *temp = strtok(tempPath,"/");
	int i=0;
	while(temp) // adding one sub folder at a time to check wheter the new subPath has premission.
	{
		if(i != 0)
			strcat(subPath,"/");
		else
			i++;
		strcat(subPath,temp);
		if( !is_folder_writable(subPath))
		{
			free(tempPath);
			free(subPath);
			return false;
		}
		temp = strtok(NULL,"/");
	}
	free(tempPath);
	free(subPath);
	return true;
}
bool is_folder_writable(char* path) 
{
	struct stat statbuf;
	stat(path, &statbuf);
	if(S_ISDIR(statbuf.st_mode) && !(statbuf.st_mode &S_IXOTH)) // if folder and has no execute premission return false;
	{
		return false;
    } //EXECUTE TO OTHER 
    else if(!(statbuf.st_mode & S_IROTH)) // if file and has no read premission return false
    {
    	return false;
	} // READ TO OTHER 

	return true; 
}
void sendFile(char* path,int fd)
{
	int headerSize = getHeaderLength("200","OK",path);
	struct stat fileInfo;
	stat(path, &fileInfo);
	char timebuf[BUF_LEN];
	getDate(timebuf);
	long int htmlSize = fileInfo.st_size; // if were sending a file the html we need is the file size.
	long int totalSize = htmlSize+headerSize;
	char * response = (char*)malloc(totalSize);
	if(!response)
	{
		errorHandler(fd,"500 Internal server error","Some server side error",path);
		return;
	}
	memset(response,'\0',totalSize);
	strcat(response,"HTTP/1.0"); strcat(response, " 200 OK"); strcat(response, END_OF_LINE); // entering into response the http header.
	strcat(response, "Server: webserver/1.1\r\n");
	strcat(response, "Date: ");	strcat(response, timebuf);	strcat(response, END_OF_LINE);
	char * mime_type = get_mime_type(path);
	if(mime_type)
	{
		strcat(response, "Content-Type: ");
		char contType[32];
		memset(contType,'\0',32);
		strcpy(contType,mime_type);
		strcat(response,contType); strcat(response,END_OF_LINE);
	}
	strcat(response, "Content-Length: ");	char contentLen[BUF_LEN]; sprintf(contentLen, "%ld", htmlSize); strcat(response, contentLen); strcat(response, END_OF_LINE);
	char fileLM[BUF_LEN];
	memset(fileLM,'\0',BUF_LEN);
	strftime(fileLM, BUF_LEN, RFC1123FMT, gmtime(&fileInfo.st_mtime));
	strcat(response, "Last-Modified: "); strcat(response, fileLM); strcat(response,END_OF_LINE);
	strcat(response, "Connection: close\r\n\r\n");

	write(fd,response,strlen(response)); // writing the http header into fd.
	FILE* fileFD = fopen(path,"r");
	char *buf = (char*)malloc(fileInfo.st_size+1); 
	if(!buf)
	{
		errorHandler(fd,"500 Internal server error","Some server side error",path);
		return;
	}
	memset(buf,'\0',fileInfo.st_size+1);
	int r = 0;
	while( (r = fread(buf, 1, fileInfo.st_size+1, fileFD)) > 0) // reading the file into buf
		write(fd,buf,fileInfo.st_size+1);
	free(buf);
	fclose(fileFD);
	free(response);
	close(fd);
}
void handleDirectory(int sock_fd,char *path)
{
	struct dirent **fileList;
	int numOfFiles = scandir(path,&fileList,NULL, NULL); // scan dir in order to go thorugh all of its content 
	for (int i = 0; i < numOfFiles; ++i)
	{
		char* currFile = fileList[i]->d_name;
		if(strcmp(currFile,"index.html") == 0) // if we find an index.html file send it.
		{
			char *newPath = (char*)malloc(strlen(path)+11);
			if(!newPath)
			{	
				errorHandler(sock_fd,"500 Internal server error","Some server side error",path);
				return;
			}
			memset(newPath,'\0',strlen(path)+11);
			strcat(newPath,path);
			strcat(newPath,"index.html"); // adding to the old path /index.html and calling sendFile function.
			sendFile(newPath,sock_fd); 
			for (int i = 0; i < numOfFiles; ++i)
			{
				free(fileList[i]);
			}
			free(fileList);
			free(newPath);
			return;
		}
	}
	sendDir(sock_fd,path); // if we dont find index.html we call the sendDir function.
	for (int i = 0; i < numOfFiles; ++i)
	{
		free(fileList[i]);
	}
	free(fileList);
	return;
}
bool notEndsWithSlash(char *path) // private func to check if path does not end with slash.
{
	char * temp = strchr(path,'\0');
	temp = temp -1;
	if(strcmp(temp,"/") != 0)
		return true;
	else
		return false;
}
bool pathExists(char* path) // private function to check if path exists.
{
	if( access( path, F_OK ) != -1 )
		return true;
	else 
		return false;

}
int isDirectory(const char *path) // private function to check if path is a directory.
{
	struct stat statbuf;
	if (stat(path, &statbuf) != 0)
		return 0;
	return S_ISDIR(statbuf.st_mode);
}
int is_regular_file(const char *path) // private function to check if path is a regular file.
{
	struct stat path_stat;
	stat(path, &path_stat);
	return S_ISREG(path_stat.st_mode);
}
void sendDir(int fd,char* path)
{
	int headerSize = getHeaderLength("200","OK",path);
	int htmlSize = getHtmlLength("200","OK",path);
	int totalSize = headerSize + htmlSize;
	char* response = (char*)malloc(totalSize);
	memset(response,'\0',totalSize);
	if(!response)
	{
		errorHandler(fd,"500 Internal server error","Some server side error.",path);
		return;
	}
	struct stat dirStat;
	stat(path, &dirStat);
	char lastModified[BUF_LEN];
	strftime(lastModified, BUF_LEN, RFC1123FMT, gmtime(&dirStat.st_mtime));
	char timebuf[BUF_LEN];
	getDate(timebuf);
	strcat(response,"HTTP/1.0"); strcat(response, " 200 OK"); strcat(response, END_OF_LINE); // adding to reponse the http header as requested.
	strcat(response, "Server: webserver/1.1\r\n");
	strcat(response, "Date: ");	strcat(response, timebuf);	strcat(response, END_OF_LINE);
	strcat(response, "Content-Type: text/html\r\n");
	strcat(response, "Content-Length: ");	char contLen[BUF_LEN]; sprintf(contLen, "%d", htmlSize); strcat(response, contLen); strcat(response, END_OF_LINE);
	strcat(response, "Last-Modified: "); strcat(response, lastModified); strcat(response,END_OF_LINE);
	strcat(response, "Connection: close\r\n\r\n");
	struct dirent **fileList;
	int noOfFiles = scandir(path,&fileList,NULL, NULL); // scanning the directory to iterate through its content.

	strcat(response, "<HTML><HEAD><TITLE>Index of "); // entering the html code to display the directory as requested.
	strcat(response, path);
	strcat(response, "</TITLE></HEAD><BODY><H4>Index of ");
	strcat(response, path);
	strcat(response, "</H4><table CELLSPACING=8><tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>");
	char filePath[BUF_LEN];
	for (int i = 0; i < noOfFiles; i++) // for each file inside the directory we add a table row and adding the file into the table.
	{
		memset(filePath,'\0',BUF_LEN);
		struct stat fileStat;
		strcat(filePath,path);
		strcat(filePath,fileList[i]->d_name);
		stat(filePath, &fileStat);
		strcat(response, "<tr><td><A HREF=\"");
		strcat(response, fileList[i]->d_name);
		strcat(response, "\">");
		strcat(response, fileList[i]->d_name);
		strcat(response, "</A></td><td>");
		char fileLM[BUF_LEN];
		strftime(fileLM, BUF_LEN, RFC1123FMT, gmtime(&fileStat.st_mtime));
		strcat(response, fileLM);
		strcat(response,"</td><td>");
		
		if(S_ISDIR(fileStat.st_mode) == 0) // if file adding its size to the table.
		{
			char fileSize[BUF_LEN];
			memset(fileSize,'\0',BUF_LEN);
			sprintf(fileSize,"%ld", fileStat.st_size);
			strcat(response, fileSize);
		}
		strcat(response, "</td></tr>");
	}
	strcat(response, "</table><HR><ADDRESS>webserver/1.1</ADDRESS></BODY></HTML>");
	write(fd, response, totalSize); // write response into the fd.
	for (int i = 0; i < noOfFiles; ++i)
	{
		free(fileList[i]);
	}
	free(fileList);
	free(response);
	close(fd);
	return;
}
void getDate(char* timebuf) // private function that return the current date.
{
	time_t now;
	now = time(NULL);
	strftime(timebuf,BUF_LEN,RFC1123FMT,gmtime(&now));
}
bool checkRequest(char* req,int fd)
{
	char *test = (char*)malloc(strlen(req)+1);
	if(!test)
	{
		errorHandler(fd,"500 Internal server error","Some server side error",NULL);
		return EXIT_SUCCESS;
	}
	memset(test,'\0',strlen(req)+1);
	strcpy(test,req); // cpy req into test so we can use strtok on it.
	char delim[2] = " ";
	char *temp = strtok(test,delim);
	int i=0;
	while(temp != NULL)
	{
		i++;
		if(i == 3) // if 3rd token must be one of the http protocol.
		{
			if( (strcmp(temp,"HTTP/1.0\r\n") != 0) && (strcmp(temp,"HTTP/1.1\r\n") != 0) ) 
			{
				free(test);
				return false;
			}
		}
		temp = strtok(NULL,delim);
	}
	if(i != 3) // if at the end there is more or less than 3 token the bad request.
	{
		free(test);
		return false;
	}
	free(test);
	return true;
}

int getHeaderLength(char* errorType,char* errorInfo,char* path) // private function that return the length of the http header.
{
	int length = 0;
	length += strlen("HTTP/1.0    ");
	length += strlen(errorType);
	//length += strlen(errorInfo);
	length += strlen(END_OF_LINE);
	length += strlen("Server: webserver/1.0 ");
	length += strlen(END_OF_LINE);
	length += strlen("Date: ");
	length += BUF_LEN;
	length += strlen(END_OF_LINE);
	if( strcmp(errorType,"302 Found") == 0 )
	{
		length += strlen("Location:  ");
		length += strlen(path);
		length += strlen("/");
		length += strlen(END_OF_LINE);
	}
	length += strlen("Content-Type:  text/html");
	length += strlen(END_OF_LINE);
	length += strlen("Content-Length:   ");
	length += 256;
	if(strcmp(errorType,"200 OK") == 0)
	{
		length += strlen("Last-Modified:  ");
		length += BUF_LEN;
	}
	length += strlen(END_OF_LINE);
	length += strlen("Connection:  close");
	length += strlen(END_OF_LINE);
	length += strlen(END_OF_LINE);
	return length;
}
int getHtmlLength(char * errorType,char *info,char * path) // private function that returns the length of the html needed.
{
	int size = 0;
	struct dirent **fileList;
	struct stat dirStatus;
	stat(path, &dirStatus);
	int numOfFiles = scandir(path,&fileList,NULL, NULL);

	size += strlen("<HTML><HEAD><TITLE>Index of ");	size += strlen(path);
	size += strlen("</TITLE></HEAD><BODY><H4>Index of "); size += strlen(path);
	size += strlen("</H4><table CELLSPACING=8><tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>");
	char filePath[BUF_LEN];
	for (int i = 0; i < numOfFiles; i++)
	{
		memset(filePath,'\0',BUF_LEN);
		struct stat fileStat;
		strcpy(filePath,path);
		strcpy(filePath,fileList[i]->d_name);
		stat(filePath, &fileStat);
		size += strlen("<tr><td><A HREF=");
		size += strlen(fileList[i]->d_name);
		size += strlen("></A></td><td>");
		size += BUF_LEN;
		size += strlen("</td><td>");
		if(S_ISDIR(fileStat.st_mode) == 0)
			size += BUF_LEN;
		size += strlen("</td></tr>");
	}
	size += strlen("</table><HR><ADDRESS>webserver/1.0</ADDRESS></BODY></HTML>");
	for (int i = 0; i < numOfFiles; ++i)
	{
		free(fileList[i]);
	}
	free(fileList);
	return size;
}
void errorHandler(int fd,char* errorType,char* errorInfo,char* path)
{
	int headerSize = getHeaderLength(errorType,errorInfo,path);
	//headerSize = headerSize+ 1000;
	int htmlSize = getContentLengthForError(errorType);
	int totalSize = headerSize + htmlSize;
	char* error = (char*)malloc(totalSize);
	if(!error)
	{
		errorHandler(fd,"500 Internal server error","Some server side error",path);
		return;
	}
	memset(error,'\0',totalSize); // adding to error the http header for the current error.
	strcat(error,"HTTP/1.0 ");
	strcat(error,errorType);
	strcat(error,END_OF_LINE);
	strcat(error,"Server: webserver/1.0");
	strcat(error,END_OF_LINE);
	strcat(error,"Date: ");
	char timebuf[BUF_LEN];
	getDate(timebuf);
	strcat(error,timebuf);
	strcat(error,END_OF_LINE);
	if(strcmp(errorType,"302 Found") == 0)
	{
		strcat(error,"Location: /");
		strcat(error,path);
		strcat(error,"/");
		strcat(error,END_OF_LINE);
	}
	strcat(error,"Content-Type: text/html");
	strcat(error,END_OF_LINE);
	strcat(error,"Content-Length: ");
	char size[256];
	memset(size,'\0',256);
	sprintf(size,"%d",htmlSize);
	strcat(error,size);
	strcat(error,END_OF_LINE);
	strcat(error,"Connection: close");
	strcat(error,END_OF_LINE);
	strcat(error,END_OF_LINE);
	strcat(error,"<HTML><HEAD><TITLE>"); // adding the needed html code into error .
	strcat(error,errorType);
	strcat(error,"</TITLE></HEAD>");
	strcat(error,END_OF_LINE);
	strcat(error,"<BODY><H4>");
	strcat(error,errorType);
	strcat(error,"</H4>");
	strcat(error,END_OF_LINE);
	strcat(error,errorInfo);
	strcat(error,END_OF_LINE);
	strcat(error,"</BODY></HTML>");	 
	write(fd,error,totalSize); // writing the error to fd and closes.
	close(fd);
	free(error);
}
int getContentLengthForError(char* errorType)
{
	if ( strcmp(errorType, "302 Found") == 0) return 123;                 // pre calculated from the given error files.
	if ( strcmp(errorType, "400 Bad Request") == 0) return 113;
	if ( strcmp(errorType, "403 Forbiden") == 0) return 111;
	if ( strcmp(errorType, "404 Not Found") == 0) return 112;
	if ( strcmp(errorType, "500 Internal Server Error") == 0) return 144;
	if ( strcmp(errorType, "501 Not supported") == 0) return 129;
	return 0;
}
char * get_mime_type(char *name)
{
	char *ext = strrchr(name, '.');
	if (!ext) return NULL;
	if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
	if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
	if (strcmp(ext, ".gif") == 0) return "image/gif";
	if (strcmp(ext, ".png") == 0) return "image/png";
	if (strcmp(ext, ".css") == 0) return "text/css";
	if (strcmp(ext, ".au") == 0) return "audio/basic";
	if (strcmp(ext, ".wav") == 0) return "audio/wav";
	if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
	if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0) return "video/mpeg";
	if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
	return NULL;
}
bool isNumber(const char* num) 
{
	int i=0; // check each char in the array to see if its a digit.
	while(num[i] != '\0')
	{
		if(!isdigit(num[i]))
			return false;
		
		i++;
	}
	return true;
}