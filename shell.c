#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_INPUT 250
#define MAX_PATH 150
#define MAX_ARGV 50

// Protótipos
void handler(int);
int verifyBackground(int *, char *[MAX_ARGV]);
int verifyOutput(int*, char *[MAX_ARGV], char []);
int verifyPipe(int, char *[MAX_ARGV], char *[MAX_ARGV], char *[MAX_ARGV]);
void handleOutput(int *, char []);
void handlePipe(char *[MAX_ARGV], char *[MAX_ARGV]);
void handleCd(char [], char [], char []);

int main() {
  pid_t result, first, second;
  char *arg, input[MAX_INPUT], filename[MAX_INPUT], curDir[MAX_PATH], lastDir[MAX_PATH], *argv[MAX_ARGV], *argvFirst[MAX_ARGV], *argvSecond[MAX_ARGV];
  int k, code, fileDescriptor, status, argc, isBackground, isOutput, isPipe, pipefd[2];

  // Mostrar diretório atual no shell
  if (getcwd(curDir, sizeof(curDir)) == NULL) {
    perror("getcwd() error");
    return 1;
  }

  // Loop para executar comandos
  while (1) {
    argc = 0;
    isBackground = 0;
    isOutput = 0;
    isPipe = 0;

    // Ler input do usuário
    printf("\x1b[96mJvShell\x1b[0m:\x1b[95m%s\x1b[0m$ ", curDir);
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;

    // Separar o input em um array de argumentos
    arg = strtok(input, " ");
    while (arg != NULL && argc < (MAX_ARGV - 1)) {
      argv[argc++] = arg;
      arg = strtok(NULL, " ");
    }
    argv[argc] = NULL; 

    // Nenhum argumento informado
    if (argc == 0) {
      continue;
    }

    // Built in para sair do terminal
    if (strcmp(argv[0], "exit") == 0) {
      break;
    } 

    // Execução built in cd
    if (strcmp(argv[0], "cd") == 0) {
      handleCd(argv[1], curDir, lastDir);
      continue;
    }

    // Verificações
    isBackground = verifyBackground(&argc, argv);
    isOutput = verifyOutput(&argc, argv, filename);
    isPipe = verifyPipe(argc, argv, argvFirst, argvSecond);

    // Verifica inputs inválidos
    if ((isOutput && isBackground) || (isOutput && isPipe) || (isBackground && isPipe)) {
      printf("invalid input\n");
      continue;
    }

    // Execução do input com pipe
    if (isPipe) {
      handlePipe(argvFirst, argvSecond);
      continue;
    }

    // Criar processo filho
    result = fork();
    if (result == -1) {
      perror("fork() error");
      exit(1);
    }

    if (result == 0) {
      // Execução do input com saída em um arquivo
      if (isOutput) {
        handleOutput(&fileDescriptor, filename);
      }

      // Execução do input do usuário
      code = execvp(argv[0], argv);
      if (code == -1) {
        perror("execvp() error");
        exit(1);
      }
    } else {
      // Execução do input em segundo plano
      if (isBackground) {
        signal(SIGCHLD, handler);
      // Execução do input em primeiro plano
      } else {
        wait(&status);
      }
    }
  }

  return 0;
}

// Função para tratar os processos filhos encerrados
void handler(int signal) {
  int status;
  while (waitpid(0, &status, WNOHANG) > 0);
}

// Verifica se é para executar em background
int verifyBackground(int *argc, char *argv[MAX_ARGV]) {
  int isBackground = 0;

  if (*argc > 1 && strcmp(argv[*argc - 1], "&") == 0) {
    isBackground = 1;
    *argc -= 1;
    argv[*argc] = NULL;
  }

  return isBackground;
}

// Verifica se é para redirecionar a saída para um arquivo
int verifyOutput(int *argc, char *argv[MAX_ARGV], char filename[]) {
  int isOutput = 0;

  for (int i = 0; i < *argc; i++) {
    if (strcmp(argv[i], ">") == 0) {
      isOutput = 1;
      strcpy(filename, argv[*argc - 1]);
      *argc -= 2;
      argv[*argc] = NULL;
      break;
    }
  }

  return isOutput;
}

// Verifica se é para utilizar o mecanismo de pipe
int verifyPipe(int argc, char *argv[MAX_ARGV], char *argvFirst[MAX_ARGV], char *argvSecond[MAX_ARGV]) {
  int k, isPipe = 0;

  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "|") == 0) {
      isPipe = 1;
      for (int j = 0; j < i; j++) {
        argvFirst[j] = argv[j];
      }
      argvFirst[i] = NULL;
      k = 0;
      for (int j = i + 1; j < argc; j++) {
        argvSecond[k] = argv[j];
        k++;
      }
      argvSecond[k] = NULL;
      break;
    }
  }

  return isPipe;
}

// Executa o output
void handleOutput(int *fileDescriptor, char filename[]) {
  *fileDescriptor = open(filename, O_RDWR | O_CREAT, 0666);
  if (*fileDescriptor == -1) {
    perror("open() error");
    exit(1);
  }
  dup2(*fileDescriptor, STDOUT_FILENO);
  close(*fileDescriptor);
}

// Executa a pipe
void handlePipe(char *argvFirst[MAX_ARGV], char *argvSecond[MAX_ARGV]) {
  pid_t first, second;
  int status, code, pipefd[2];
  
  code = pipe(pipefd);
  if (code == -1) {
    perror("pipe() error");
    exit(1);
  }

  first = fork();
  if (first == -1) {
    perror("fork() error");
    exit(1);
  }

  if (first == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
    execvp(argvFirst[0], argvFirst);
  } else {
    second = fork();
    if (second == -1) {
      perror("fork() error");
      exit(1);
    }

    if (second == 0) {
      close(pipefd[1]);
      dup2(pipefd[0], STDIN_FILENO);
      close(pipefd[0]);
      execvp(argvSecond[0], argvSecond);
    } else {
      close(pipefd[0]);
      close(pipefd[1]);
      waitpid(first, &status, 0);
      waitpid(second, &status, 0);
    }
  }
}

// Built in para comando cd
void handleCd(char arg[], char curDir[], char lastDir[]) {
  int code;
  char path[MAX_PATH];

  if (arg == NULL || (strcmp(arg, "~") == 0)) {
    strcpy(path, getenv("HOME"));
  } else if (strcmp(arg, "-") == 0) {
    if (strcmp(lastDir, "\0") == 0) {
      printf("cd - failed, there is no previous directory\n");
      return;
    }
    strcpy(path, lastDir);
  } else {
    strcpy(path, arg);
  }

  code = chdir(path);
  if (code == -1) {
    perror("chdir() error");
  } else {
    strcpy(lastDir, curDir);
    getcwd(curDir, MAX_PATH);
  }
}