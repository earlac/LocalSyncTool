#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/stat.h>
#include <time.h>

#define PORT 8889

void error(const char *msg) {
    perror(msg);
    exit(0);
}

void printFileMetadata(const char *directoryPath, const char *filename) {
    char filePath[1024];
    struct stat fileInfo;
    struct tm *tm;

    // Construir la ruta completa del archivo
    snprintf(filePath, sizeof(filePath), "%s/%s", directoryPath, filename);
    printf("Intentando acceder a: %s\n", filePath); // Mostrar la ruta del archivo

    // Obtener información del archivo
    if (stat(filePath, &fileInfo) < 0) {
        perror("Error al obtener información del archivo");
        printf("No se pudo acceder a: %s\n", filePath); // Mostrar el error con la ruta del archivo
        return;
    }

    // Convertir la fecha y hora de la última modificación a formato legible
    tm = localtime(&fileInfo.st_mtime);

    // Imprimir detalles
    printf("Archivo: %s\n", filename);

    // Usar printf con formato adecuado para off_t
    #ifdef __APPLE__
    printf("Tamaño: %lld bytes\n", fileInfo.st_size); // macOS usa %lld para off_t
    #else
    printf("Tamaño: %ld bytes\n", fileInfo.st_size);  // Otros sistemas suelen usar %ld
    #endif

    printf("Última modificación: %d-%02d-%02d %02d:%02d:%02d\n",
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec);
}

void startClient(const char *directoryPath) {
    int sockfd, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char buffer[256];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");
    server = gethostbyname("172.19.12.86"); // Asegúrate de que esta sea la dirección IP correcta
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(PORT);

    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
        error("ERROR connecting");

    write(sockfd, directoryPath, strlen(directoryPath)); // Enviar el nombre del directorio al servidor

        while (1) {
        bzero(buffer, 256);
        n = read(sockfd, buffer, 255);
        if (n < 0) 
             error("ERROR reading from socket");
        if (strcmp(buffer, "end") == 0) {
            break;
        }
        if (strlen(buffer) > 1) {  // Asegurar que el buffer no esté vacío o no sea solo un salto de línea
            buffer[strcspn(buffer, "\n")] = 0; // Eliminar el salto de línea al final
            printFileMetadata(directoryPath, buffer); // Imprimir metadatos del archivo
        }
    }
    close(sockfd);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,"Uso: %s <directorio>\n", argv[0]);
        exit(0);
    }

    startClient(argv[1]);
    return 0;
}
