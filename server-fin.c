#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h> // Para basename
#include <time.h>

#define PORT 8889

// structs ---------------------------------------------------------------------------------------------------------

typedef struct
{
    char filename[256];
    off_t size;
    time_t mod_time;
} FileInfo;
typedef enum
{
    NEW,
    MODIFIED,
    DELETED
} FileChangeType;

typedef struct
{
    char filename[256];
    FileChangeType changeType;
} FileChange;

typedef struct
{
    FileChange *changes;
    int count;
} FileChangeList;

// Estructura para almacenar la información de un archivo
typedef struct
{
    char filename[256]; // Nombre del archivo
} FileEntry;

// Estructura para una lista de archivos
typedef struct
{
    FileEntry *entries; // Array dinámico de entradas de archivos
    int count;          // Número actual de archivos en la lista
    int size;           // Tamaño actual del array
} FileList;

// prototypes ------------------------------------------------------------------------------------------------------
char *generateStateFileName(
    const char *directoryPath);
void readDirectory(
    const char *directoryPath,
    FileInfo **files,
    int *fileCount);
void loadPreviousState(
    const char *stateFilePath,
    FileInfo **previousFiles,
    int *previousFileCount);
void startServer(
    const char *directoryPath,
    FileInfo *currentFiles,
    int currentFileCount,
    FileInfo *previousFiles,
    int previousFileCount);
void processClientChanges(
    int socket,
    FileInfo *currentFiles,
    int currentFileCount,
    FileInfo *previousFiles,
    int previousFileCount,
    const char *directoryPath);
void sincronizarDirectorio(
    FileList *archivosNuevosCliente,
    FileList *archivosModificadosCliente,
    FileList *archivosEliminadosCliente,
    FileInfo *currentFiles,
    int currentFileCount,
    FileInfo *previousFiles,
    int previousFileCount,
    const char *directoryPath,
    int socket);
// functions -------------------------------------------------------------------------------------------------------
// Inicializa una lista de archivos
void initFileList(FileList *list, int initialSize)
{
    list->entries = malloc(sizeof(FileEntry) * initialSize);
    list->count = 0;
    list->size = initialSize;
}

// Agrega un archivo a la lista
void addToFileList(FileList *list, const char *filename)
{
    if (list->count == list->size)
    {
        // Redimensiona el array si se alcanza el límite de tamaño
        list->size *= 2;
        list->entries = realloc(list->entries, sizeof(FileEntry) * list->size);
    }
    strncpy(list->entries[list->count].filename, filename, sizeof(list->entries[list->count].filename));
    list->count++;
}

// Libera la memoria utilizada por la lista de archivos
void freeFileList(FileList *list)
{
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
    list->size = 0;
}

// funcion contieneArchivo
int contieneArchivo(FileList *lista, const char *filename)
{
    for (int i = 0; i < lista->count; i++)
    {
        if (strcmp(lista->entries[i].filename, filename) == 0)
        {
            return 1;
        }
    }
    return 0;
}

// Eliminar archivo del servidor solo con filename
void eliminarArchivoServidor(const char *filename, const char *directoryPath)
{
    // build fullpath

    char fullPath[1024];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", directoryPath, filename);

    if (remove(fullPath) != 0)
    {
        perror("Error al eliminar archivo");
    }
    else
    {
        printf("Archivo eliminado: %s\n", fullPath);
    }
}

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

void listFiles(int socket, const char *directoryPath)
{
    DIR *d;
    struct dirent *dir;
    d = opendir(directoryPath);
    char buffer[1024];

    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            if (dir->d_type == DT_REG)
            { // Asegurar que es un archivo regular
                snprintf(buffer, sizeof(buffer), "%s\n", dir->d_name);
                write(socket, buffer, strlen(buffer));
            }
        }
        closedir(d);
    }
    write(socket, "end", strlen("end")); // Marca el fin de la lista de archivos
}

