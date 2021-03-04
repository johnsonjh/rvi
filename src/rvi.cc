// ---------------- 
// rvi - Revision Control System Interface <mtsegaye-fm@rucus.ru.ac.za>
//       v1.0.2 25/09/2000
//
// NB. Correct the program paths below for your particular system.
//
// Freeware. Use/modify as you wish.
//
// ----------------

#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define RCSDIR "RCS/\0"
#define RCSEXT ",v\0"

#define UNLOCK "/usr/bin/rcs -u -q \0" 
#define CHECK_OUT "/usr/bin/co -l -q \0"
#define CHECK_OUT_R "/usr/bin/co -q \0"
#define CHECK_IN  "/usr/bin/ci -u -q \0"
 
#define EDITOR "/usr/bin/vi\0"  // guy :)

#define MAX_BUFFER_SZ 255

#define REEDIT 2
#define ABORT  4
#define DG_YES 6
#define DG_NO  8
#define DG_OTHER 0

//#define DEBUG

//protos
void handle_error(int);
void do_filecheck(char*);
int do_filechange_check(char*,struct stat *);
char * exec_cmd(char**,int,int flag=0);
int dialog();

int main(int argc,char ** argv)
{ printf("Revision Control System Interface (rvi) <mtsegaye-fm@rucus.ru.ac.za>\n");
  if (argc<2)
    { printf("Usage : rvi <filename> [editor]\n");
      exit(0);
    }
  
  // check that
  // 1) the file isn't already locked out
  // 2) check/create /RCS exists, if it doesn't create it w/user's consent 
  do_filecheck(argv[1]);
 
  // save the uid and gid of the file being opened
  struct stat fileinfo;
  stat(argv[1] , &fileinfo);
  
  #ifdef DEBUG
   printf("UID: %d EUID:%d GID:%d EGID:%d\n",
    fileinfo.st_uid,getenv("UID"),fileinfo.st_gid,getenv("GID"));
  #endif

  // get editor  
  char * editor;
  if (argc==3)  
     editor = argv[2]; // from the cmd line
  else if(argc<3) 
     editor = getenv("EDITOR");  // or the environment 
    
  if (!editor) editor = EDITOR;

  // multiple editing
  
  // go ahead and check the file out (co -l filename
    { char * params[4];
      params[0] = CHECK_OUT;
      params[1] = argv[1];
      exec_cmd(params,2);
      // save modification time
      struct stat tmp;
      stat(argv[1],&tmp);
      fileinfo.st_mtime = tmp.st_mtime; 
    } 

   // multiple editing 
   do {
       // spawn editor
       char * params[3];
       params[0] = editor;
       params[1] = " \0";
       params[2] = argv[1]; 
       exec_cmd(params,3);
    
       // ask user if he/she wants to keep the changes he made
     } while (do_filechange_check(argv[1],&fileinfo)==REEDIT);
     

  // check in file into RCS and check out a read_only copy  
  char * params[2];
  params[0] = CHECK_IN;
  params[1] = argv[1];
  exec_cmd(params,2);
  
  //restore the uid_gid of the file 
  chown(argv[1], fileinfo.st_uid, fileinfo.st_gid);
     
  return 0;   
} 


