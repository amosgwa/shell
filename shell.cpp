#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>

#include <readline/readline.h>
#include <readline/history.h>

#include "builtins.h"

// Potentially useful #includes (either here or in builtins.h):
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;

// The characters that readline will use to delimit words
const char* const WORD_DELIMITERS = " \t\n\"\\'`@><=;|&{(";

// An external reference to the execution environment
extern char** environ;

extern vector<string> event_list;

extern map<string, vector<string> > alias_list;

// Define 'command' as a type for built-in commands
typedef int (*command)(vector<string>&);

// A mapping of internal commands to their corresponding functions
map<string, command> builtins;

// Variables local to the shell
map<string, string> localvars;

int execute_line(vector<string>& tokens, map<string, command>& builtins);

// Handles external commands, redirects, and pipes.
int execute_external_command(vector<string> tokens) {
  // TODO: YOUR CODE GOES HERE
  // We will use execvp(const char *path, const char *arg)
  // First generate the char args including the first command.
  pid_t subprocess;
  pid_t childresult;

  int status;
  int exitCode = -1;

  if ((subprocess = fork()) == -1) {
    perror("fork failed");
    return exitCode;
  }

  if (subprocess == 0) {
    // child process.
    // Generate the arguments for execvp.

    if (commandType(tokens) == 1 || commandType(tokens) == 2) {
      // exitCode = pipes(tokens);
      exitCode = pipeLoop(tokens);

    } else {
      char** arg = (char**)malloc(sizeof(char*) * tokens.size() + 2);

      for (int i = 0; i < tokens.size(); i++) {
        arg[i] = convert(tokens[i]);
      }

      arg[tokens.size()] = NULL;

      // Execute the command
      exitCode = execvp(tokens[0].c_str(), arg);
      if (exitCode != 0) {
        perror("Execvp error ");
        _exit(-1);
      }
    }
  } else {
    // Parent process.
    // Get that status of the child.
    childresult = waitpid(subprocess, &status, WUNTRACED);

    // Success
    if (status == 0) {
      exitCode = 0;
    }
  }
  return exitCode;
}

// Converter for transform
char* convert(const string& s) {
  char* result = new char[s.size() + 1];
  strcpy(result, s.c_str());
  return result;
}

// Generate 2d vectors of the tokens for multiple commands.
vector<vector<string> > genMultiTokens(vector<string> tokens) {
  vector<vector<string> > result;
  vector<string> tmp;
  vector<string>::iterator iter;
  // cout << "token size is " << tokens.size() << endl;

  for (iter = tokens.begin(); iter != tokens.end(); ++iter) {
    tmp.push_back(*iter);
    // cout << *iter << endl;
    if (*iter == "|") {
      tmp.pop_back();
      result.push_back(tmp);
      tmp.clear();
    }
  }
  result.push_back(tmp);
  tmp.clear();
  return result;
}

void closePipes(int pipes[], int size) {
  for (int i = 0; i < size; i++) {
    close(pipes[i]);
  }
}

