
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <termios.h>
#include "util.h"

#define LIMIT 256 // max number of tokens for a command
#define MAXLINE 1024 // max number of characters from user input

char currentDirectory[MAXLINE];

void shellPrompt() {
    printf("wish %s > ", currentDirectory);
}

void init() {
    // Verificar si estamos ejecutando de manera interactiva
    GBSH_PID = getpid();
    // El shell es interactivo si STDIN es la terminal
    GBSH_IS_INTERACTIVE = isatty(STDIN_FILENO);  

    if (GBSH_IS_INTERACTIVE) {
        // Bucle hasta que estemos en primer plano
        while (tcgetpgrp(STDIN_FILENO) != (GBSH_PGID = getpgrp()))
            kill(GBSH_PID, SIGTTIN);             

        // Establecer los manejadores de señales para SIGCHILD y SIGINT
        act_child.sa_handler = signalHandler_child;
        act_int.sa_handler = signalHandler_int;			

        sigaction(SIGCHLD, &act_child, 0);
        sigaction(SIGINT, &act_int, 0);

        // Ponernos en nuestro propio grupo de procesos
        setpgid(GBSH_PID, GBSH_PID); // hacemos que el proceso del shell sea el líder del nuevo grupo de procesos
        GBSH_PGID = getpgrp();
        if (GBSH_PID != GBSH_PGID) {
            printf("Error, el shell no es el líder del grupo de procesos");
            exit(EXIT_FAILURE);
        }
        // Tomar el control de la terminal
        tcsetpgrp(STDIN_FILENO, GBSH_PGID);  

        // Guardar los atributos de terminal predeterminados para el shell
        tcgetattr(STDIN_FILENO, &GBSH_TMODES);

        // Obtener el directorio actual que se utilizará en diferentes métodos
        if (getcwd(currentDirectory, sizeof(currentDirectory)) == NULL) {
            perror("getcwd() error");
            exit(EXIT_FAILURE);
        }

        // Establecemos la variable de entorno shell con "wish" seguido de la ruta actual
        char wish_path[MAXLINE];
        snprintf(wish_path, sizeof(wish_path), "wish %s", currentDirectory);
        setenv("shell", wish_path, 1);
    } else {
        printf("No se pudo hacer que el shell fuera interactivo.\n");
        exit(EXIT_FAILURE);
    }
}


//manejadores de señal
void signalHandler_child(int p){
    // Esperar a que todos los procesos muertos sean limpiados.
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
    printf("\n");
}


//Manejador de señales para SIGINT(ctrl+c) finalización de un proceso
void signalHandler_int(int p){
    // Enviamos una señal SIGTERM al proceso hijo
    if (kill(pid,SIGTERM) == 0){
        printf("\nProceso %d recibió una señal SIGINT\n",pid);
        no_reprint_prmpt = 1;           
    }else{
        printf("\n");
    }
}


void shellPrompt(){
    // Imprimimos el indicador de comando en la forma "<usuario>@<host> <cwd> >"
    char hostn[1204] = "";
    gethostname(hostn, sizeof(hostn));
    printf("%s@%s %s > ", getenv("LOGNAME"), hostn, getcwd(currentDirectory, 1024));
}


//Método para cambiar de directorio
int changeDirectory(char* args[]){
    // Si no escribimos una ruta (solo 'cd'), entonces vamos al directorio principal(raíz)
    if (args[1] == NULL) {
        chdir(getenv("HOME")); 
        return 1;
    }
    // De lo contrario, cambiamos al directorio especificado por el argumento, si es posible (cd holi)
    else{ 
        if (chdir(args[1]) == -1) {
            printf(" %s: no existe el directorio\n", args[1]);
            return -1;
        }
    }
    return 0;
}

