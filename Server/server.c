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
#include <sqlite3.h>

#define PORT 2912

extern int errno;

typedef struct thData
{
	int idThread; //id-ul thread-ului ;
	int cl;       //descritporul clientului pe care acest threat il va trata;
}thData;

static void *treat(void *); //functia executata de fiecare thread in lucrul cu clientii; aceasta este apelata la crearea threadului
void raspunde(void *);   //functia executata in cadrul threadului, care primeste, prelucreaza si trimite mesaje;
int i=0; //iteratorul threadurilor;

int client_list[100], client_nr;  //aceasta lista contine toti descriptorii clientilor care sunt conectati la server pentru a putea indeplini functia de "broadcast" a alertelor

void add_client(int newClient)
{
  client_list[client_nr++]=newClient;
}

void rm_cl_index(int index)           //elimina din lista descriptorul clientului de la un index specificat
{
  for(int i=index;i<client_nr-1;i++)
    client_list[i]=client_list[i+1];
  client_nr--;
}

void remove_client(int rmClient)      //Elimina din lista descriptorul clientului egal cu rmClient
{
  for(int i=0;i<client_nr;i++)
    if(client_list[i]==rmClient)
    {
      rm_cl_index(i);
      break;
    }
}


int main ()
{
  struct sockaddr_in server;	// structura folosita de server;
  struct sockaddr_in from;	
  int sd;		                  //descriptorul de socket pentru conexiunea cu clientul;
  pthread_t th[100];          //Identificatorii thread-urilor care se vor crea;
	                    

  if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1)  //cream socket-ul;
  {
    perror ("[server]Eroare la socket().\n");
    return errno;
  }
  
  int on=1;
  setsockopt(sd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
  
  bzero (&server, sizeof (server));
  bzero (&from, sizeof (from));
  
  server.sin_family = AF_INET;	
  server.sin_addr.s_addr = htonl (INADDR_ANY);  //acceptam conexiuni locale
  server.sin_port = htons (PORT);               //alocam port-ul;
  
  if (bind (sd, (struct sockaddr *) &server, sizeof (struct sockaddr)) == -1)   //facem legatura dintre socket ul sd si structura server
  {
    perror ("[server]Eroare la bind().\n");
    return errno;
  }

  if (listen (sd, 2) == -1)         //serverul asteapta conexiuni
  {
    perror ("[server]Eroare la listen().\n");
    return errno;
  }

  while (1)
  {
    int client;   //client va retine valoarea socket ului returnat de accept si va fi folosit pentru comunicarea cu clientul
    thData * td; //parametrul functiei *treat apelate atunci cand cream thread-ul;  
    int length = sizeof (from);
    printf ("[server]Asteptam la portul %d...\n",PORT);
    fflush (stdout);
    if ( (client = accept (sd, (struct sockaddr *) &from, &length)) < 0)
    {
      perror ("[server]Eroare la accept().\n");
      continue;
    }
    printf("[server]S-a conectat clientul la socket-ul %d\n",client);
    add_client(client);       //adaugam clientul in lista
    td=(struct thData*)malloc(sizeof(struct thData));	  //umplem structura de date a threadului 
    td->idThread=i;
    td->cl=client;
    pthread_create(&th[i], NULL, &treat, td);	      
    i++;
  } 
}	

static void *treat(void * arg)  //functia apelata atunci cand cream thread-ul;
{		
		struct thData tdL; 
		tdL= *((struct thData*)arg);	
		fflush (stdout);		 
		pthread_detach(pthread_self());	//separam threadul pentru ca acesta sa ruleze independent de altii;	
		raspunde((struct thData*)arg);  //apelam functia care schimba mesaje cu clientul;
    //odata iesiti din functia raspunde, putem incheia conexiunea cu acest client;
		printf("[server]Am terminat cu acest client, inchidem conexiunea\n\n");
    remove_client(tdL.cl);    //scoatem  clientul din lista
		close ((intptr_t)arg);
		return(NULL);		
}

int prelucrare_comanda(char msg[])  //aceasta functie detecteaza tipul comenzii primite de la client;
{
  char copie[100];
  strcpy(copie,msg);
  char *cuv = strtok(copie," ");
  if(strstr(cuv,"login"))
    return 0;
  else if(strstr(cuv,"auto"))
    return 1;
  else if(strstr(cuv,"raport"))
    return 2;
  else if(strstr(cuv,"quit"))
    return 3;
  else 
    return -1;
}