// Handles piping.
// The input is 2d vector of commands
// i.e : [ls],[cat],[grep,shell.cpp],[cut,-d,1-2]
int pipeLoop(vector<string> tokens) {
  vector<vector<string> > multiTokens = genMultiTokens(tokens);
  // Need this for recording the status of each child.
  int status;
  int statusArr[multiTokens.size()];

  // Initialize the pipes.
  // File descriptor size should be (n-1)*2 because there are 2 ends
  // in each pipe (write(1) and read(0)). i.e A <-> B <-> C
  // STD_IN : 0, STD_OUT : 1, STD_ERR : 2
  int pipeFdSize = (multiTokens.size() - 1) * 2;
  int pipes[pipeFdSize];

  // Wait for each child.
  pid_t waitresult;

  // Initilize the fork() array depending on the size of the commands.
  pid_t forkArr[multiTokens.size()];

  // 0 : success , -1 : error
  int return_value = -1;

  // Create the pipes. Notice here that we are offsetting by 2.
  // because a pipe has two ends which requires two slots in the array.
  for (int i = 0; i < pipeFdSize; i += 2) {
    pipe(pipes + i);
  }

  // Now the annoying part.
  for (int i = 0; i < multiTokens.size(); i++) {
    // Call fork() and check error.
    if ((forkArr[i] = fork()) == -1) {
      perror("Pipes fork error ");
      exit(-1);
    }

    // Child process should execute the commands. Before we do that
    // we have to put a pipe between the parent process and the
    // child process. We will use dup2 because it allows us to set
    // STD_OUT and STD_IN for a specified process.
    if (forkArr[i] == 0) {
      // We need to take care of the special cases : first pipe
      // and last pipe. The pipes in the middle have same behavior.
      // i.e : A<->B<->C<->D<->E
      if (i == 0) {
        // First pipe. Duplicate a file descriptor to STD_OUT.
        // A <-> B and A's out is set to the pipe's write.
        dup2(pipes[1], 1);
      } else if (i == multiTokens.size() - 1) {
        // Last index. A<->B...<->D and the last's command needs to read
        // from the pipe. Thus, we set the last pipe's read to STD_IN
        // from previous command.
        dup2(pipes[2 * (multiTokens.size() - 1) - 2], 0);
      } else {
        // Middle pipes. A...<->C<->...E We want to read the STD_OUT from B
        // to the pipe before C and write STD_IN to the pipe for the next
        // command.
        dup2(pipes[2 * (i - 1)], 0);
        dup2(pipes[(2 * i) + 1], 1);
      }
      // Always make sure to close the pipes. So then, the parent knows if
      // the child has reached to the end of file.
      closePipes(pipes, pipeFdSize);
      // Execute the command
      return_value = execute_line(multiTokens[i], builtins);
      exit(return_value);
    }
  }

  // This is the parent process.
  closePipes(pipes, pipeFdSize);

  // Check the status of each child and wait for finishing the execution.
  for (int i = 0; i < multiTokens.size(); i++) {
    waitresult = waitpid(forkArr[i], &status, WUNTRACED);
    statusArr[i] = status;
  }

  // Last status should indicate the success of the piping.
  if (statusArr[multiTokens.size() - 1] == 0) {
    return_value = 0;
  }

  exit(return_value);
  return return_value;
}

// Handles piping.
int pipes(vector<string> tokens) {
  // genMultiTokens(tokens);
  int status;
  int statusArr[3];
  vector<vector<string> > multiTokens = genMultiTokens(tokens);
  // cout << "Size of multiTokens is " << multiTokens.size() << endl;

  int pipefd[4];  // need 4 file descriptors
  pid_t subprocess;
  pid_t waitresult;

  pid_t forkArr[multiTokens.size()];

  int return_value = -1;

  // make a pipe
  pipe(pipefd);      // 1st pipe
  pipe(pipefd + 2);  // second pipe

  // We have 2 fds
  // pipefd[0] = read end of cat -> grep pipe
  // pipefd[1] = write end of cat -> grep pipe
  // 0 std in , 1 std out, 2 std error

  if ((forkArr[0] = fork()) == -1) {
    perror("pipes fork error ");
    exit(1);
  }

  if (forkArr[0] == 0) {
    cout << "child | " << forkArr[0] << " | " << getpid() << endl;

    // 1st child
    // Replace STD out of A with write 1st pipe
    dup2(pipefd[1], 1);

    // close the unused pipes
    close(pipefd[0]);
    close(pipefd[1]);
    close(pipefd[2]);
    close(pipefd[3]);

    // cout << "Execute "<< multiTokens[0][0] << endl;
    // execute the A
    return_value = execute_line(multiTokens[0], builtins);

  } else {
    // Second child of fork
    if ((forkArr[1] = fork()) == 0) {
      cout << "child | " << forkArr[1] << " | " << getpid() << endl;

      // Replace STD in of B with read 1st pipe
      dup2(pipefd[0], 0);
      // Replace STD out of B with write 2nd pipe
      dup2(pipefd[3], 1);

      // close the unused pipes
      close(pipefd[0]);
      close(pipefd[1]);
      close(pipefd[2]);
      close(pipefd[3]);

      // cout << "Execute "<< multiTokens[1][0] << endl;
      // execute the cat
      return_value = execute_line(multiTokens[1], builtins);
    } else {
      // Third child of fork
      if ((forkArr[2] = fork()) == 0) {
        cout << "child | " << forkArr[2] << " | " << getpid() << endl;

        // Replace STD in of C with read 2nd pipe
        dup2(pipefd[2], 0);

        // close the unused pipes
        close(pipefd[0]);
        close(pipefd[1]);
        close(pipefd[2]);
        close(pipefd[3]);

        // cout << "Execute "<< multiTokens[2][0] << endl;
        // execute the cat
        return_value = execute_line(multiTokens[2], builtins);
      }
    }
  }
  // close the unused pipes
  close(pipefd[0]);
  close(pipefd[1]);
  close(pipefd[2]);
  close(pipefd[3]);
  cout << "parent | " << getpid() << endl;

  for (int i = 0; i < 3; i++) {
    waitresult = waitpid(forkArr[i], &status, WUNTRACED);
    statusArr[i] = status;
  }

  // Success
  if (statusArr[2] == 0) {
    return_value = 0;
  }

  exit(return_value);
  return return_value;
}

