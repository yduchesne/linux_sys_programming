CC = gcc
CFLAGS = -g -Wall
TARGET = my-fork

OUT_DIR=@mkdir -p out

format:
	@find . -regex '.*\.\(cpp\|hpp\|cu\|c\|h\)' -exec clang-format -style=file -i {} \;

lint:
	@find . -regex '.*\.\(cpp\|hpp\|cu\|c\|h\)' -exec clang-format --Werror --dry-run -style=file -i {} \;	

all:
	$(OUT_DIR)
	$(CC) $(CFLAGS) -o out/$(TARGET) $(TARGET).c

clean:
	$(RM) out/$(TARGET)