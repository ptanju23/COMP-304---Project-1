#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h> // for open() and flags and stuff
#include <sys/stat.h> //for named pipes
#include <dirent.h> //for chatroom dirs
#include <time.h>
#include <limits.h>
const char *sysname = "shellish";

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0) {
      struct command_t *c =
          (struct command_t *)malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = (char *)malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}

//part 3a: shellish-cut

int shellish_cut(struct command_t *command) {
	char delim = '\t'; //default setting
	char *fields = NULL; //ptr for the indice fields (1,3,10 given in pdf)
	
	for (int i=1; command->args[i] != NULL; i++) {
		if ((strcmp(command->args[i], "-d") == 0 || strcmp(command->args[i], "--delimiter") == 0) && command->args[i+1] != NULL) { //-d case - do we have -d arg & some more args afterwards to use as delim?
			delim = command->args[i+1][0];
			i++; //skips next since it's determined as the delim
		}
		else if ((strncmp(command->args[i], "-d", 2) == 0 && command->args[i][2] != '\0')){
			delim = command->args[i][2];
		} // dC case, where C = char
       		else if ((strcmp(command->args[i], "-f") == 0 || strcmp(command->args[i], "--fields") == 0) && command->args[i+1] != NULL) { //-f case - same logic as -d
           		fields= command->args[i+1];
            		i++; //same logic once again
       		}
		else if (strncmp(command->args[i], "-f", 2) == 0 && command->args[i][2] != '\0'){
			fields = &command->args[i][2];
		}
	}
	if (fields == NULL) return UNKNOWN; //if args are empty, return
	
	//now, we are parsing the fields to an array for easier handling (yay!)
	int fields_arr[128]; //positive integers
	int num_fields = 0; //index for looping

	char temp[256]; //since we're going to tokenize
	strncpy(temp, fields, sizeof(temp) - 1);
	temp[sizeof(temp) -1] = '\0'; //don't forget null terminator!!
	
	char *tok = strtok(temp, ","); //now the fun part
	while (tok != NULL && num_fields < 128) {
		fields_arr[num_fields++] = atoi(tok); //yeah, this one's painful - was warned quite nicely :/
		tok = strtok(NULL, ",");
	} // we're done, yay!
	
	char line[4096]; //lots and lots just in case
	while (fgets(line, sizeof(line), stdin) != NULL) {

	// Remove trailing newline - i forgor before...
	line[strcspn(line, "\n")] = '\0';

	// manual split - cause i need the empty parts which strtok cut out before... i had to replace the code -_-
	char *parts[1024];
	int num_parts = 0;

	char *start = line;
	char *cur = line;

	while (1) {
    	  if (*cur == delim || *cur == '\0') {
        	if (num_parts < 1024) {
            		parts[num_parts++] = start;   // start of this field
        	}

        	if (*cur == '\0') {
            		break; // end of line
        	}

        	*cur = '\0';      // terminate this field
        	cur++;            // move past delimiter
        	start = cur;      // next field starts here
        	continue;
    	  }
    	cur++;
	}	

        for (int k = 0; k < num_fields; k++) {
            int l = fields_arr[k];    
            if (k > 0) putchar(delim);
            if (l >= 1 && l <= num_parts) {
                fputs(parts[l - 1], stdout);
            } 
	    
        }
        putchar('\n');
    }

    return 0;

}

//part 3-b: shellish_chatroom

