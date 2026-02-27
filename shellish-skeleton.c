#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h> // for open() and flags and stuff
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
       		else if ((strcmp(command->args[i], "-f") == 0 || strcmp(command->args[i], "--fields") == 0) && command->args[i+1] != NULL) { //-f case - same logic as -d
           		fields= command->args[i+1];
            		i++; //same logic once again
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
		fields[num_fields++] = atoi(tok); //yeah, this one's painful - was warned quite nicely :/
		tok = strtok(NULL; ",");
	} // we're done, yay!
	
	char line[4096]; //lots and lots just in case
	while (fgets(line, sizeof(line), stdin) != NULL) {

	char *parts[1024];
        int num_parts = 0;
        char d[2] = { delim, '\0' }; //either delim or terminate string 

        char *p = strtok(line, d);
        while (p != NULL && num_parts < 1024) {
            parts[num_parts++] = p;
            p = strtok(NULL, d);
        }

        for (int k = 0; k < num_fields; k++) {
            int l = fields[k];    
            if (k > 0) putchar(delim);
            if (l >= 1 && l <= num_parts) {
                fputs(parts[l - 1], stdout);
            } 
	    else {
                fprint("Outta range");
            }
        }
        putchar('\n');
    }

    return 0;

	


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
	  int piperw[2]; //determines read (0) or write (1) end of pipe
	  if (pipe(piperw) == -1) return UNKNOWN; //failed to create pipe case
	  pid_t left = fork();
	  if (left == 0) { //child case - the one to be read
		  close(piperw[0]);
		  dup2(piperw[1], STDOUT_FILENO); 
		  close(piperw[1]); //only need stdout now so close
		  
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
	    		close(ioflag); //i give up from shifting, sorry for bad readability
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
    pid_t right = fork(); //next child aka command->next one
    	if (right == 0) {
		dup2(piperw[0], STDIN_FILENO); //set stdin here
		close(piperw[0]);
		close(piperw[1]); //only care about stdin

		 //part 2
    int ioflag;
    if (command->next->redirects[0] != NULL) { //i/o case 1: stdin
	    ioflag = open(command->next->redirects[0], O_RDONLY); //read permission + address provided)
	    dup2(ioflag, 0); //replace stdin
	    close(ioflag);
    }
    if (command->next->redirects[1] != NULL) { //i/o case 2: stdout w/ truncate 
	    ioflag = open(command->next->redirects[1], O_WRONLY | O_CREAT | O_TRUNC, 0644); //ready to write into given file
	    dup2(ioflag, 1); //replace stdout
	    close(ioflag);
    }
    if (command->next->redirects[2] != NULL) { //i/o case 3: stdout w/ append
	    ioflag = open(command->next->redirects[2], O_WRONLY | O_CREAT | O_APPEND, 0644); //same as #2 except append/truncate thingy
	    dup2(ioflag, 1);
	    close(ioflag);
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
	    strcat(moreDir, command->next->name);
	    moreDir = strdup(moreDir); //i hope this doesnt crash mate
	    if (access(moreDir, X_OK) == 0) {execv(moreDir, command->next->args);} //find it? exec it. if not, will come back anyway
	    dir = strtok(NULL, ":"); //go to next dir for iteration
    }
    printf("-%s: %s: command not found\n", sysname, command->next->name);
    free(copyPath); //mem leak countermeasure
    exit(127);
	}

	close(piperw[0]);
	close(piperw[1]); //parent code doesn't need these
	
    if(command->background) { //'&' arg passed case aka bg case
            return SUCCESS; //don't wait, return immediately
    }
    waitpid(left, NULL, 0);
   waitpid(right, NULL, 0);  // wait for child processes to finish
    return SUCCESS;




  }
  else {
  pid_t pid = fork();
  if (pid == 0) // child
  {

	/// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // TODO: do your own exec with path resolving using execv()
    // do so by replacing the execvp call below
    

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
  } else {
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
