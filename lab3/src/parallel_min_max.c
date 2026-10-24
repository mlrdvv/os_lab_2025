#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <getopt.h>

#include "find_min_max.h"
#include "utils.h"

int main(int argc, char **argv) {
  int seed = -1;
  int array_size = -1;
  int pnum = -1;
  bool with_files = false;

  // разбор аргументов
  while (true) {

    static struct option options[] = {
        {"seed", required_argument, 0, 0},
        {"array_size", required_argument, 0, 0},
        {"pnum", required_argument, 0, 0},
        {"by_files", no_argument, 0, 'f'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    int c = getopt_long(argc, argv, "f", options, &option_index);

    if (c == -1) break;

    switch (c) {
      case 0:
        switch (option_index) {
          case 0:
            seed = atoi(optarg);
            if (seed <= 0) {
              printf("seed must be positive\n");
              return 1;
            }
            break;
          case 1:
            array_size = atoi(optarg);
            if (array_size <= 0) {
              printf("array_size must be positive\n");
              return 1;
            }
            break;
          case 2:
            pnum = atoi(optarg);
            if (pnum <= 0) {
              printf("pnum must be positive\n");
              return 1;
            }
            break;
          case 3:
            with_files = true;
            break;
          default:
            printf("Index %d is out of options\n", option_index);
        }
        break;
      case 'f':
        with_files = true;
        break;
      case '?':
        break;
      default:
        printf("getopt returned character code 0%o?\n", c);
    }
  }

  if (seed == -1 || array_size == -1 || pnum == -1) {
    printf("Usage: %s --seed \"num\" --array_size \"num\" --pnum \"num\" [--by_files]\n", argv[0]);
    return 1;
  }

  if (pnum > array_size) {
    printf("pnum cannot be greater than array_size\n");
    return 1;
  }

  //генерация массива
  int *array = malloc(sizeof(int) * array_size);
  GenerateArray(array, array_size, seed);
  
  //двумерный массив для хранения pipeов (если with_files = false)
  int pipes[pnum][2];
  
  //если используем файлы, заранее подготовим имена
  char filenames[pnum][64];

  //создание IPC
  if (!with_files) {
    for (int i = 0; i < pnum; i++) {
      if (pipe(pipes[i]) == -1) {
        perror("pipe failed");
        return 1;
      }
    }
  } else {
    for (int i = 0; i < pnum; i++) {
      sprintf(filenames[i], "temp_result_%d.txt", i);
    }
  }

  int active_child_processes = 0;
  struct timeval start_time;
  gettimeofday(&start_time, NULL);

  //запуск и создание дочерних процессов
  for (int i = 0; i < pnum; i++) {
    pid_t child_pid = fork();
    
    if (child_pid >= 0) {
      active_child_processes += 1;
      
      if (child_pid == 0) {
        //дочерний процесс
        
        //разбиваем массив на pnum частей
        unsigned int begin = i * array_size / pnum;
        unsigned int end = (i + 1) * array_size / pnum;
        
        //ищем min/max на своём куске
        struct MinMax mm = GetMinMax(array, begin, end);
        
        if (with_files) {
          //отправка через файл
          FILE *f = fopen(filenames[i], "w");
          if (f == NULL) {
            perror("fopen failed");
            exit(1);
          }
          fprintf(f, "%d %d\n", mm.min, mm.max);
          fclose(f);
        } else {
          //отправка через pipe
          for (int j = 0; j < pnum; j++) {
            close(pipes[j][0]);          // чтение в дочернем не нужно
            if (j != i) close(pipes[j][1]); // чужие write не нужны
          }
          
          write(pipes[i][1], &mm, sizeof(struct MinMax));
          close(pipes[i][1]);
        }
        
        free(array);
        exit(0);
      }
    } else {
      printf("Fork failed!\n");
      return 1;
    }
  }

  //ожидание завершения дочерних процессов
  while (active_child_processes > 0) {
    int status;
    wait(&status);
    active_child_processes -= 1;
  }

  //сбор результатов
  struct MinMax min_max;
  min_max.min = INT_MAX;
  min_max.max = INT_MIN;

  if (!with_files) {
    //закрываем write-концы в родителе
    for (int i = 0; i < pnum; i++) {
      close(pipes[i][1]);
    }
  }

  for (int i = 0; i < pnum; i++) {
    struct MinMax child_mm;
    
    if (with_files) {
      //чтение из файла
      FILE *f = fopen(filenames[i], "r");
      if (f == NULL) {
        perror("fopen failed (read)");
        continue;
      }
      if (fscanf(f, "%d %d", &child_mm.min, &child_mm.max) != 2) {
        fprintf(stderr, "Error reading from %s\n", filenames[i]);
        fclose(f);
        continue;
      }
      fclose(f);
      unlink(filenames[i]);  // удаляем временный файл
    } else {
      //чтение из pipe
      read(pipes[i][0], &child_mm, sizeof(struct MinMax));
      close(pipes[i][0]);
    }

    // обновляем общий min/max
    if (child_mm.min < min_max.min) min_max.min = child_mm.min;
    if (child_mm.max > min_max.max) min_max.max = child_mm.max;
  }

  // вывод результата
  struct timeval finish_time;
  gettimeofday(&finish_time, NULL);

  double elapsed_time = (finish_time.tv_sec - start_time.tv_sec) * 1000.0;
  elapsed_time += (finish_time.tv_usec - start_time.tv_usec) / 1000.0;

  free(array);

  printf("Min: %d\n", min_max.min);
  printf("Max: %d\n", min_max.max);
  printf("Elapsed time: %fms\n", elapsed_time);
  fflush(NULL);
  
  return 0;
}