void broadcast(int threadId,char message[])   //aceasta functie este folosita pentru alerte, astfel incat daca un client a trimis o alerta atunci serverul o va trimite la toti ceilalti clienti;
{
  int size = strlen(message)+1;
  printf("[Thread %d]Facem broadcast tuturor clientilor cu mesajul: %s\n",threadId,message);
  int nr_clients_unreached=0;
  for(int i=0;i<client_nr;i++)
  {
    if (write (client_list[i], &size, 4) <= 0)
    {
      printf("[Thread %d] - client %d ",threadId,client_list[i]);
      perror ("[Thread]Eroare la write() dimens broadcast.\n");
      nr_clients_unreached++;
    }
    else if(write(client_list[i],message,size)<=0)
    {
      printf("[Thread %d] - client %d ",threadId,client_list[i]);
      perror ("[Thread]Eroare la write() broadcast.\n");
      nr_clients_unreached++;
    }
  }
  if(nr_clients_unreached==0)
    printf ("[Thread %d]Mesajul a fost broadcastat cu succes.\n\n",threadId);
  else
    printf ("[Thread %d]Mesajul NU a fost broadcastat cu succes. %d clienti nu au primit mesajul\n\n",threadId,nr_clients_unreached);
}

char raspuns_automat[500];    //aici vor fi introduse aspecte legate de un anumit drum 
char nume_drum[100];
int viteza_rec=90;            //viteza recomadata se va modifica in functie de drum si de conditiile acestuia

void cautareBD(char nume_drum[])
{
  sqlite3 *db;        //structura bazei de date
  char *err_msg = 0;
  sqlite3_stmt *res;  //statement
  
  int rc = sqlite3_open("tabele.db", &db);
  
  if (rc != SQLITE_OK) 
  {
    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db); 
  }
  
  char sql[50];   //sql va retine interogarea 
  strcpy(sql,"SELECT * FROM drum WHERE nume =");
  strcat(sql," \'");
  strcat(sql,nume_drum);
  strcat(sql,"\'");

  rc = sqlite3_prepare_v2(db, sql, -1, &res, 0);  //interogarea este compilata in byte-code si pusa in variabila res, care va reprezenta statement-ul
                                                  //parametrul al treilea retine dimensiunea interogarii iar valoarea -1 permite citirea interogarii pana la '\0'
  
  if (rc != SQLITE_OK) 
  {
      fprintf(stderr, "Failed to execute statement: %s\n", sqlite3_errmsg(db));
  }
  
  int step = sqlite3_step(res); //aceasta functie executa statement-ul pregatit de functia 

  if (step == SQLITE_ROW) 
  {
    // in aceasta secventa de cod verificam valorile primite de la baza de date si vom adauga in string-ul "raspuns_automat" mesaje pentru fiecare proprietate legata de drum din baza de date;
    strcpy(raspuns_automat,"\0");
    viteza_rec=90;          //valoarea variabilel viteza_rec se va modifica in functie de factorii din baza de date
    //parcurgem pe rand fiecare coloana din raspunsul bazei de date, reprezentand cate o proprietate a drumului din baza de date;
    //mesajul poate fi indentificat de string-ul care este concatenat in raspuns_automat
    for(int i=2;i<=12;i++)
    {
      if(i==2 && strcmp(sqlite3_column_text(res,i),"1")==0)
      {
        if(viteza_rec>50)
          viteza_rec = 50;
        strcat(raspuns_automat,"Portiune de drum in localitate| ");
      }
      if(i==3 && strcmp(sqlite3_column_text(res,i),"1")==0)
      {
        strcat(raspuns_automat,"Portiune de drum in afara localitatii| ");
      }
      if(i==4 && strcmp(sqlite3_column_text(res,i),"-")!=0)
      {
        if(viteza_rec>30)
          viteza_rec = 30;
        strcat(raspuns_automat,"Institutie de invatamant ");
        strcat(raspuns_automat,sqlite3_column_text(res,i));
        strcat(raspuns_automat,"| ");
      }
      if(i==5 && strcmp(sqlite3_column_text(res,i),"0")==0)
      {
        if(viteza_rec>30)
          viteza_rec = 30;
        printf("\n 5 succes\n");
        strcat(raspuns_automat,"Portiune de drum neasfaltata| ");
        printf("\n 5 succes\n");
      }
      if(i==6 && strcmp(sqlite3_column_text(res,i),"1")==0)
      {
        if(viteza_rec>20)
          viteza_rec = 20;        
        printf("\n 6 succes\n");
        strcat(raspuns_automat,"Portiune de drum offroad| ");
        printf("\n 6 succes\n");
      }
      if(i==7 && strcmp(sqlite3_column_text(res,i),"0")!=0)
      {
        printf("\n 7 succes\n");
        strcat(raspuns_automat,"Alerta - accident raportat| ");
        printf("\n 7 succes\n");
      }
      if(i==8 && strcmp(sqlite3_column_text(res,i),"0")!=0)
      {
        printf("\n 8 succes\n");
        strcat(raspuns_automat,"Alerta - politie raportata| ");
      }
      if(i==9 && strcmp(sqlite3_column_text(res,i),"0")!=0)
      {
        printf("\n 9 succes\n");
        strcat(raspuns_automat,"Alerta - camera de viteza raportata| ");
      }
      if(i==10 && strcmp(sqlite3_column_text(res,i),"0")!=0)
      {
        strcat(raspuns_automat,"Alerta - obstacol mic prezent pe carosabil| ");      
      }
      if(i==11 && strcmp(sqlite3_column_text(res,i),"0")!=0)
      {
        strcat(raspuns_automat,"Alerta - obstacol mare prezent pe carosabil| ");
      }
      if(i==12 && strcmp(sqlite3_column_text(res,i),"0")!=0)
      {
        strcat(raspuns_automat,"Alerta - ambuteiaj raporat| ");      
      }
    }
  } 
  sqlite3_finalize(res);
  sqlite3_close(db);
}