// Check the command type.
int commandType(vector<string> tokens) {
  int command = 0;
  vector<string>::iterator iter;
  for (iter = tokens.begin(); iter != tokens.end(); iter++) {
    if (*iter == "|") {
      command = 1;
    } else if (*iter == ">>" | *iter == "<" | *iter == ">") {
      command = 2;
    }
  }
  return command;
}

// Return a string representing the prompt to display to the user. It needs to
// include the current working directory and should also use the return value to
// indicate the result (success or failure) of the last command.
string get_prompt(int return_value) {
  // TODO: YOUR CODE GOES HERE
  string emocon = return_value ? ":(" : ":)";

  return pwd() + "prompt " + emocon + " $ ";  // replace with your own code
}

// Return one of the matches, or NULL if there are no more.
char* pop_match(vector<string>& matches) {
  if (matches.size() > 0) {
    const char* match = matches.back().c_str();

    // Delete the last element
    matches.pop_back();

    // We need to return a copy, because readline deallocates when done
    char* copy = (char*)malloc(strlen(match) + 1);
    strcpy(copy, match);

    return copy;
  }

  // No more matches
  return NULL;
}

// Generates environment variables for readline completion. This function will
// be called multiple times by readline and will return a single cstring each
// time.
char* environment_completion_generator(const char* text, int state) {
  // A list of all the matches;
  // Must be static because this function is called repeatedly
  static vector<string> matches;

  // If this is the first time called, construct the matches list with
  // all possible matches
  if (state == 0) {
    // TODO: YOUR CODE GOES HERE
  }

  // Return a single match (one for each time the function is called)
  return pop_match(matches);
}

// Generates commands for readline completion. This function will be called
// multiple times by readline and will return a single cstring each time.
char* command_completion_generator(const char* text, int state) {
  // A list of all the matches;
  // Must be static because this function is called repeatedly
  static vector<string> matches;

  // If this is the first time called, construct the matches list with
  // all possible matches
  if (state == 0) {
    // TODO: YOUR CODE GOES HERE
  }

  // Return a single match (one for each time the function is called)
  return pop_match(matches);
}

// This is the function we registered as rl_attempted_completion_function. It
// attempts to complete with a command, variable name, or filename.
char** word_completion(const char* text, int start, int end) {
  char** matches = NULL;

  if (start == 0) {
    rl_completion_append_character = ' ';
    matches = rl_completion_matches(text, command_completion_generator);
  } else if (text[0] == '$') {
    rl_completion_append_character = ' ';
    matches = rl_completion_matches(text, environment_completion_generator);
  } else {
    rl_completion_append_character = '\0';
    // We get directory matches for free (thanks, readline!)
  }

  return matches;
}

// Transform a C-style string into a C++ vector of string tokens, delimited by
// whitespace.
vector<string> tokenize(const char* line) {
  vector<string> tokens;
  string token;

  // istringstream allows us to treat the string like a stream
  istringstream token_stream(line);

  while (token_stream >> token) {
    tokens.push_back(token);
  }

  // Search for quotation marks, which are explicitly disallowed
  for (size_t i = 0; i < tokens.size(); i++) {
    if (tokens[i].find_first_of("\"'`") != string::npos) {
      cerr << "\", ', and ` characters are not allowed." << endl;

      tokens.clear();
    }
  }

  return tokens;
}

// Executes a line of input by either calling execute_external_command or
// directly invoking the built-in command.
int execute_line(vector<string>& tokens, map<string, command>& builtins) {
  int return_value = 0;
  cout << ">>>>>>>>>>>>>executing>>>>>>>>>>>>>>>" << endl;
  for (int i = 0; i < tokens.size(); i++) {
    cout << tokens[i] << "-";
  }
  cout << endl;
  if (commandType(tokens) == 1 || commandType(tokens) == 2) {
    return_value = execute_external_command(tokens);
  } else if (tokens.size() != 0) {
    map<string, command>::iterator cmd = builtins.find(tokens[0]);

    if (cmd == builtins.end()) {
      return_value = execute_external_command(tokens);
    } else {
      return_value = ((*cmd->second)(tokens));
    }
  }

  return return_value;
}