//gestión de las variables de entorno con diferentes opciones
int manageEnviron(char * args[], int option){
    char **env_aux;
    switch(option){
        // Caso 'environ': imprimimos las variables de entorno junto con sus valores
        case 0: 
            for(env_aux = environ; *env_aux != 0; env_aux ++){
                printf("%s\n", *env_aux);
            }
            break;
        // Caso 'setenv': establecemos una variable de entorno con un valor
        case 1: 
            if((args[1] == NULL) && args[2] == NULL){
                printf("%s","No hay suficientes argumentos de entrada\n");
                return -1;
            }
            
            // Utilizamos diferentes mensajes para variables nuevas y sobrescritas
            if(getenv(args[1]) != NULL){
                printf("%s", "La variable ha sido sobrescrita\n");
            }else{
                printf("%s", "La variable ha sido creada\n");
            }
            
            // Si no especificamos un valor para la variable, la establecemos como ""
            if (args[2] == NULL){
                setenv(args[1], "", 1);
            // Establecemos la variable con el valor dado
            }else{
                setenv(args[1], args[2], 1);
            }
            break;
        // Caso 'unsetenv': eliminamos una variable de entorno
        case 2:
            if(args[1] == NULL){
                printf("%s","No hay suficientes argumentos de entrada\n");
                return -1;
            }
            if(getenv(args[1]) != NULL){
                unsetenv(args[1]);
                printf("%s", "La variable ha sido borrada\n");
            }else{
                printf("%s", "La variable no existe\n");
            }
        break;
    }
    return 0;
}

//Método para lanzar un programa. Se puede ejecutar en segundo plano.
//o en primer plano
void launchProg(char **args, int background){	 
     int err = -1;
     
     if((pid=fork())==-1){
         printf("No se pudo crear el proceso hijo\n");
         return;
     }
     // pid == 0 implica que el siguiente código está relacionado con el proceso hijo
    if(pid==0){
        // Configuramos que el hijo ignore las señales SIGINT (queremos que el proceso padre
        // las maneje con signalHandler_int)    
        signal(SIGINT, SIG_IGN);
        
        // Establecemos parent=<ruta>/simple-c-shell como una variable de entorno
        // para el hijo
        setenv("parent",getcwd(currentDirectory, 1024),1);    
        
        // Si lanzamos comandos que no existen, terminamos el proceso
        if (execvp(args[0],args)==err){
            printf("Comando no encontrado");
            kill(getpid(),SIGTERM);
        }
     }
     
     // Lo siguiente será ejecutado por el padre
     
     // Si el proceso no se solicita que se ejecute en segundo plano, esperamos
     // a que el hijo termine.
     if (background == 0){
         waitpid(pid,NULL,0);
     }else{
         printf("Proceso creado con PID: %d\n",pid);
     }     
}

// Gewstion de la redirección de E/S
  
void fileIO(char * args[], char* inputFile, char* outputFile, int option){
     
    int err = -1;
    
    int fileDescriptor; // entre 0 y 19, describiendo el archivo de salida o entrada
    
    if((pid=fork())==-1){
        printf("No se pudo crear el proceso hijo\n");
        return;
    }
    if(pid==0){
        // Opción 0: redirección de salida
        if (option == 0){
            // Abrimos (creamos) el archivo truncándolo en 0, solo para escritura
            fileDescriptor = open(outputFile, O_CREAT | O_TRUNC | O_WRONLY, 0600); 
            // Reemplazamos la salida estándar con el archivo apropiado
            dup2(fileDescriptor, STDOUT_FILENO); 
            close(fileDescriptor);
        // Opción 1: redirección de entrada y salida
        }else if (option == 1){
            // Abrimos el archivo solo para lectura (es STDIN)
            fileDescriptor = open(inputFile, O_RDONLY, 0600);  
            // Reemplazamos la entrada estándar con el archivo apropiado
            dup2(fileDescriptor, STDIN_FILENO);
            close(fileDescriptor);
            // Lo mismo que antes para el archivo de salida
            fileDescriptor = open(outputFile, O_CREAT | O_TRUNC | O_WRONLY, 0600);
            dup2(fileDescriptor, STDOUT_FILENO);
            close(fileDescriptor);       
        }
         
        setenv("parent",getcwd(currentDirectory, 1024),1);
        
        if (execvp(args[0],args)==err){
            printf("error");
            kill(getpid(),SIGTERM);
        }         
    }
    waitpid(pid,NULL,0);
}

//Gestion de las tuberías.

