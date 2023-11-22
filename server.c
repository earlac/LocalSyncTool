#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <dirent.h>

#define PORT 8889

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void listFiles(int socket, const char *directoryPath) {
    DIR *d;
    struct dirent *dir;
    d = opendir(directoryPath);
    char buffer[256];

    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) { // Asegurar que es un archivo regular
                snprintf(buffer, sizeof(buffer), "%s\n", dir->d_name);
                write(socket, buffer, strlen(buffer));
            }
        }
        closedir(d);
    }
    write(socket, "end", strlen("end")); // Marca el fin de la lista de archivos
}


void startServer(const char *directoryPath) {
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
       error("ERROR opening socket");

    bzero((char *) &serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) 
            error("ERROR on binding");

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
    if (newsockfd < 0) 
          error("ERROR on accept");

    listFiles(newsockfd, directoryPath);
    close(newsockfd);
    close(sockfd);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,"Uso: %s <directorio>\n", argv[0]);
        exit(1);
    }

    startServer(argv[1]);
    return 0;
}