// function to check for existance of filename, proper user permissions and
// locked out files
void do_filecheck(char * filename)
{ 
  // extract path to file
  char * rcspath; 
  int fname_p; int str_ln=strlen(filename)-1;
  for(fname_p=str_ln;fname_p>0 && filename[fname_p]!='/';fname_p--);
  if(fname_p<1 && filename[0]=='/')
    { rcspath ="/\0"; fname_p++;}
  else if(fname_p<1) rcspath = "./\0";
  else 
  { rcspath = new char[fname_p+1];
    strncpy(rcspath,filename,++fname_p);
  }  

  struct stat *filecheck = new struct stat;
  memset(filecheck,-1,sizeof(struct stat));
  stat(rcspath,filecheck);
   // check that user has write access to current dir
   if(!((filecheck->st_mode - S_IFDIR | S_IWUSR)==filecheck->st_mode - S_IFDIR))
      { printf("You don't have write access to %s : Bailing out\n",rcspath);
        exit(0);
      }
   
   // check if RCS exits
   int rcsdir_exists=0, rcsfile_exists=0;
   filecheck->st_size = -1;
   memset(filecheck,-1,sizeof(struct stat));
   char * FULLRCSPATH;
   { char * params[2];
     params[0] = rcspath;
     params[1] = RCSDIR;
     FULLRCSPATH = exec_cmd(params,2,1);
   }
   stat(FULLRCSPATH,filecheck);
   if(S_ISDIR(filecheck->st_mode)) 
      { rcsdir_exists=1; 
        if (rcsdir_exists )
          { // check RCS/filename,v exists
            char *param[3];
            param[0] = FULLRCSPATH;
            param[1] = filename+fname_p; 
            param[2] = RCSEXT;
            //concat RCSDIR + filename + RCSEXT 
            memset(filecheck,-1,sizeof(struct stat));
            char * fname = exec_cmd(param,3,1);
            stat(fname,filecheck);
            if (S_ISREG(filecheck->st_mode)) rcsfile_exists=1;
          }         
      }
   
  // check if the file exists
  memset(filecheck,-1,sizeof(struct stat));
  stat(filename,filecheck);
  int file_exists=0;
  if (S_ISREG(filecheck->st_mode))  file_exists =1; 
  if( !file_exists && !rcsfile_exists ) // specified file doesn't exist
    {  printf("The file \"%s\" doesn't exist \n",filename);
       printf("Should I create a new file and check it into rcs [N] ");   
       if (dialog() != DG_YES) exit(0);    
       #ifndef DEBUG
       FILE * f = fopen(filename,"a"); 
       if (!f)
           { printf("Failed to create %s \n ERROR: ",filename);
             handle_error(-1);
           }
       else fclose(f); 
       if (!rcsdir_exists) mkdir(FULLRCSPATH,S_IRWXU | S_IRWXG | S_IRWXO);
       #endif
       // check file in first time
       char * params[2];
       params[0] = CHECK_IN;
       params[1] = filename;
       exec_cmd(params,2); 
    }
  else
  if( !rcsfile_exists && file_exists)
  {  printf("%s isn't being handled by RCS, check it into RCS for the first time[N] ",filename);
     if (dialog() != DG_YES) exit(0); 
     #ifndef DEBUG
     if (!rcsdir_exists) mkdir(FULLRCSPATH,S_IRWXU | S_IRWXG | S_IRWXO);
     #endif
     // check file in first time
      char * params[2];
      params[0] = CHECK_IN;
      params[1] = filename;
      exec_cmd(params,2);
  } 
  else if(file_exists & rcsfile_exists)
  // check if file has been checked out aready/Writable version exists
   { memset(filecheck,-1,sizeof(struct stat));
     stat(filename,filecheck);
     if ((filecheck->st_mode - S_IFREG | S_IWUSR)  == filecheck->st_mode -S_IFREG  ||
         (filecheck->st_mode - S_IFREG | S_IWGRP) == filecheck->st_mode - S_IFREG  ||
         (filecheck->st_mode - S_IFREG | S_IWOTH) == filecheck->st_mode - S_IFREG )
        { printf("%s has already been checked out \n",filename);
          printf("Writable version exists : Bailing out\n");
          exit(0);
        }
   }
  delete filecheck;
}

// function to check in size & time of a file
int do_filechange_check(char * fname,struct stat * original)
{ struct stat finfo;
  stat(fname,&finfo);
  if(finfo.st_mtime != original->st_mtime)
   { printf("You made changes to %s, (Accept \\ Re-edit \\ Discard) [A] ", fname);
     int key = dialog();
     if (key == ABORT)  
       {  // unlock file and exit  
           { char * params[2];
             params[0] = UNLOCK;
             params[1] = fname;
             exec_cmd(params,2);
           }
           remove(fname);
           // restore file (read-only)
           { char * params[2];
             params[0] = CHECK_OUT_R;
             params[1] = fname;
             exec_cmd(params,2);
           } 
           exit(0); 
       }
     else return key;
   }
  return DG_OTHER; 
}


// function to execute a command || copy list into a linear array
// command[0] == command
// command[1..n] == args
char * exec_cmd(char ** params,int len,int flag)
{ char * command = new char [MAX_BUFFER_SZ];
  int buf_ptr=0;
  for(int i=0;i<len;i++)
   { strncpy(command+buf_ptr,params[i],MAX_BUFFER_SZ-buf_ptr);
     buf_ptr += strlen(params[i]);
   } 
  #ifndef DEBUG
    if (flag) return command; 
    handle_error(system(command));
  #else
    printf("%s\n",command);
    if (flag) return command; 
  #endif
  delete [] command;
  return (char*) NULL; 
}

// um.. just in case
void handle_error(int err)
{ switch(err)
   {  case 127 : 
          printf("Error : execve call for /bin/sh failed\n"); 
          exit(1);
      case -1  : 
          printf("Error %s\n",sys_errlist[errno]);
          exit(1);
      default  : break;
   } 
}

// handles simple y/n dialog w/user
int dialog()
{  char response[10];
   fgets(response,10,stdin);
   response[0] = toupper(response[0]);
   switch(response[0])
     { case 'D' : return ABORT;
       case 'R' : return REEDIT;
       case 'Y' : return DG_YES;
       case 'N' : return DG_NO;
     } 
   return DG_OTHER;
}

