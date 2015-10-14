OBJS = shell.cpp builtins.cpp
NAME = myshell

all: $(NAME)

myshell: $(OBJS)
	g++ $(OBJS) -l readline -o $(NAME)

clean:
	rm -rf $(NAME)