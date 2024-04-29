#!/bin/bash
make -C ./aesd-char-driver
make -C ./server
sudo ./aesd-char-driver/aesdchar_unload
sudo ./aesd-char-driver/aesdchar_load
