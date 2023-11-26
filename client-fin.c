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
#include <dirent.h>
#include <libgen.h>  // Para basename

#define PORT 8889
#define HOST "localhost"

typedef struct {
    char filename[256];
    off_t size;
    time_t mod_time;
} FileInfo;

void error(const char *msg) {
    perror(msg);
    exit(0);
}
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


// prototype

void readDirectory(const char *directoryPath, FileInfo **files, int *fileCount);
void loadPreviousState(const char *stateFilePath, FileInfo **previousFiles, int *previousFileCount);
char *generateStateFileName(const char *directoryPath);
void compareFileLists(const FileInfo *currentFiles, int currentFileCount, const FileInfo *previousFiles, int previousFileCount, FileChangeList *fileChangeList);

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
void enviarCambios(int sockfd, FileChangeList *fileChangeList) {
    char buffer[4096];
    bzero(buffer, 4096);

    for (int i = 0; i < fileChangeList->count; i++) {
        snprintf(buffer, sizeof(buffer), "File: %s, ChangeType: %d\n", fileChangeList->changes[i].filename, fileChangeList->changes[i].changeType);
        write(sockfd, buffer, strlen(buffer));
    }
    write(sockfd, "END_OF_CHANGES\n", strlen("END_OF_CHANGES\n"));
}

// funcion enviarArchivoAlServidor

void enviarArchivoAlServidor(int sockfd, const char *filename, const char *directoryPath) {
    char filePath[1024];
    snprintf(filePath, sizeof(filePath), "%s/%s", directoryPath, filename);


    FILE *file = fopen(filePath, "r");
    if (file == NULL) {
        perror("Error al abrir el archivo");
        printf("No se pudo acceder a: %s\n", filePath); // Mostrar el error con la ruta del archivo
        return;
    }

    char buffer[4096];
    
    bzero(buffer, 4096);
    
    int n;
     // Enviar los datos del archivo
    while ((n = fread(buffer, sizeof(char), 4095, file)) > 0) {
        write(sockfd, buffer, n);
        memset(buffer, 0, 4096); // Limpia el buffer después de cada envío
    }
    fclose(file);

    // Enviar el delimitador de fin de archivo
    write(sockfd, "END_OF_FILE", strlen("END_OF_FILE"));
}