void pipeHandler(char * args[]){
    // Descriptores de archivos
    int filedes[2]; // pos. 0 salida, pos. 1 entrada de la tubería
    int filedes2[2];
    
    int num_cmds = 0;
    
    char *command[256];
    
    pid_t pid;
    
    int err = -1;
    int end = 0;
    
    int i = 0;
    int j = 0;
    int k = 0;
    int l = 0;
    
    // Primero calculamos el número de comandos (se separan
    // por '|')
    while (args[l] != NULL){
        if (strcmp(args[l],"|") == 0){
            num_cmds++;
        }
        l++;
    }
    num_cmds++;

    while (args[j] != NULL && end != 1){
        k = 0;
        // Usamos un array auxiliar de punteros para almacenar el comando
        // que se ejecutará en cada iteración
        while (strcmp(args[j],"|") != 0){
            command[k] = args[j];
            j++;    
            if (args[j] == NULL){
                end = 1;
                k++;
                break;
            }
            k++;
        }
        command[k] = NULL;
        j++;        
        
        if (i % 2 != 0){
            pipe(filedes);
        }else{
            pipe(filedes2); 
        }
        
        pid=fork();
        
        if(pid==-1){            
            if (i != num_cmds - 1){
                if (i % 2 != 0){
                    close(filedes[1]); 
                }else{
                    close(filedes2[1]); 
            } 
            }           
            printf("No se pudo crear el proceso hijo\n");
            return;
        }
        if(pid==0){
            // Si estamos en el primer comando
            if (i == 0){
                dup2(filedes2[1], STDOUT_FILENO);
            }
            // Si estamos en el último comando, dependiendo de si se
            // encuentra en una posición impar o par, reemplazaremos
            // la entrada estándar por una u otra tubería. La salida estándar
            // no se tocará porque queremos ver la salida en la terminal.
            else if (i == num_cmds - 1){
                if (num_cmds % 2 != 0){ // para número impar de comandos
                    dup2(filedes[0],STDIN_FILENO);
                }else{ // para número par de comandos
                    dup2(filedes2[0],STDIN_FILENO);
                }
            // Si estamos en un comando que está en el medio, tendremos
            // que usar dos tuberías, una para la entrada y otra para la salida.
            // La posición también es importante para elegir qué descriptor de archivo
            // corresponde a cada entrada/salida.
            }else{ // para i impar
                if (i % 2 != 0){
                    dup2(filedes2[0],STDIN_FILENO); 
                    dup2(filedes[1],STDOUT_FILENO);
                }else{ // para i par
                    dup2(filedes[0],STDIN_FILENO); 
                    dup2(filedes2[1],STDOUT_FILENO);                    
                } 
            }
            
            if (execvp(command[0],command)==err){
                kill(getpid(),SIGTERM);
            }       
        }
        
        // CERRANDO DESCRIPTORES EN EL PADRE
        if (i == 0){
            close(filedes2[1]);
        }
        else if (i == num_cmds - 1){
            if (num_cmds % 2 != 0){                    
                close(filedes[0]);
            }else{                    
                close(filedes2[0]);
            }
        }else{
            if (i % 2 != 0){                    
                close(filedes2[0]);
                close(filedes[1]);
            }else{                    
                close(filedes[0]);
                close(filedes2[1]);
            }
        }
                
        waitpid(pid,NULL,0);
                
        i++;    
    
    }
}