int shellish_chatroom(struct command_t *command) {

  if (command->arg_count < 3) {
    return UNKNOWN; //error msg if there arent enough args for chatroom [0], roomname [1] and username [2]
  }
  
  char *roomname = command->args[1];
  char *username = command->args[2];

  //dirs for both vars

  char roomDir[256];
  strcpy(roomDir,"/tmp/chatroom-");
  strcat(roomDir, roomname);
  char userDir[256];
  strcpy(userDir, roomDir);
  strcat(userDir, "/");
  strcat(userDir, username);

  mkdir(roomDir, 0777); //rwx perms
  mkfifo(userDir, 0666); //rw- perms (since fifo)

  printf("Welcome to %s!\n", roomname);

  pid_t pid = fork();

  if (pid == 0) { //child - responsible for cont read
    int readFile = open(userDir, O_RDWR);
    if (readFile < 0) return UNKNOWN; //couldn't open read, error return

    char buf[1024]; //for reading
    while (1) { //for cont read
      ssize_t received = read(readFile, buf, sizeof(buf));
      if (received > 0) { //aka there's sth to write
	      write(1, "\n", 1);
      	      write(1, buf, received);
	      printf("[%s] %s > ", roomname, username);
	      fflush(stdout);
      }
    }
    close(readFile); //shouldn't be able to reach here unless problem encountered
    return EXIT;
  }

  else { //parent - responsible for write

    while (1) {
      char line[256];
      char msg[1024];

      printf("[%s] %s > ", roomname, username);
      fflush(stdout); //in case it refuses to print (-_-)

      if (fgets(line, sizeof(line), stdin) == NULL) break; //fgets failed -> escape loop (nothing to write to)
      
      strcpy(msg, "[");
      strcat(msg, roomname);
      strcat(msg, "] ");
      strcat(msg, username);
      strcat(msg, ": ");
      strcat(msg, line);

      DIR *dir = opendir(roomDir);
      if (dir == NULL) { perror("opendir"); continue; }

      while (1) { //lops every user to send the write msg to every one
        struct dirent *ptr = readdir(dir); //pointer
	if (ptr == NULL) break;

        //skip cases 
        if (strcmp(ptr->d_name, ".") == 0) continue; 
        if (strcmp(ptr->d_name, "..") == 0) continue;
        if (strcmp(ptr->d_name, username) == 0) continue;

        char other_user[512];

        strcpy(other_user, roomDir);
        strcat(other_user, "/");
        strcat(other_user, ptr->d_name);

        pid_t pid = fork();
        if (pid == 0) { //child case
            int writeFile = open(other_user, O_WRONLY | O_NONBLOCK); //can sometimes block so needs NONBLOCK too
            if (writeFile >= 0) { //write to all users case
                write(writeFile, msg, strlen(msg));
                close(writeFile);
            }
            return EXIT;
        }
    }

    closedir(dir);

    //zombie prevention (no apocalypse in this house)
    while (waitpid(-1, NULL, WNOHANG) > 0) { }
    }
  }
}

//part 3-c -> trash. makes a trash dir, places trash files by command, allows restoration (limited)

