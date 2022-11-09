CC = gcc
CFLAGS = -g -Wall
TARGET = my-ls

format:
	@find . -regex '.*\.\(cpp\|hpp\|cu\|c\|h\)' -exec clang-format -style=file -i {} \;

lint:
	@find . -regex '.*\.\(cpp\|hpp\|cu\|c\|h\)' -exec clang-format --Werror --dry-run -style=file -i {} \;	

all:
	$(CC) $(CFLAGS) -o out/$(TARGET) $(TARGET).c

clean:
	$(RM) out/$(TARGET)