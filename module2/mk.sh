#!/bin/bash

OUT_DIR="out"
PROG_NAME="my-ls"

if ! [ -d "$OUT_DIR" ]; then
    mkdir "$OUT_DIR";
fi

gcc -o "$OUT_DIR/$PROG_NAME" "$PROG_NAME.c"

eval "$OUT_DIR/$PROG_NAME" "$OUT_DIR"