// Substitutes any tokens that start with a $ with their appropriate value, or
// with an empty string if no match is found.
void variable_substitution(vector<string>& tokens) {
  vector<string>::iterator token;

  for (token = tokens.begin(); token != tokens.end(); ++token) {
    if (token->at(0) == '$') {
      string var_name = token->substr(1);

      if (getenv(var_name.c_str()) != NULL) {
        *token = getenv(var_name.c_str());
      } else if (localvars.find(var_name) != localvars.end()) {
        *token = localvars.find(var_name)->second;
      } else {
        *token = "";
      }
    }
  }
}

// Examines each token and sets an env variable for any that are in the form
// of key=value.
void local_variable_assignment(vector<string>& tokens) {
  vector<string>::iterator token = tokens.begin();

  // Return if the token is alias so it wouldn't remove the key=value
  if (*token == "alias") return;

  while (token != tokens.end()) {
    string::size_type eq_pos = token->find("=");

    // If there is an equal sign in the token, assume the token is var=value
    if (eq_pos != string::npos) {
      string name = token->substr(0, eq_pos);
      string value = token->substr(eq_pos + 1);

      localvars[name] = value;

      token = tokens.erase(token);
    } else {
      ++token;
    }
  }
}

// Manipulate tokens if the command is !! or !#
void tokennizeForSpecialHistory(vector<string>& tokens, char* line) {
  string tmpline = "";

  event_list.pop_back();

  // If the size of the history is 0 then do nothing.
  if (event_list.size() == 0) {
    cout << "There are no recent events." << endl;
    return;
  }

  if (strcmp(line, "!!") == 0) {
    // Access the last command which is -2 because !! is added to the last.
    tmpline = event_list[event_list.size() - 1];
    // Add this command to the history;
    event_list.push_back(tmpline);
    cout << tmpline << endl;
    tokens = tokenize(tmpline.c_str());
  } else if (line[0] == '!') {
    // Parse number from the readin.
    string number;

    for (int i = 1; i < strlen(line); i++) {
      number += line[i];
    }
    // Convert the string to number.
    int index = atoi(number.c_str());

    // Check if the even is in the list.
    if (index >= event_list.size() || index == 0) {
      cout << "!" << index << ": event not found" << endl;
    } else {
      // Access the !index
      tmpline = event_list[index - 1];
      // Add this command to the history.
      event_list.push_back(tmpline);
      cout << tmpline << endl;
      tokens = tokenize(tmpline.c_str());
    }
  }
}

// The main program
int main() {
  // Populate the map of available built-in functions
  builtins["ls"] = &com_ls;
  builtins["cd"] = &com_cd;
  builtins["pwd"] = &com_pwd;
  builtins["alias"] = &com_alias;
  builtins["unalias"] = &com_unalias;
  builtins["echo"] = &com_echo;
  builtins["exit"] = &com_exit;
  builtins["history"] = &com_history;

  // Specify the characters that readline uses to delimit words
  rl_basic_word_break_characters = (char*)WORD_DELIMITERS;

  // Tell the completer that we want to try completion first
  rl_attempted_completion_function = word_completion;

  // The return value of the last command executed
  int return_value = 0;
  // Loop for multiple successive commands

  while (true) {
    // Get the prompt to show, based on the return value of the last command
    string prompt = get_prompt(return_value);

    // Read a line of input from the user
    char* line = readline(prompt.c_str());

    // If the pointer is null, then an EOF has been received (ctrl-d)
    if (!line) {
      break;
    }

    // If the command is non-empty, attempt to execute it
    if (line[0]) {
      add_history(line);

      // Add this command to readline's history
      event_list.push_back(line);

      // Break the raw input line into tokens
      vector<string> tokens = tokenize(line);

      // Check if the command is in the alias.
      // If so replace the tokens with the commands
      map<string, vector<string> >::iterator aliasIter =
          alias_list.find(tokens[0]);
      if (aliasIter != alias_list.end()) {
        tokens = aliasIter->second;
      }

      // Check if the command is special history commands.
      if (strcmp(line, "!!") == 0 || line[0] == '!') {
        tokennizeForSpecialHistory(tokens, line);
      }

      // Handle local variable declarations
      local_variable_assignment(tokens);

      // Substitute variable references
      variable_substitution(tokens);

      // Execute the line
      return_value = execute_line(tokens, builtins);
    }

    // Free the memory for the input string
    free(line);
  }

  return 0;
}