void processClientChanges(int socket, FileInfo *currentFiles, int currentFileCount, FileInfo *previousFiles, int previousFileCount, const char *directoryPath)
{
    char buffer[4096];
    int n;

    FileList archivosNuevosCliente, archivosModificadosCliente, archivosEliminadosCliente;
    initFileList(&archivosNuevosCliente, 10);
    initFileList(&archivosModificadosCliente, 10);
    initFileList(&archivosEliminadosCliente, 10);

    bzero(buffer, 4096);
    n = read(socket, buffer, 4095);
    if (n < 0)
        error("ERROR reading from socket");

    buffer[n] = '\0'; // Asegurar que el buffer es una cadena válida
    // Procesamiento de los cambios

    char *change = strtok(buffer, "\n");

    while (change != NULL)
    {
        char filename[256] = {0};
        int changeType;
        sscanf(change, "File: %[^,], ChangeType: %d", filename, &changeType); // Nota: %[^,] para leer hasta la coma
        switch (changeType)
        {
        case NEW:
            addToFileList(&archivosNuevosCliente, filename);
            break;
        case MODIFIED:
            addToFileList(&archivosModificadosCliente, filename);
            break;
        case DELETED:
            addToFileList(&archivosEliminadosCliente, filename);
            break;
        }
        change = strtok(NULL, "\n");
    }
    //END_OF_CHANGES clear msg

    // print archivosnuevoscliente
    printf("Archivos nuevos del cliente:\n");
    for (int i = 0; i < archivosNuevosCliente.count; i++)
    {
        printf("%s\n", archivosNuevosCliente.entries[i].filename);
    }
    // print archivosmodificadoscliente
    printf("Archivos modificados del cliente:\n");
    for (int i = 0; i < archivosModificadosCliente.count; i++)
    {
        printf("%s\n", archivosModificadosCliente.entries[i].filename);
    }
    // print archivoseliminadoscliente
    printf("Archivos eliminados del cliente:\n");
    for (int i = 0; i < archivosEliminadosCliente.count; i++)
    {
        printf("%s\n", archivosEliminadosCliente.entries[i].filename);
    }

    // Lógica para sincronizar directorio
    sincronizarDirectorio(&archivosNuevosCliente, &archivosModificadosCliente, &archivosEliminadosCliente, currentFiles, currentFileCount, previousFiles, previousFileCount, directoryPath, socket);


    // Liberar memoria de las listas
    freeFileList(&archivosNuevosCliente);
    freeFileList(&archivosModificadosCliente);
    freeFileList(&archivosEliminadosCliente);
}

