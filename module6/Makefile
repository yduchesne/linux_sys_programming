CC = gcc
CFLAGS = -I .
TARGET = my-shell
STATIC_DIR= bin/static
SHARED_DIR= bin/shared

format:
	@find . -regex '.*\.\(cpp\|hpp\|cu\|c\|h\)' -exec clang-format -style=file -i {} \;

lint:
	@find . -regex '.*\.\(cpp\|hpp\|cu\|c\|h\)' -exec clang-format --Werror --dry-run -style=file -i {} \;	

static-lib:
	@echo "*** Building static library ***"
	@mkdir -p $(STATIC_DIR)
	$(CC) $(CFLAGS) -c -o $(STATIC_DIR)/my-util.o ./my-util.c
	$(CC) $(CFLAGS) -c -o $(STATIC_DIR)/my-shell.o ./my-shell.c
	$(CC) $(CFLAGS) -c -o $(STATIC_DIR)/my-shell-main.o ./my-shell-main.c
	ar rcs $(STATIC_DIR)/libmyshell.a  $(STATIC_DIR)/my-util.o $(STATIC_DIR)/my-shell.o
	$(CC) $(CFLAGS) $(STATIC_DIR)/my-shell-main.o -L$(STATIC_DIR) -lmyshell -o $(STATIC_DIR)/$(TARGET)

shared-lib:
	@echo "*** Building shared library ***"
	@mkdir -p $(SHARED_DIR)
	$(CC) $(CFLAGS) -c -fPIC -o $(SHARED_DIR)/my-util.o ./my-util.c
	$(CC) $(CFLAGS) -c -fPIC -o $(SHARED_DIR)/my-shell.o ./my-shell.c
	$(CC) $(CFLAGS) -c -fPIC -o $(SHARED_DIR)/my-shell-main.o ./my-shell-main.c	
	$(CC) $(CFLAGS) -shared $(SHARED_DIR)/my-util.o $(SHARED_DIR)/my-shell.o -o $(SHARED_DIR)/libmyshell.so
	$(CC) $(CFLAGS) $(SHARED_DIR)/my-shell-main.o -L$(SHARED_DIR) -lmyshell -o $(SHARED_DIR)/$(TARGET)

all: static-lib shared-lib

clean:
	@rm -Rf bin/