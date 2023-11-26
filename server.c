#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>  // Para basename
#include <time.h>

#define PORT 8889

//prototypes
char *generateStateFileName(const char *directoryPath);
typedef struct {
    char filename[256];
    off_t size;
    time_t mod_time;
} FileInfo;
typedef enum {
    NEW,
    MODIFIED,
    DELETED
} FileChangeType;

typedef struct {
    char filename[256];
    FileChangeType changeType;
} FileChange;

typedef struct {
    FileChange *changes;
    int count;
} FileChangeList;

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void listFiles(int socket, const char *directoryPath) {
    DIR *d;
    struct dirent *dir;
    d = opendir(directoryPath);
    char buffer[1024];

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

void processClientChanges(int socket) {
    char buffer[1024];
    int n;

    while (1) {
        bzero(buffer, 1024);
        n = read(socket, buffer, 1023);
        if (n < 0) error("ERROR reading from socket");
        if (n == 0) break; // Fin de la transmisión

        buffer[n] = '\0'; // Asegurar que el buffer es una cadena válida

        // Procesar el mensaje recibido
        if (strncmp(buffer, "END_OF_CHANGES", 14) == 0) {
            break; // Fin de la recepción de cambios
        }

        // Aquí, analizar el buffer para extraer el nombre del archivo y su tipo de cambio
        printf("%s", buffer); // Ejemplo de procesamiento simple

        // Aquí, puedes agregar lógica para manejar los cambios recibidos
        // Por ejemplo, actualizar archivos, descargarlos, etc.
    }
}

char *generateStateFileName(const char *directoryPath) {
    char *dirName = strdup(directoryPath);  // Duplicar la ruta del directorio para no modificar el original
    char *baseName = basename(dirName);     // Obtener el nombre base del directorio

    char *stateFileName = malloc(strlen(baseName) + strlen("_state-server.txt") + 1);
    if (stateFileName == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    sprintf(stateFileName, "%s_state.txt", baseName); // Crear el nombre del archivo de estado
    free(dirName);  // Liberar la memoria duplicada

    return stateFileName;  // Retornar el nombre del archivo de estado
}

void readDirectory(const char *directoryPath, FileInfo **files, int *fileCount) {
    DIR *d;
    struct dirent *dir;
    d = opendir(directoryPath);
    if(!d) {
        perror("Error al abrir el directorio");
        exit(1);
    }

    *fileCount = 0;
    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_REG) { // Asegurar que es un archivo regular
            *fileCount += 1;
        }
    }

    *files = malloc(sizeof(FileInfo) * (*fileCount));
    if(!*files) {
        perror("Error al asignar memoria");
        exit(1);
    }

    rewinddir(d);
    int i = 0;
    struct stat fileInfo;

    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_REG) { // Asegurar que es un archivo regular
            snprintf((*files)[i].filename, sizeof((*files)[i].filename), "%s", dir->d_name);

            char filePath[1024];
            snprintf(filePath, sizeof(filePath), "%s/%s", directoryPath, dir->d_name);

            if (stat(filePath, &fileInfo) < 0) {
                perror("Error al obtener información del archivo");
                printf("No se pudo acceder a: %s\n", filePath); // Mostrar el error con la ruta del archivo
                continue;
            }

            (*files)[i].size = fileInfo.st_size;
            (*files)[i].mod_time = fileInfo.st_mtime;
            printf("Archivo: %s\n", (*files)[i].filename);
            i++;
        }
    }
    closedir(d);
}

void loadPreviousState(const char *stateFilePath, FileInfo **previousFiles, int *previousFileCount) {
    FILE *file = fopen(stateFilePath, "r");
    if (file == NULL) {
        printf("No se encontró el archivo de estado: %s\n", stateFilePath);
        *previousFiles = NULL;
        *previousFileCount = 0;
        return;
    }
    char line[1024];
    *previousFileCount = 0;
    while (fgets(line, sizeof(line), file)) {
        (*previousFileCount)++;
    }

    // Asignar memoria para los archivos previos
    *previousFiles = malloc(sizeof(FileInfo) * (*previousFileCount));
    if (!*previousFiles) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
 // Volver al inicio del archivo para leer los datos
    rewind(file);
    int idx = 0;
    while (fgets(line, sizeof(line), file)) {
        sscanf(line, "%s %ld %ld", 
               (*previousFiles)[idx].filename, 
               &(*previousFiles)[idx].size, 
               &(*previousFiles)[idx].mod_time);
        idx++;
    }

    fclose(file);
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

    processClientChanges(newsockfd); // Procesar los cambios enviados por el cliente
    close(newsockfd);
    close(sockfd);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,"Uso: %s <directorio>\n", argv[0]);
        exit(1);
    }

    char *stateFileName = generateStateFileName(argv[1]);

    printf("Nombre del archivo de estado: %s\n", stateFileName);

    printf("Directorio a monitorear: %s\n", argv[1]);

    FileInfo *currentFiles, *previousFiles;
    FileChangeList fileChangeList;

    int currentFileCount, previousFileCount;
    readDirectory(argv[1], &currentFiles, &currentFileCount);
    loadPreviousState(stateFileName, &previousFiles, &previousFileCount);

    //print current files name and mod time
    for (int i = 0; i < currentFileCount; i++) {
        printf("Archivo: %s\n", currentFiles[i].filename);
        printf("Tamaño: %ld bytes\n", currentFiles[i].size);
        printf("Última modificación: %s\n", ctime(&currentFiles[i].mod_time));
    }

    startServer(argv[1]);
    return 0;
}