char *generateStateFileName(const char *directoryPath)
{
    char *dirName = strdup(directoryPath); // Duplicar la ruta del directorio para no modificar el original
    char *baseName = basename(dirName);    // Obtener el nombre base del directorio

    char *stateFileName = malloc(strlen(baseName) + strlen("_state-server.txt") + 1);
    if (stateFileName == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    sprintf(stateFileName, "%s_state.txt", baseName); // Crear el nombre del archivo de estado
    free(dirName);                                    // Liberar la memoria duplicada

    return stateFileName; // Retornar el nombre del archivo de estado
}

void readDirectory(const char *directoryPath, FileInfo **files, int *fileCount)
{
    DIR *d;
    struct dirent *dir;
    d = opendir(directoryPath);
    if (!d)
    {
        perror("Error al abrir el directorio");
        exit(1);
    }

    *fileCount = 0;
    while ((dir = readdir(d)) != NULL)
    {
        if (dir->d_type == DT_REG)
        { // Asegurar que es un archivo regular
            *fileCount += 1;
        }
    }

    *files = malloc(sizeof(FileInfo) * (*fileCount));
    if (!*files)
    {
        perror("Error al asignar memoria");
        exit(1);
    }

    rewinddir(d);
    int i = 0;
    struct stat fileInfo;

    while ((dir = readdir(d)) != NULL)
    {
        if (dir->d_type == DT_REG)
        { // Asegurar que es un archivo regular
            snprintf((*files)[i].filename, sizeof((*files)[i].filename), "%s", dir->d_name);
            char filePath[1024];
            snprintf(filePath, sizeof(filePath), "%s/%s", directoryPath, dir->d_name);

            if (stat(filePath, &fileInfo) < 0)
            {
                perror("Error al obtener información del archivo");
                printf("No se pudo acceder a: %s\n", filePath); // Mostrar el error con la ruta del archivo
                continue;
            }

            (*files)[i].size = fileInfo.st_size;
            (*files)[i].mod_time = fileInfo.st_mtime;
            i++;
        }
    }
    closedir(d);
}

void loadPreviousState(const char *stateFilePath, FileInfo **previousFiles, int *previousFileCount)
{
    FILE *file = fopen(stateFilePath, "r");
    if (file == NULL)
    {
        printf("No se encontró el archivo de estado: %s\n", stateFilePath);
        *previousFiles = NULL;
        *previousFileCount = 0;
        return;
    }
    char line[1024];
    *previousFileCount = 0;
    while (fgets(line, sizeof(line), file))
    {
        (*previousFileCount)++;
    }

    // Asignar memoria para los archivos previos
    *previousFiles = malloc(sizeof(FileInfo) * (*previousFileCount));
    if (!*previousFiles)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    // Volver al inicio del archivo para leer los datos
    rewind(file);
    int idx = 0;
    while (fgets(line, sizeof(line), file))
    {
        sscanf(line, "%s %ld %ld",
               (*previousFiles)[idx].filename,
               &(*previousFiles)[idx].size,
               &(*previousFiles)[idx].mod_time);
        idx++;
    }

    fclose(file);
}

void startServer(const char *directoryPath, FileInfo *currentFiles, int currentFileCount, FileInfo *previousFiles, int previousFileCount)
{
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    bzero((char *)&serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
    if (newsockfd < 0)
        error("ERROR on accept");

    processClientChanges(newsockfd, currentFiles, currentFileCount, previousFiles, previousFileCount, directoryPath); // Procesar los cambios enviados por el cliente
    close(newsockfd);
    close(sockfd);

    
}
FileInfo *obtenerInfoArchivo(FileInfo *archivos, int fileCount, const char *filename)
{
    for (int i = 0; i < fileCount; i++)
    {
        if (strcmp(archivos[i].filename, filename) == 0)
        {
            return &archivos[i]; // Devuelve la referencia al FileInfo encontrado
        }
    }
    return NULL; // No se encontró el archivo
}
int contieneFileInfo(FileInfo *archivos, int fileCount, const char *filename)
{
    for (int i = 0; i < fileCount; i++)
    {
        if (strcmp(archivos[i].filename, filename) == 0)
        {
            return 1;
        }
    }
    return 0;
}void recibirArchivoDelCliente(int socket, const char *destPath) {
    FILE *file = fopen(destPath, "wb");
    if (file == NULL) {
        perror("Error al abrir el archivo para escritura");
        return;
    }

    char buffer[4096];
    int bytesReceived;

    while (1) {
        bytesReceived = recv(socket, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            perror("Error en la recepción del archivo o conexión cerrada");
            break;
        }

        fwrite(buffer, sizeof(char), bytesReceived, file);

        if (bytesReceived < sizeof(buffer)) {
            // Si se recibe menos datos que el tamaño del buffer, se llegó al final del archivo
            break;
        }
    }

    fclose(file);
}



void enviarInstruccionesAlCliente( int socket, FileList *archivosParaEnviarAlCliente, FileList *archivosParaSolicitarAlCliente, FileList *archivosEnConflicto)
{
    char buffer[4096];
    bzero(buffer, 4096);
    int n;
    // solicitar archivos al cliente
    if (archivosParaSolicitarAlCliente->count > 0)
    {
        for (int i = 0; i < archivosParaSolicitarAlCliente->count; i++)
        {
        snprintf(buffer, sizeof(buffer), "REQUEST:%s\n", archivosParaSolicitarAlCliente->entries[i].filename);
        printf("Solicitando archivo al cliente: %s\n", archivosParaSolicitarAlCliente->entries[i].filename);
            n = write(socket, buffer, strlen(buffer));
            if (n < 0)
                error("ERROR writing to socket");
        }
    }
    write(socket, "END_OF_COMMUNICATION\n", strlen("END_OF_COMMUNICATION\n"));

}


void sincronizarDirectorio(
    FileList *archivosNuevosCliente, 
    FileList *archivosModificadosCliente, 
    FileList *archivosEliminadosCliente, 
    FileInfo *currentFiles, 
    int currentFileCount, 
    FileInfo *previousFiles, 
    int previousFileCount, 
    const char *directoryPath,
    int socket)
{
    // Lógica para sincronizar directorio
    // 1. eliminar archivos eliminados del cliente
    // 2. manejo de modificaciones
    // 3. archivos nuevos en el servidor
    // 4. archivos modificados en el servidor
    // 5. archivos nuevos en el cliente
    // 6. archivos en conflicto (modificados en el servidor y en el cliente) 


    // Listas para manejar la sincronización y conflictos
    FileList archivosParaEnviarAlCliente, archivosParaSolicitarAlCliente, archivosEnConflicto;
    initFileList(&archivosParaEnviarAlCliente, 10);
    initFileList(&archivosParaSolicitarAlCliente, 10);
    initFileList(&archivosEnConflicto, 10);

    // Analizamos cada archivo en el servidor
    for (int i = 0; i < currentFileCount; i++)
    {
        FileInfo *previousFileInfo = previousFiles ? obtenerInfoArchivo(previousFiles, previousFileCount, currentFiles[i].filename) : NULL;

        printf("Analizando archivo: %s\n", currentFiles[i].filename);
        // 1. Eliminar archivos eliminados del cliente
        if (contieneArchivo(archivosEliminadosCliente, currentFiles[i].filename))
        {
            eliminarArchivoServidor(currentFiles[i].filename, directoryPath);
        }
        // 2. Manejo de modificaciones
        else if (contieneArchivo(archivosModificadosCliente, currentFiles[i].filename))
        {
            // 2.1. Si el archivo fue modificado en el servidor hay conflicto
            if (currentFiles[i].mod_time != previousFiles[i].mod_time)
            {
                addToFileList(&archivosEnConflicto, currentFiles[i].filename);
            }
            // 2.2. Si el archivo fue modificado en el cliente, solicitarlo al cliente
            else
            {
                addToFileList(&archivosParaSolicitarAlCliente, currentFiles[i].filename);
            }
        }
        // 3. archivos nuevos en el servidor
        else if (!previousFileInfo)
        {
            printf("Archivo nuevo en el servidor: %s\n", currentFiles[i].filename);
            addToFileList(&archivosParaEnviarAlCliente, currentFiles[i].filename);
        }
        // 4. archivos modificados en el servidor
        else if (currentFiles[i].mod_time != previousFileInfo->mod_time)
        {
            printf("Archivo modificado en el servidor: %s\n", currentFiles[i].filename);
            addToFileList(&archivosParaEnviarAlCliente, currentFiles[i].filename);
        }

        // 5. archivos nuevos en el cliente

        for (int i = 0; i < archivosNuevosCliente->count; i++)
        {
            if (!contieneFileInfo(currentFiles, currentFileCount, archivosNuevosCliente->entries[i].filename))
            {
                printf("Archivo nuevo en el cliente: %s\n", archivosNuevosCliente->entries[i].filename);
                addToFileList(&archivosParaSolicitarAlCliente, archivosNuevosCliente->entries[i].filename);
            }
        }

        // enviar archivos al cliente
        enviarInstruccionesAlCliente(socket, &archivosParaEnviarAlCliente, &archivosParaSolicitarAlCliente, &archivosEnConflicto);  
        // recibir archivos del cliente
        for (int i = 0; i < archivosParaSolicitarAlCliente.count; i++)
        {
            printf("Recibiendo archivo del cliente: %s\n", archivosParaSolicitarAlCliente.entries[i].filename);
            char destPath[1024];
            snprintf(destPath, sizeof(destPath), "%s/%s", directoryPath, archivosParaSolicitarAlCliente.entries[i].filename);
            recibirArchivoDelCliente(socket, destPath);
        }
        // Liberar memoria de las listas
        freeFileList(&archivosParaEnviarAlCliente);
        freeFileList(&archivosParaSolicitarAlCliente);
        freeFileList(&archivosEnConflicto);

    }

}
// main ------------------------------------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Uso: %s <directorio>\n", argv[0]);
        exit(1);
    }

    char *stateFileName = generateStateFileName(argv[1]);

    FileInfo *currentFiles, *previousFiles;
    FileChangeList fileChangeList;

    int currentFileCount, previousFileCount;
    readDirectory(argv[1], &currentFiles, &currentFileCount);
    loadPreviousState(stateFileName, &previousFiles, &previousFileCount);

    startServer(argv[1], currentFiles, currentFileCount, previousFiles, previousFileCount);

    free(currentFiles);
    free(previousFiles);
    free(stateFileName);
    return 0;
}