void raspunde(void *arg)
{
  while(1)
  {
    int msg_size;
    char msg[100];
    struct thData tdL; 
    tdL= *((struct thData*)arg);
    if (read (tdL.cl, &msg_size,4) <= 0)  //mai intai citim dimensiunea mesajului;
    {
      printf("[Thread %d]\n",tdL.idThread);
      perror ("Eroare la read() msg_size de la client");
      break;  //in cazul in care read returneaza -1 (eroare), sau returneaza 0 (semn ca clientul s-a deconectat), iesim din bucla;
    }
    if (read (tdL.cl, msg,msg_size) <= 0) //si apoi citim mesajul in sine;
    {
      printf("[Thread %d]\n",tdL.idThread);
      perror ("Eroare la read() msg de la client");
      break;
    }

    int msg_len = strlen(msg);
    printf ("[Thread %d]Mesajul a fost receptionat... %s\n",tdL.idThread, msg);
    char rasp[100];
    int comm = prelucrare_comanda(msg);
    if(comm == 0) //comanda login
    {
      printf("[thread %d]Comanda este login.\n",tdL.idThread);
      strcpy(msg,msg+6);
      printf("[thread %d]Comanda este login - %s\n",tdL.idThread,msg);
      char *username;
      if(strstr(msg,":")) //daca utilizatorul a specificat preferinte
      {
        username = strtok(msg,":"); //separam username-ul, cu ajutorul delimitatorului ':';
        strcpy(rasp,"Inregistrare cu succes. Username: ");
        strcat(rasp,username);
        strcat(rasp,". Preferinte: ");
        username = strtok(NULL,":");      //separam apoi preferintele utilizatorului;
        strcat(rasp,username);
        strcat(rasp,"\0");
      }
      else    //utilizatorul nu a specificat preferinte
      {
        strcpy(rasp,"Inregistrare cu succes. Username: ");
        strcat(rasp,msg);
        strcat(rasp,". Fara preferinte");
        strcat(rasp,"\0");
      }
      
    }
    else if(comm == 1)
    {
      printf("[thread %d]Comanda este auto.\n",tdL.idThread);
      strcpy(msg,msg+5);
      char *drum = strtok(msg,":"); //separam numele drumului;
      strcpy(nume_drum, drum);
      cautareBD(nume_drum);
      printf("[server]Proprietati drum: %s\n",raspuns_automat);
      strcpy(rasp,"Pe ");
      strcat(rasp, drum);
      strcat(rasp," viteza recomandata este: ");
      char vit[10];
      sprintf(vit,"%d",viteza_rec);
      strcat(rasp,vit);
      strcat(rasp,"km/h\n");
      strcat(rasp,raspuns_automat);
      strcat(rasp,"\0");
    }
    else if(comm == 2)
    {
      printf("[thread %d]Comanda este raport.\n",tdL.idThread);
      printf("[thread %d]Nr Clienti: %d\n",tdL.idThread,client_nr);
      strcpy(msg,msg+7);
      char *rap = strtok(msg,":");  //separam numele drumului
      strcpy(rasp,"ATENTIE! Pe ");
      strcat(rasp,rap);
      strcat(rasp," s-au raportat urmatoarele evenimente: ");
      rap = strtok(NULL,":");       //separam codurile rapoartelor (folosim aceeasi variabila "rap");
      strcat(rasp,rap);
      strcat(rasp,"\0");
      broadcast(tdL.idThread,rasp);
    }
    else if(comm == 3)              //in cazul in care am primit comanda quit, iesim din bucla;
    {
      close(tdL.cl);
      break;    
    }
    
    if(comm != 2)                 //cazul in care nu facem broadcast
    {
      int rasp_size = strlen(rasp)+1;
      printf("[Thread %d]Trimitem mesajul inapoi... %s\n",tdL.idThread, rasp);
                
      if (write (tdL.cl, &rasp_size, 4) <= 0)
      {
        printf("[Thread %d] ",tdL.idThread);
        perror ("[Thread]Eroare la write() rasp_size catre client.\n");
      }
      if(write(tdL.cl,rasp,rasp_size)<=0)
      {
        printf("[Thread %d] ",tdL.idThread);
        perror ("[Thread]Eroare la write() rasp catre client.\n");
      }
      else
        printf ("[Thread %d]Mesajul a fost trasmis cu succes.\n\n",tdL.idThread);	
    }
  }
}
