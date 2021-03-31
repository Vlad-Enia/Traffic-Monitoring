#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sqlite3.h>

extern int errno;
int port;

int randomIndex() 
{
  srand(time(0));
  return rand()%8+1; ///%numarul de strazi-1 +1 ca sa nu dea 0
}

char nume_drum[100];

void pseudoLocalizare()
{
  int index_drum = randomIndex();
  sqlite3 *db;
  char *err_msg = 0;
  sqlite3_stmt *res;
  int rc = sqlite3_open("tabele.db", &db);
  
  if (rc != SQLITE_OK) 
  {
      
    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db); 
  }
  
  char *sql = "SELECT nume FROM drum WHERE id = ?";
      
  rc = sqlite3_prepare_v2(db, sql, -1, &res, 0);
  
  if (rc == SQLITE_OK) 
  {
      printf("\n index: %d\n",index_drum);
      sqlite3_bind_int(res, 1, index_drum);
  } 
  else 
  {
      fprintf(stderr, "Failed to execute statement: %s\n", sqlite3_errmsg(db));
  }
  
  int step = sqlite3_step(res);
  if (step == SQLITE_ROW) 
  {
    strcpy(nume_drum,sqlite3_column_text(res, 0));   
  } 

  sqlite3_finalize(res);
  sqlite3_close(db);
  
}

int main (int argc, char *argv[])
{
  int sd;			// descriptorul de socket
  struct sockaddr_in server;	// structura folosita pentru conectare 
  if (argc != 3)
    {
      printf ("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
      return -1;
    }

  port = atoi (argv[2]);

  if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      perror ("Eroare la socket().\n");
      return errno;
    }

  server.sin_family = AF_INET;
  server.sin_addr.s_addr = inet_addr(argv[1]);
  server.sin_port = htons (port);
  
  if (connect (sd, (struct sockaddr *) &server,sizeof (struct sockaddr)) == -1) //conectare la server;
    {
      perror ("[client]Eroare la connect().\n");
      return errno;
    }

  char loc[100] = "auto Bv Independentei:50"; //trimitem, pentru functia de recomandare viteza, o comanda prestabilita;
  int loc_size = strlen(loc)+1;
  char rasp[50];                               //raspunsul de la server pt toate comenzile;
  int rasp_size;
  char input[100];                             //comenzile cititie de la tastatura (login, raport si quit);
  int input_size,input_len;
  pid_t pid;
  int countdown_halving=1;
  switch(pid = fork())
  {
    case -1:
        perror("[client]Eroare la fork()\n");
        return errno;
    case 0:     //copilul este cel care se ocupa cu trimiterea mesajelor spre server
        
        while(1)
        {
            //vom utiliza select pentru a monitoriza stdin
            //daca in intervalul de 10 secunde nu s-a scris nimic in stdin, atunci 
            fd_set stdinput;
            FD_ZERO(&stdinput);     //initializam set-ul stdinput
            FD_SET(0, &stdinput);   //adaugam in stdinput 0 pt stdin

            struct timeval countdown;
            
            countdown.tv_sec = 10/countdown_halving;  //asteptam 10 secunde input de la tastatura. Variabila countdown_halving exista pentru a injumatati timpul de asteptare
                                                      //pentru mesajul automat dupa citirea unui input de la tastatura; acest lucru se intampla pentru ca informatiile care vin 
                                                      //automat de la server sa ajunga dupa un timp mai scurt;
            countdown.tv_usec = 0;

            int return_select = select(1, &stdinput, NULL, NULL, &countdown);
            pseudoLocalizare();
            if (return_select == -1)
            {
                perror("[client] Eroare la select()\n");
                return errno;
            }
            else if (return_select) //in cazul asta putem citi ceva din stdin
            {
              countdown_halving=2;
              fgets(input, sizeof(input), stdin);

              input_len = strlen(input);      //eliminam caracterul newline;
              if (input[input_len-1] == '\n')
              {
                  input[input_len-1] = '\0';
                  input_len--;
              }
              if(strstr(input,"raport"))
              {
                char *cuvant = strtok(input," ");
                strcpy(loc,cuvant);
                strcat(loc," ");
                strcat(loc,nume_drum);
                strcat(loc,":");
                cuvant = strtok(NULL,":");
                strcat(loc,cuvant);
                printf("\n alerta %s \n",loc);
              }
              else if(strstr(input,"quit"))
              {
                kill(pid,SIGTERM);      //in cazul in care comanda este quit, mai intai am trimis serverului comanda, pentru ca acesta sa sisteze conexinea
                shutdown(sd,SHUT_RDWR);
                break;                  //apoi oprim executia copiluli si iesim din bucla;
              }
            }
            else
            { 
              
              countdown_halving=1;
              strcpy(loc,"auto ");
              strcat(loc,nume_drum);
              strcat(loc,":50");
               //aici se trimite mesajul automat
            }
            loc_size = strlen(loc)+1;
            if(write(sd,&loc_size,4)<=0)
                perror("[client] Eroare la write() msg_size\n");

            if(write(sd,loc,loc_size)<=0)
                perror("[client] Eroare la write() msg\n");

        }

    default:    //parintele este cel care se ocupa cu primirea mesajelor de la server
        while(1)
        {
        
            if(read(sd,&rasp_size,4)<0)    
            {
                perror ("[client child] Eroare la read() loc_size de la server.\n");
                return errno;
            }
            if(read(sd,rasp,rasp_size)<0)
            {
                perror ("[client child] Eroare la read() loc de la server.\n");
                return errno;
            }
            printf ("\n[client child] %s\n", rasp);
        }
  }
}