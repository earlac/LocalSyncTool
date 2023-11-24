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
#include <libgen.h>

#define PORT 8889
#define HOST "localhost"

typedef struct {
    char filename[256];
    off_t size;
    time_t mod_time;
    char status[32];
} FileInfo;

void error(const char *msg) {
    perror(msg);
    exit(0);
}

char *generateStateFileName(const char *directoryPath) {
    char *dirName = strdup(directoryPath);
    char *baseName = basename(dirName);
    char *stateFileName = malloc(strlen(baseName) + strlen("_state.txt") + 1);
    if (stateFileName == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    sprintf(stateFileName, "%s_state.txt", baseName);
    free(dirName);
    return stateFileName;
}

void readDirectory(const char *directoryPath, FileInfo **files, int *fileCount) {
    DIR *d = opendir(directoryPath);
    if (!d) {
        perror("Error al abrir el directorio");
        exit(EXIT_FAILURE);
    }

    struct dirent *dir;
    struct stat fileInfo;
    char filePath[1024];
    *fileCount = 0;
    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_REG) {
            snprintf(filePath, sizeof(filePath), "%s/%s", directoryPath, dir->d_name);
            if (stat(filePath, &fileInfo) == -1) {
                continue;
            }
            (*fileCount)++;
        }
    }

    *files = (FileInfo *)malloc(sizeof(FileInfo) * (*fileCount));
    if (!*files) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    rewinddir(d);
    int i = 0;
    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_REG) {
            snprintf(filePath, sizeof(filePath), "%s/%s", directoryPath, dir->d_name);
            if (stat(filePath, &fileInfo) == -1) {
                continue;
            }
            strncpy((*files)[i].filename, dir->d_name, sizeof((*files)[i].filename));
            (*files)[i].size = fileInfo.st_size;
            (*files)[i].mod_time = fileInfo.st_mtime;
            strcpy((*files)[i].status, "intacto");
            i++;
        }
    }
    closedir(d);
}

void readStateFile(const char *stateFileName, FileInfo **previousFiles, int *previousFileCount) {
    FILE *file = fopen(stateFileName, "r");
    if (!file) {
        *previousFiles = NULL;
        *previousFileCount = 0;
        return;
    }

    char line[1024];
    *previousFileCount = 0;
    while (fgets(line, sizeof(line), file)) {
        (*previousFileCount)++;
    }

    *previousFiles = (FileInfo *)malloc(sizeof(FileInfo) * (*previousFileCount));
    if (!*previousFiles) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    rewind(file);
    int i = 0;
    while (fgets(line, sizeof(line), file)) {
        sscanf(line, "%s %ld %ld %s", 
               (*previousFiles)[i].filename, 
               &(*previousFiles)[i].size, 
               &(*previousFiles)[i].mod_time, 
               (*previousFiles)[i].status);
        i++;
    }
    fclose(file);
}

void writeStateFile(const char *stateFileName, const FileInfo *files, int fileCount) {
    FILE *file = fopen(stateFileName, "w");
    if (!file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < fileCount; i++) {
        fprintf(file, "%s %lld %ld %s\n", 
                files[i].filename, 
                (long long)files[i].size, 
                (long)files[i].mod_time,
                files[i].status);
    }
    fclose(file);
}

void compareAndUpdateFileStates(FileInfo **currentFiles, int *currentFileCount, FileInfo *previousFiles, int previousFileCount) {
    int newCurrentFileCount = *currentFileCount + previousFileCount; // Máximo posible si todos los archivos anteriores fueron eliminados
    FileInfo *newCurrentFiles = (FileInfo *)realloc(*currentFiles, sizeof(FileInfo) * newCurrentFileCount);
    if (!newCurrentFiles) {
        perror("realloc");
        exit(EXIT_FAILURE);
    }
    *currentFiles = newCurrentFiles;

    for (int i = 0; i < *currentFileCount; i++) {
        int found = 0;
        for (int j = 0; j < previousFileCount; j++) {
            if (strcmp((*currentFiles)[i].filename, previousFiles[j].filename) == 0) {
                found = 1;
                if ((*currentFiles)[i].mod_time != previousFiles[j].mod_time) {
                    strcpy((*currentFiles)[i].status, "modificado");
                } else {
                    strcpy((*currentFiles)[i].status, "intacto");
                }
                break;
            }
        }
        if (!found) {
            strcpy((*currentFiles)[i].status, "nuevo");
        }
    }

    int currentIndex = *currentFileCount;
    for (int j = 0; j < previousFileCount; j++) {
        int found = 0;
        for (int i = 0; i < *currentFileCount; i++) {
            if (strcmp(previousFiles[j].filename, (*currentFiles)[i].filename) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            // Agrega el archivo eliminado a la lista actual con estado "eliminado"
            strcpy((*currentFiles)[currentIndex].filename, previousFiles[j].filename);
            (*currentFiles)[currentIndex].size = previousFiles[j].size;
            (*currentFiles)[currentIndex].mod_time = previousFiles[j].mod_time;
            strcpy((*currentFiles)[currentIndex].status, "eliminado");
            currentIndex++;
        }
    }
    *currentFileCount = currentIndex; // Actualizar el contador de archivos actuales
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <directorio>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *stateFileName = generateStateFileName(argv[1]);
    
    FileInfo *currentFiles, *previousFiles;
    int currentFileCount, previousFileCount;

    readDirectory(argv[1], &currentFiles, &currentFileCount);
    readStateFile(stateFileName, &previousFiles, &previousFileCount);

    compareAndUpdateFileStates(&currentFiles, &currentFileCount, previousFiles, previousFileCount);
    writeStateFile(stateFileName, currentFiles, currentFileCount);

    free(currentFiles);
    if (previousFiles != NULL) {
        free(previousFiles);
    }
    free(stateFileName);
    return 0;
}