//helper 1: makes the dir if doesn't exist
static int make_trash_dir(char *trashDir, size_t size) {
  const char *home = getenv("HOME");
  if (home == NULL) return -1; //(spiderman) no home (or stg)
  if (snprintf(trashDir, size, "%s/.shellish_trash", home) >= (int) size) return -1; 

  struct stat st;
  if (stat(trashDir, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
  return mkdir(trashDir, 0700);
}

//helper 2: finds base name (just the last part) since we need that for moving the file around dirs
static const char *base_name(const char *path) {
  const char *str = strrchr(path, '/');
  return str ? (str + 1) : path;
}

//actual func
int shellish_trash(struct command_t *command) {
  char trashDir[PATH_MAX];
  if (make_trash_dir(trashDir, sizeof(trashDir)) == -1) {return UNKNOWN;}

  if (!command->args[1]) {return UNKNOWN;} //nothing entered after trash case

  // case 1: trash ls: iterates over every entry and stores them
  if (strcmp(command->args[1], "ls") == 0) {
    DIR *dir = opendir(trashDir);
    if (!dir) { return UNKNOWN; }

    struct dirent *dirptr;
    while ((dirptr = readdir(dir))) {
      if (!strcmp(dirptr->d_name, ".") || !strcmp(dirptr->d_name, "..")) continue; //skips over these since they're not real content
      printf("%s\n", dirptr->d_name);
    }
    closedir(dir);
    return SUCCESS; //case closed successfully!
  }

  // case 2: trash restore: if restore is entered as well, finds the newest entry to restore according to info entered
  if (strcmp(command->args[1], "restore") == 0) {
    const char *name = command->args[2];
    if (!name) {return UNKNOWN;}

    DIR *dir = opendir(trashDir);
    if (!dir) {return UNKNOWN; } //if can't opn dir case

    // Look for newest
    char newest[NAME_MAX + 1];
    newest[0] = '\0';
    long long best_tag = -1; //a long long tag ago, there lived a...

    char prefix[NAME_MAX + 1];
    snprintf(prefix, sizeof(prefix), "%s__", name); //so it isn't mixed with normal '_'
    size_t prefixlen = strlen(prefix);

    struct dirent *dirptr;
    while ((dirptr = readdir(dir))) {
      if (strncmp(dirptr->d_name, prefix, prefixlen) != 0) continue;

      // parse after "__"
      const char *ptr = dirptr->d_name + prefixlen; //for parsing
      char *end = NULL;
      long long tag = strtoll(ptr, &end, 10);
      if (end == ptr) continue; // no number

      if (tag > best_tag) { //find best fit
        best_tag = tag; 
        strncpy(newest, dirptr->d_name, sizeof(newest) - 1);
        newest[sizeof(newest) - 1] = '\0';
      }
    }

    closedir(dir);

    if (best_tag < 0) { //nothing found
      return UNKNOWN;
    }

    //renames it after current dir
    char from_address[PATH_MAX];
    if (snprintf(from_address, sizeof(from_address), "%s/%s", trashDir, newest) >= (int)sizeof(from_address)) {return UNKNOWN;}

    // restore into current dir
    if (rename(from_address, name) == -1) {return UNKNOWN;}

    return SUCCESS;
  }

  //case 3: move to trash (default)
  int temp = SUCCESS;
  for (int i = 1; command->args[i]; i++) {
    const char *src = command->args[i];
    const char *bn = base_name(src);

    char dst[PATH_MAX];
    snprintf(dst, sizeof(dst), "%s/%s__%lld_%d_%d",
             trashDir, bn, (long long)time(NULL), (int)getpid(), i);

    if (rename(src, dst) == -1) {temp = UNKNOWN;}
  }

  return temp;
}


int process_command(struct command_t *command) {
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[1]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }
  
  if (command-> next != NULL) {
    //renewed: now can handle multi-piping (not just two: left and right...)
	  int num_cmd = 0;
	  struct command_t *temp = command;
	  while (temp != NULL) {num_cmd++; temp = temp->next;} //iterate to get total pipe count
	  int (*piperw)[2] = malloc(sizeof(int[2]) * (num_cmd - 1)); //read&write keep for every pipe
	  if (piperw == NULL) return UNKNOWN; //mem coulnd't be alloced case

	  for (int i = 0; i < num_cmd - 1; i++) {
     	  if (pipe(piperw[i]) == -1) return UNKNOWN; //pipe init issue handle
	  }

    pid_t *childs = malloc(sizeof(pid_t) * num_cmd); //honestly forgot about this in the prev implementation...
    if (childs == NULL) return UNKNOWN; //no memory alloc case

    struct command_t *curr = command;

    for (int i = 0; i < num_cmd; i++) {

    childs[i] = fork();

    if (childs[i] == 0) {

        //pipe connect logic
        if (i > 0) { //not the first pipe, so takes input from prev one                
          dup2(piperw[i - 1][0], 0);
        }
        if (i < num_cmd - 1) { //not last pipe, so outputs to next pipe
            dup2(piperw[i][1], 1);
        }

        //iterate and close all
        for (int j = 0; j < num_cmd - 1; j++) {
            close(piperw[j][0]);
            close(piperw[j][1]);
        }

        //pipe rw logic
        int ioflag;
        if (curr->redirects[0] != NULL) {
            ioflag = open(curr->redirects[0], O_RDONLY);
            dup2(ioflag, 0);
            close(ioflag);
        }
        if (curr->redirects[1] != NULL) {
            ioflag = open(curr->redirects[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup2(ioflag, 1);
            close(ioflag);
        }
        if (curr->redirects[2] != NULL) {
            ioflag = open(curr->redirects[2], O_WRONLY | O_CREAT | O_APPEND, 0644);
            dup2(ioflag, 1);
            close(ioflag);
        }

        //part 3 - cut called here
        if (strcmp(curr->name, "cut") == 0) {
            exit(shellish_cut(curr));
        }

        if (strcmp(curr->name, "chatroom") == 0) {
          exit(shellish_chatroom(curr));
        }

        if (strcmp(curr->name, "trash") == 0) {
          exit(shellish_trash(curr));
        }

        //part 1 - pretty much same as no-pipe
        char *getPath = getenv("PATH");
        char *copyPath = strdup(getPath);
        char *dir = strtok(copyPath, ":");

        while (dir != NULL) {
            char *moreDir = strdup(dir);
            strcat(moreDir, "/");
            strcat(moreDir, curr->name);
            moreDir = strdup(moreDir);

            if (access(moreDir, X_OK) == 0) {
                execv(moreDir, curr->args);
            }
            dir = strtok(NULL, ":");
        }

        printf("-%s: %s: command not found\n", sysname, curr->name);
        free(copyPath);
        exit(127);
    }

    curr = curr->next;
  }


  }
  else {
  pid_t pid = fork();
  if (pid == 0) { // child

    //part 2
    int ioflag;
    if (command->redirects[0] != NULL) { //i/o case 1: stdin
	    ioflag = open(command->redirects[0], O_RDONLY); //read permission + address provided)
	    dup2(ioflag, 0); //replace stdin
	    close(ioflag);
    }
    if (command->redirects[1] != NULL) { //i/o case 2: stdout w/ truncate 
	    ioflag = open(command->redirects[1], O_WRONLY | O_CREAT | O_TRUNC, 0644); //ready to write into given file
	    dup2(ioflag, 1); //replace stdout
	    close(ioflag);
    }
    if (command->redirects[2] != NULL) { //i/o case 3: stdout w/ append
	    ioflag = open(command->redirects[2], O_WRONLY | O_CREAT | O_APPEND, 0644); //same as #2 except append/truncate thingy
	    dup2(ioflag, 1);
	    close(ioflag);
    }

    //part 3a
    if (strcmp(command->name, "cut") == 0) {
    exit(shellish_cut(command));
    }
    //3b
    if (strcmp(command->name, "chatroom") == 0) { 
      exit(shellish_chatroom(command));
    }

    if (strcmp(command->name, "trash") == 0) {
      exit(shellish_trash(command));
    }

    //part 1
    char *getPath = getenv("PATH"); //raw PATH, needs to be tokenized (:)
    char *copyPath = strdup(getPath); //tokenizer edit countermeasure
    char *dir = strtok(copyPath, ":"); //first dir from path string
    while (dir != NULL) {
	    //logic code
	    //get dir -> check for command (use access) -> if there execv, if not cont
	    char *moreDir = strdup(dir);
	    strcat (moreDir, "/"); //for dir/name to form path correctly
	    strcat(moreDir, command->name);
	    moreDir = strdup(moreDir); //i hope this doesnt crash mate
	    if (access(moreDir, X_OK) == 0) {execv(moreDir, command->args);} //find it? exec it. if not, will come back anyway
	    dir = strtok(NULL, ":"); //go to next dir for iteration
    }
    printf("-%s: %s: command not found\n", sysname, command->name);
    free(copyPath); //mem leak countermeasure
    exit(127);
    } 
    else {
    if(command->background) { //'&' arg passed case aka bg case
	    return SUCCESS; //don't wait, return immediately
    } 
    wait(0); // wait for child process to finish
    return SUCCESS;
    }
  }
}

int main() {
  while (1) {
    struct command_t *command =
        (struct command_t *)malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}