void recibirInstruccionesDelServidor(int sockfd, const char *directoryPath) {
    char buffer[4096];
    char comando[10];
    char filename[256];

 while (1) {
        bzero(buffer, 4096);
        int n = read(sockfd, buffer, 4095);
        if (n < 0) 
            error("ERROR reading from socket");
        
        if (strncmp(buffer, "END_OF_COMMUNICATION", 20) == 0) {
            break; // Fin de la comunicación
        }

        sscanf(buffer, "%[^:]:%s", comando, filename);
        
        if (strcmp(comando, "DELETE") == 0) {
            // Eliminar el archivo localmente
        } else if (strcmp(comando, "REQUEST") == 0) {
            printf("Solicitud de archivo: %s\n", filename);
            // Enviar archivo solicitado al servidor
            enviarArchivoAlServidor(sockfd, filename, directoryPath);
        } else if (strcmp(comando, "SEND") == 0) {
            // Recibir archivo del servidor
        }

        bzero(comando, 10);
        bzero(filename, 256);
    }
}
void startClient(const char *directoryPath, FileChangeList *fileChangeList) {
    int sockfd, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char buffer[4096]; // Ajustar el tamaño según sea necesario

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    server = gethostbyname(HOST); // Asegúrate de que esta sea la dirección IP correcta
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

    // Leer los cambios y enviarlos al servidor
    enviarCambios(sockfd, fileChangeList);

    // Leer la respuesta del servidor
    recibirInstruccionesDelServidor(sockfd, directoryPath);
    close(sockfd);
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
char *generateStateFileName(const char *directoryPath) {
    char *dirName = strdup(directoryPath);  // Duplicar la ruta del directorio para no modificar el original
    char *baseName = basename(dirName);     // Obtener el nombre base del directorio

    char *stateFileName = malloc(strlen(baseName) + strlen("_state.txt") + 1);
    if (stateFileName == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    sprintf(stateFileName, "%s_state.txt", baseName); // Crear el nombre del archivo de estado
    free(dirName);  // Liberar la memoria duplicada

    return stateFileName;  // Retornar el nombre del archivo de estado
}

void compareFileLists(const FileInfo *currentFiles, int currentFileCount, const FileInfo *previousFiles, int previousFileCount, FileChangeList *fileChangeList) {
    int maxChanges = currentFileCount + previousFileCount;
    fileChangeList->changes = malloc(sizeof(FileChange) * maxChanges);
    fileChangeList->count = 0;

    // Identificar archivos nuevos o modificados
    for (int i = 0; i < currentFileCount; i++) {
        int found = 0;
        for (int j = 0; j < previousFileCount; j++) {
            if (strcmp(currentFiles[i].filename, previousFiles[j].filename) == 0) {
                found = 1;
                if (currentFiles[i].size != previousFiles[j].size || currentFiles[i].mod_time != previousFiles[j].mod_time) {
                    strcpy(fileChangeList->changes[fileChangeList->count].filename, currentFiles[i].filename);
                    fileChangeList->changes[fileChangeList->count++].changeType = MODIFIED;
                }
                break;
            }
        }
        if (!found) {
            strcpy(fileChangeList->changes[fileChangeList->count].filename, currentFiles[i].filename);
            fileChangeList->changes[fileChangeList->count++].changeType = NEW;
        }
    }

    // Identificar archivos eliminados
    for (int i = 0; i < previousFileCount; i++) {
        int found = 0;
        for (int j = 0; j < currentFileCount; j++) {
            if (strcmp(previousFiles[i].filename, currentFiles[j].filename) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            strcpy(fileChangeList->changes[fileChangeList->count].filename, previousFiles[i].filename);
            fileChangeList->changes[fileChangeList->count++].changeType = DELETED;
            }
        }
    
}



int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,"Uso: %s <directorio>\n", argv[0]);
        exit(0);
    }

    char *stateFileName = generateStateFileName(argv[1]);
    
    printf("Nombre del archivo de estado: %s\n", stateFileName);

    //print argv[1] files name and mod time
    printf("Directorio: %s\n", argv[1]);

    FileInfo *currentFiles, *previousFiles;
    FileChangeList fileChangeList;

    int currentFileCount, previousFileCount;

    readDirectory(argv[1], &currentFiles, &currentFileCount);

    loadPreviousState(stateFileName, &previousFiles, &previousFileCount);

    compareFileLists(currentFiles, currentFileCount, previousFiles, previousFileCount, &fileChangeList);

    startClient(argv[1], &fileChangeList);
    return 0;
}


/*
    //print fileChangeList changes
    for (int i = 0; i < fileChangeList.count; i++) {
        printf("Archivo: %s\n", fileChangeList.changes[i].filename);
        switch (fileChangeList.changes[i].changeType) {
            case NEW:
                printf("Tipo de cambio: Nuevo\n");
                break;
            case MODIFIED:
                printf("Tipo de cambio: Modificado\n");
                break;
            case DELETED:
                printf("Tipo de cambio: Eliminado\n");
                break;
        }
    }

    //print current files name and mod time
    for (int i = 0; i < currentFileCount; i++) {
        printf("Archivo: %s\n", currentFiles[i].filename);
        printf("Tamaño: %ld bytes\n", currentFiles[i].size);
        printf("Última modificación: %s\n", ctime(&currentFiles[i].mod_time));
    }

    //print previous files name and mod time
    for (int i = 0; i < previousFileCount; i++) {
        printf("Archivo: %s\n", previousFiles[i].filename);
        printf("Tamaño: %ld bytes\n", previousFiles[i].size);
        printf("Última modificación: %s\n", ctime(&previousFiles[i].mod_time));
    }
    free(fileChangeList.changes);
*/