// manejo de los comandos introducidos mediante la entrada estándar
int commandHandler(char * args[]){
    int i = 0;
    int j = 0;
    
    int fileDescriptor;
    int standardOut;
    
    int aux;
    int background = 0;
    
    char *args_aux[256];
    
    // Buscamos los caracteres especiales y separamos el comando en sí
    // en un nuevo array para los argumentos
    while ( args[j] != NULL){
        if ( (strcmp(args[j],">") == 0) || (strcmp(args[j],"<") == 0) || (strcmp(args[j],"&") == 0)){
            break;
        }
        args_aux[j] = args[j];
        j++;
    }
    
    // El comando 'exit' cierra la shell
    if(strcmp(args[0],"exit") == 0) exit(0);
    // El comando 'pwd' imprime el directorio actual
    else if (strcmp(args[0],"pwd") == 0){
        if (args[j] != NULL){
            // Si queremos salida de archivo
            if ( (strcmp(args[j],">") == 0) && (args[j+1] != NULL) ){
                fileDescriptor = open(args[j+1], O_CREAT | O_TRUNC | O_WRONLY, 0600); 
                // Reemplazamos la salida estándar con el archivo apropiado
                standardOut = dup(STDOUT_FILENO); // primero hacemos una copia de stdout
                                                  // porque lo querremos de vuelta
                dup2(fileDescriptor, STDOUT_FILENO); 
                close(fileDescriptor);
                printf("%s\n", getcwd(currentDirectory, 1024));
                dup2(standardOut, STDOUT_FILENO);
            }
        }else{
            printf("%s\n", getcwd(currentDirectory, 1024));
        }
    } 
    // El comando 'clear' limpia la pantalla
    else if (strcmp(args[0],"clear") == 0) system("clear");
    // El comando 'cd' cambia de directorio
    else if (strcmp(args[0],"cd") == 0) changeDirectory(args);
    // El comando 'environ' lista las variables de entorno
    else if (strcmp(args[0],"environ") == 0){
        if (args[j] != NULL){
            // Si queremos salida de archivo
            if ( (strcmp(args[j],">") == 0) && (args[j+1] != NULL) ){
                fileDescriptor = open(args[j+1], O_CREAT | O_TRUNC | O_WRONLY, 0600); 
                // Reemplazamos la salida estándar con el archivo apropiado
                standardOut = dup(STDOUT_FILENO); // primero hacemos una copia de stdout
                                                  // porque lo querremos de vuelta
                dup2(fileDescriptor, STDOUT_FILENO); 
                close(fileDescriptor);
                manageEnviron(args,0);
                dup2(standardOut, STDOUT_FILENO);
            }
        }else{
            manageEnviron(args,0);
        }
    }
    // El comando 'setenv' establece variables de entorno
    else if (strcmp(args[0],"setenv") == 0) manageEnviron(args,1);
    // El comando 'unsetenv' borra variables de entorno
    else if (strcmp(args[0],"unsetenv") == 0) manageEnviron(args,2);
    else{
        // Si no se usaron ninguno de los comandos anteriores, invocamos
        // el programa especificado. Tenemos que detectar si se solicitó
        // redirección de E/S, ejecución con tuberías o ejecución en segundo plano
        while (args[i] != NULL && background == 0){
            // Si se solicitó ejecución en segundo plano (último argumento '&')
            // salimos del bucle
            if (strcmp(args[i],"&") == 0){
                background = 1;
            // Si se detecta '|', se solicitó tubería, y llamamos
            // al método apropiado que manejará las diferentes ejecuciones
            }else if (strcmp(args[i],"|") == 0){
                pipeHandler(args);
                return 1;
            // Si se detecta '<', tenemos redirección de entrada y salida.
            // Primero verificamos si la estructura dada es correcta,
            // y si es así, llamamos al método apropiado
            }else if (strcmp(args[i],"<") == 0){
                aux = i+1;
                if (args[aux] == NULL || args[aux+1] == NULL || args[aux+2] == NULL ){
                    printf("Argumentos de entrada insuficientes\n");
                    return -1;
                }else{
                    if (strcmp(args[aux+1],">") != 0){
                        printf("Uso: Se esperaba '>' y se encontró %s\n",args[aux+1]);
                        return -2;
                    }
                }
                fileIO(args_aux,args[i+1],args[i+3],1);
                return 1;
            }
            // Si se detecta '>', tenemos redirección de salida.
            // Primero verificamos si la estructura dada es correcta,
            // y si es así, llamamos al método apropiado
            else if (strcmp(args[i],">") == 0){
                if (args[i+1] == NULL){
                    printf("Argumentos de entrada insuficientes\n");
                    return -1;
                }
                fileIO(args_aux,NULL,args[i+1],0);
                return 1;
            }
            i++;
        }
        // Lanzamos el programa con nuestro método, indicando si queremos
        // ejecución en segundo plano o no
        args_aux[i] = NULL;
        launchProg(args_aux,background);
        
    }
    return 1;
}


int main(int argc, char *argv[], char ** envp) {
    char line[MAXLINE]; // buffer para la entrada del usuario
    char * tokens[LIMIT]; // array para los diferentes tokens en el comando
    int numTokens;
        
    no_reprint_prmpt = 0;  
    pid = -10; // inicializamos pid a un valor que no es posible

    init();

    
    // Establecemos nuestro extern char** environ al entorno, para que
    // podamos tratarlo más tarde en otros métodos
    environ = envp;
    
    // Establecemos shell=<ruta>/simple-c-shell como una variable de entorno para
  
    
    // Bucle principal, donde se leerá la entrada del usuario y se imprimirá el indicador
    // de comando
    while(TRUE){
        if (no_reprint_prmpt == 0) shellPrompt();
        no_reprint_prmpt = 0;
        
        memset ( line, '\0', MAXLINE );


        fgets(line, MAXLINE, stdin);

        if((tokens[0] = strtok(line," \n\t")) == NULL) continue;
        
        // Leemos todos los tokens de la entrada y los pasamos a nuestro
        // commandHandler como argumento
        numTokens = 1;
        while((tokens[numTokens] = strtok(NULL, " \n\t")) != NULL) numTokens++;
        
        commandHandler(tokens);
        
    }          

    exit(0);
}
